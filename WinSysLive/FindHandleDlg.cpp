#include "pch.h"
#include "FindHandleDlg.h"
#include "Interfaces.h"
#include "HandleIcons.h"
#include <WinSys/Processes.h>
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/Handles.h>
#include <TlHelp32.h>
#include <Psapi.h>

using namespace WinSys;

namespace {
	// a search stops adding results here
	const size_t MaxResults = 10000;
	// a process whose file name query hung has the rest of its files skipped (the hangs are its pipes being read);
	// past this many hung queries (each leaves a worker thread stuck), files aren't queried at all
	const int MaxHungQueries = 32;
}

CFindHandleDlg::CFindHandleDlg(IMainFrame* frame) : m_Frame(frame) {
}

LRESULT CFindHandleDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_FIND_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP | LVS_EX_SUBITEMIMAGES);
	// the frame's icons (the list has LVS_SHAREIMAGELISTS: they aren't its to destroy)
	m_List.SetImageList(m_Frame->GetImageList(), LVSIL_SMALL);
	auto cm = GetColumnManager(m_List);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	cm->AddColumn(L"Process", LVCFMT_LEFT, width(150), Column::Process);
	cm->AddColumn(L"PID", LVCFMT_RIGHT, width(60), Column::Pid);
	cm->AddColumn(L"Type", LVCFMT_LEFT, width(90), Column::Type);
	cm->AddColumn(L"Handle / Base", LVCFMT_RIGHT, width(130), Column::Value);
	cm->AddColumn(L"Name", LVCFMT_LEFT, width(500), Column::Name);

	CheckDlgButton(IDC_FIND_HANDLES, BST_CHECKED);
	CheckDlgButton(IDC_FIND_DLLS, BST_CHECKED);
	GetDlgItem(IDC_FIND_SEARCH).EnableWindow(FALSE);
	GetDlgItem(IDC_FIND_GOTO).EnableWindow(FALSE);
	CEdit(GetDlgItem(IDC_FIND_TEXT)).SetCueBannerText(L"Part of a handle's name or a DLL's path", TRUE);

	DlgResize_Init(true, true);
	CenterWindow(GetParent());
	return TRUE;
}

LRESULT CFindHandleDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	StopSearch();
	bHandled = FALSE;
	return 0;
}

LRESULT CFindHandleDlg::OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// modeless: the frame creates it again when needed
	DestroyWindow();
	return 0;
}

LRESULT CFindHandleDlg::OnTextChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_Searching)
		GetDlgItem(IDC_FIND_SEARCH).EnableWindow(GetDlgItem(IDC_FIND_TEXT).GetWindowTextLength() > 0);
	return 0;
}

LRESULT CFindHandleDlg::OnSearch(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the same button stops a search in progress
	if (m_Searching)
		StopSearch();
	else
		StartSearch();
	return 0;
}

void CFindHandleDlg::StartSearch() {
	CString text;
	GetDlgItemText(IDC_FIND_TEXT, text);
	text.Trim();
	bool handles = IsDlgButtonChecked(IDC_FIND_HANDLES) == BST_CHECKED;
	bool dlls = IsDlgButtonChecked(IDC_FIND_DLLS) == BST_CHECKED;
	if (text.IsEmpty() || (!handles && !dlls))
		return;
	std::wstring filter(text);
	::CharLowerBuff(filter.data(), (DWORD)filter.size());

	m_Results.clear();
	m_List.SetItemCount(0);
	ClearSort(m_List.m_hWnd);
	m_State = std::make_shared<SearchState>();
	m_State->Id = ++m_LastId;
	m_Searching = true;
	SetDlgItemText(IDC_FIND_SEARCH, L"&Stop");
	GetDlgItem(IDC_FIND_GOTO).EnableWindow(FALSE);
	m_Worker = std::jthread(Search, m_State, m_hWnd, std::move(filter), handles, dlls);
	UpdateStatus(false);
}

void CFindHandleDlg::StopSearch() {
	if (!m_Worker.joinable())
		return;
	m_Worker.request_stop();
	// it checks between handles; a file name query waits at most for its timeout
	m_Worker.join();
	BOOL handled;
	OnProgress(WM_FIND_PROGRESS, TRUE, m_State->Id, handled);
}

void CFindHandleDlg::Search(std::stop_token stop, std::shared_ptr<SearchState> state, HWND hWnd, std::wstring text, bool handles, bool dlls) {
	auto post = [&](bool done) { ::PostMessage(hWnd, WM_FIND_PROGRESS, done, state->Id); };

	// the processes and their image names (for the results, and the names of process and thread handles)
	auto processes = Process::EnumProcessNames();
	std::unordered_map<DWORD, std::wstring> names;
	for (auto& [pid, name] : processes)
		names[pid] = name;
	{
		std::lock_guard lock(state->Lock);
		state->Processes = (int)processes.size();
	}
	auto processName = [&](DWORD pid) {
		auto it = names.find(pid);
		return it == names.end() ? std::wstring() : it->second;
	};

	ObjectNameQuery nameQuery;
	DosNames dosNames;
	size_t found = 0;
	auto add = [&](Result result) {
		std::lock_guard lock(state->Lock);
		if (found >= MaxResults) {
			state->Truncated = true;
			return false;
		}
		state->Pending.push_back(std::move(result));
		found++;
		return true;
	};

	for (auto& [pid, name] : processes) {
		if (stop.stop_requested() || found >= MaxResults)
			break;
		{
			std::lock_guard lock(state->Lock);
			state->Current = name;
		}
		post(false);
		bool opened = false;

		if (dlls) {
			if (auto process = Process::OpenById(pid, ProcessAccessMask::QueryInformation | ProcessAccessMask::VmRead)) {
				opened = true;
				for (auto& module : Process::EnumLoadedModules(process->Handle()))
					if (CFilterBox::Contains(module.Path, text))
						add({ pid, name, L"DLL", module.Base, module.Path });
			}
		}

		if (handles) {
			auto entries = HandleEnumerator::EnumProcessHandles(pid);
			wil::unique_handle hProcess(entries ? ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid) : nullptr);
			if (entries && hProcess) {
				opened = true;
				bool files = true;	// until a file's query hangs in this process
				for (size_t i = 0; i < entries->size(); i++) {
					if ((i & 255) == 0 && stop.stop_requested())
						break;
					auto& entry = (*entries)[i];
					bool file = ObjectTypes::GetName(entry.TypeIndex) == L"File";
					if (file && (!files || nameQuery.GetAbandonedCount() >= MaxHungQueries)) {
						std::lock_guard lock(state->Lock);
						state->FilesSkipped = true;
						continue;
					}
					auto hung = nameQuery.GetAbandonedCount();
					auto handleName = HandleNames::QueryName(hProcess.get(), entry, nameQuery, dosNames, processName);
					if (file && nameQuery.GetAbandonedCount() > hung)
						files = false;
					if (!handleName.empty() && CFilterBox::Contains(handleName, text)
						&& !add({ pid, name, ObjectTypes::GetName(entry.TypeIndex), entry.Value, std::move(handleName) }))
						break;
				}
			}
		}

		std::lock_guard lock(state->Lock);
		if (!opened)
			state->Denied++;
		state->Searched++;
	}
	post(true);
}

LRESULT CFindHandleDlg::OnProgress(UINT /*uMsg*/, WPARAM done, LPARAM lParam, BOOL& /*bHandled*/) {
	// a message from an earlier search (stopped, then another started) is ignored
	if (!m_State || m_State->Id != (unsigned)lParam)
		return 0;
	std::vector<Result> results;
	{
		std::lock_guard lock(m_State->Lock);
		results = std::move(m_State->Pending);
		m_State->Pending.clear();
	}
	if (!results.empty()) {
		// not a range insert: its try/catch warns (C4530) in Release, which builds without /EH
		m_Results.reserve(m_Results.size() + results.size());
		for (auto& r : results)
			m_Results.push_back(std::move(r));
		m_List.SetItemCountEx((int)m_Results.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
		if (auto si = GetSortInfo(m_List))
			DoSort(si);
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}
	if (done && m_Searching) {
		m_Searching = false;
		if (m_Worker.joinable())
			m_Worker.join();
		SetDlgItemText(IDC_FIND_SEARCH, L"&Search");
		GetDlgItem(IDC_FIND_SEARCH).EnableWindow(GetDlgItem(IDC_FIND_TEXT).GetWindowTextLength() > 0);
	}
	UpdateStatus(done);
	return 0;
}

void CFindHandleDlg::UpdateStatus(bool done) {
	if (!m_State)
		return;
	std::lock_guard lock(m_State->Lock);
	std::wstring text;
	if (!done)
		text = std::format(L"Searching {} ({} of {})... {} found", m_State->Current, m_State->Searched, m_State->Processes, m_Results.size());
	else {
		text = std::format(L"{} found in {} of {} processes", m_Results.size(), m_State->Searched, m_State->Processes);
		if (m_State->Denied)
			text += std::format(L"; {} couldn't be opened", m_State->Denied);
		if (m_State->Truncated)
			text += std::format(L"; stopped at {} results", MaxResults);
		if (m_State->FilesSkipped)
			text += L"; some processes' file names were skipped (their queries hung)";
	}
	SetDlgItemText(IDC_FIND_STATUS, text.c_str());
}

CString CFindHandleDlg::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Results.size())
		return L"";
	auto& r = m_Results[row];
	switch (GetColumnManager(m_List)->GetColumnTag<Column>(col)) {
		case Column::Process: return r.ProcessName.c_str();
		case Column::Pid: return std::to_wstring(r.Pid).c_str();
		case Column::Type: return r.Type.c_str();
		case Column::Value: return std::format(L"0x{:X}", r.Value).c_str();
		case Column::Name: return r.Name.c_str();
	}
	return L"";
}

int CFindHandleDlg::GetRowImage(HWND, int row, int col) const {
	// the result's kind, in the leftmost column wherever it was dragged
	if (row >= (int)m_Results.size() || !ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	auto& r = m_Results[row];
	return m_Frame->GetIconIndex(r.Type == L"DLL" ? IDI_MODULES : GetHandleIcon(r.Type, r.Name));
}

void CFindHandleDlg::DoSort(SortInfo const* si) {
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Results, [&](Result const& r1, Result const& r2) {
		int result = 0;
		switch (column) {
			case Column::Process: result = ::_wcsicmp(r1.ProcessName.c_str(), r2.ProcessName.c_str()); break;
			case Column::Pid: result = compare(r1.Pid, r2.Pid); break;
			case Column::Type: result = ::_wcsicmp(r1.Type.c_str(), r2.Type.c_str()); break;
			case Column::Value: result = compare(r1.Value, r2.Value); break;
			case Column::Name: result = ::_wcsicmp(r1.Name.c_str(), r2.Name.c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	// the selection is by row: it would now be another result
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
}

CFindHandleDlg::Result const* CFindHandleDlg::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Results.size() ? &m_Results[selected] : nullptr;
}

LRESULT CFindHandleDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	GetDlgItem(IDC_FIND_GOTO).EnableWindow(GetSelected() != nullptr);
	bHandled = FALSE;
	return 0;
}

bool CFindHandleDlg::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row >= 0 && row < (int)m_Results.size())
		m_Frame->GoToProcess(m_Results[row].Pid);
	return true;
}

LRESULT CFindHandleDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto result = GetSelected())
		m_Frame->GoToProcess(result->Pid);
	return 0;
}
