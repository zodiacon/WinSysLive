#include "pch.h"
#include "FindHandleDlg.h"
#include "Interfaces.h"
#include "HandleIcons.h"
#include "HandleSheet.h"
#include <WinSys/Helpers.h>
#include <WinSys/Processes.h>
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/Handles.h>
#include <WinSys/ProcessModuleTracker.h>
#include "LockPath.h"
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
	// widths at 96 DPI: the column manager scales them to the list's DPI
	cm->AddColumn(L"Process", LVCFMT_LEFT, 150, Column::Process);
	cm->AddColumn(L"PID", LVCFMT_RIGHT, 60, Column::Pid);
	cm->AddColumn(L"Type", LVCFMT_LEFT, 90, Column::Type);
	cm->AddColumn(L"Handle / Base", LVCFMT_RIGHT, 130, Column::Value);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 500, Column::Name);

	CheckDlgButton(IDC_FIND_HANDLES, BST_CHECKED);
	CheckDlgButton(IDC_FIND_DLLS, BST_CHECKED);
	GetDlgItem(IDC_FIND_SEARCH).EnableWindow(FALSE);
	UpdateButtons();
	// Mapped Files is a VirtualQueryEx walk of every process: off until asked for (Who's Locking checks it)
	CEdit(GetDlgItem(IDC_FIND_TEXT)).SetCueBannerText(L"Part of a handle's name or a DLL's path, or a file's or folder's path", TRUE);

	DlgResize_Init(true, true);
	InitDpi();
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
	SearchOptions options{
		IsDlgButtonChecked(IDC_FIND_HANDLES) == BST_CHECKED,
		IsDlgButtonChecked(IDC_FIND_DLLS) == BST_CHECKED,
		IsDlgButtonChecked(IDC_FIND_MAPPED) == BST_CHECKED,
		IsDlgButtonChecked(IDC_FIND_EXACT) == BST_CHECKED,
	};
	if (text.IsEmpty() || (!options.Handles && !options.Dlls && !options.Mapped))
		return;
	std::wstring filter(text);
	// CFilterBox::Contains takes the text lowercased; LockPath compares without case
	if (!options.WholePath)
		::CharLowerBuff(filter.data(), (DWORD)filter.size());

	m_Results.clear();
	m_List.SetItemCount(0);
	ClearSort(m_List.m_hWnd);
	m_State = std::make_shared<SearchState>();
	m_State->Id = ++m_LastId;
	m_Searching = true;
	SetDlgItemText(IDC_FIND_SEARCH, L"&Stop");
	UpdateButtons();
	m_Worker = std::jthread(Search, m_State, m_hWnd, std::move(filter), options);
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

void CFindHandleDlg::FindLocks(std::wstring const& path) {
	StopSearch();
	SetDlgItemText(IDC_FIND_TEXT, path.c_str());
	for (int id : { IDC_FIND_HANDLES, IDC_FIND_DLLS, IDC_FIND_MAPPED, IDC_FIND_EXACT })
		CheckDlgButton(id, BST_CHECKED);
	StartSearch();
}

void CFindHandleDlg::Search(std::stop_token stop, std::shared_ptr<SearchState> state, HWND hWnd, std::wstring text, SearchOptions options) {
	auto post = [&](bool done) { ::PostMessage(hWnd, WM_FIND_PROGRESS, done, state->Id); };
	auto matches = [&](std::wstring const& name) {
		return options.WholePath ? LockPath::Matches(name, text) : CFilterBox::Contains(name, text);
	};

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

		if (options.Dlls || options.Mapped) {
			if (auto process = Process::OpenById(pid, ProcessAccessMask::QueryInformation | ProcessAccessMask::VmRead)) {
				opened = true;
				auto created = process->GetCreateTime();
				if (options.Dlls)
					for (auto& module : Process::EnumLoadedModules(process->Handle()))
						if (matches(module.Path))
							add({ pid, name, L"DLL", module.Base, module.Path, std::nullopt, created });
				// the views of data files (the images are the DLLs)
				if (options.Mapped && !stop.stop_requested()) {
					ProcessModuleTracker tracker(process->Handle());
					tracker.EnumModules();
					for (auto& m : tracker.GetModules())
						if (m->Type == MapType::Data && !m->Path.empty() && matches(m->Path))
							add({ pid, name, L"Mapped File", (uint64_t)m->Base, m->Path, std::nullopt, created });
				}
			}
		}

		if (options.Handles) {
			auto entries = HandleEnumerator::EnumProcessHandles(pid);
			auto process = entries ? Process::OpenById(pid, ProcessAccessMask::DuplicateHandle | ProcessAccessMask::QueryLimitedInformation) : nullptr;
			if (entries && process) {
				opened = true;
				auto hProcess = process->Handle();
				auto created = process->GetCreateTime();
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
					auto handleName = HandleNames::QueryName(hProcess, entry, nameQuery, dosNames, processName);
					if (file && nameQuery.GetAbandonedCount() > hung)
						files = false;
					if (!handleName.empty() && matches(handleName)
						&& !add({ pid, name, ObjectTypes::GetName(entry.TypeIndex), entry.Value, std::move(handleName), entry, created }))
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
	if (r.Type == L"DLL")
		return m_Frame->GetIconIndex(IDI_MODULES);
	if (r.Type == L"Mapped File")
		return m_Frame->GetIconIndex(IDI_MODULE_DATA);
	return m_Frame->GetIconIndex(GetHandleIcon(r.Type, r.Name));
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

void CFindHandleDlg::UpdateButtons() {
	auto result = GetSelected();
	GetDlgItem(IDC_FIND_GOTO).EnableWindow(result != nullptr);
	GetDlgItem(IDC_FIND_PROPERTIES).EnableWindow(result && result->Handle);
	// not this process's own (closing them could break it), nor Idle's and System's
	bool own = result && result->Pid == ::GetCurrentProcessId();
	GetDlgItem(IDC_FIND_CLOSEHANDLE).EnableWindow(result && result->Handle && !own);
	GetDlgItem(IDC_FIND_KILL).EnableWindow(result && !own && result->Pid > 4);
}

LRESULT CFindHandleDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	UpdateButtons();
	bHandled = FALSE;
	return 0;
}

LRESULT CFindHandleDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto result = GetSelected();
	if ((HWND)wParam != m_List || !result) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the selected row
		CRect rc;
		m_List.GetItemRect(m_List.GetSelectedIndex(), &rc, LVIR_LABEL);
		m_List.ClientToScreen(&rc);
		pt = { rc.left, rc.bottom };
	}
	else {
		// the header's isn't a row's
		CRect header;
		m_List.GetHeader().GetWindowRect(&header);
		if (header.PtInRect(pt)) {
			bHandled = FALSE;
			return 0;
		}
	}
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, IDC_FIND_GOTO, L"&Go to Process");
	menu.AppendMenu(result->Handle ? MF_STRING : MF_STRING | MF_GRAYED, IDC_FIND_PROPERTIES, L"&Properties...");
	menu.AppendMenu(MF_SEPARATOR);
	auto state = [&](int id) { return GetDlgItem(id).IsWindowEnabled() ? MF_STRING : MF_STRING | MF_GRAYED; };
	menu.AppendMenu(state(IDC_FIND_CLOSEHANDLE), IDC_FIND_CLOSEHANDLE, L"Close &Handle");
	menu.AppendMenu(state(IDC_FIND_KILL), IDC_FIND_KILL, L"&Kill Process");
	menu.SetMenuDefaultItem(IDC_FIND_GOTO);
	::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return 0;
}

LRESULT CFindHandleDlg::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto result = GetSelected();
	if (!result || !result->Handle)
		return 0;
	// copies: a search in progress may add results while the dialog runs
	auto r = *result;
	// not another process that got its ID since the search
	auto process = Process::OpenById(r.Pid, r.Created, ProcessAccessMask::DuplicateHandle);
	if (!process) {
		ShowOpenError(r);
		return 0;
	}
	CHandleSheet sheet(process->Handle(), r.Pid, r.ProcessName, *r.Handle, r.Name,
		[this](DWORD pid) { return m_Frame->GetProcessManager().GetProcessNameById(pid); });
	sheet.DoModal(m_hWnd);
	return 0;
}

void CFindHandleDlg::ShowOpenError(Result const& r) const {
	// ERROR_NOT_FOUND: another process has the ID now; ERROR_INVALID_PARAMETER: none has
	auto error = ::GetLastError();
	auto text = error == ERROR_NOT_FOUND || error == ERROR_INVALID_PARAMETER
		? std::format(L"{} ({}) has exited since the search.", r.ProcessName, r.Pid)
		: std::format(L"Failed to open the process: {}", WinSys::Helpers::GetErrorText(error));
	AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

void CFindHandleDlg::RemoveResults(std::function<bool(Result const&)> which) {
	std::erase_if(m_Results, which);
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
	m_List.SetItemCountEx((int)m_Results.size(), LVSICF_NOSCROLL);
	m_List.Invalidate();
	UpdateButtons();
	if (!m_Searching)
		UpdateStatus(true);
}

LRESULT CFindHandleDlg::OnCloseHandle(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto result = GetSelected();
	if (!result || !result->Handle || result->Pid == ::GetCurrentProcessId())
		return 0;
	// a copy: the search may add results while the message box is up
	auto r = *result;
	auto question = std::format(L"Close handle 0x{:X} ({}: {}) in {} ({})?\n\nThe process may fail when it uses the handle.",
		r.Value, r.Type, r.Name, r.ProcessName, r.Pid);
	if (AtlMessageBox(m_hWnd, question.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;
	auto process = Process::OpenById(r.Pid, r.Created, ProcessAccessMask::DuplicateHandle);
	if (!process) {
		ShowOpenError(r);
		return 0;
	}
	// the handle may have been closed and its value reused since the search: checked against the type
	if (!HandleNames::CloseInProcess(process->Handle(), r.Value, r.Type)) {
		auto error = ::GetLastError();
		if (error != ERROR_INVALID_HANDLE) {
			AtlMessageBox(m_hWnd, std::format(L"Failed to close the handle: {}", WinSys::Helpers::GetErrorText(error)).c_str(),
				IDR_MAINFRAME, MB_ICONERROR);
			return 0;
		}
		AtlMessageBox(m_hWnd, L"The handle isn't there anymore (or is another object's now).", IDR_MAINFRAME, MB_ICONWARNING);
	}
	RemoveResults([&](auto& x) { return x.Pid == r.Pid && x.Created == r.Created && x.Handle && x.Value == r.Value; });
	return 0;
}

LRESULT CFindHandleDlg::OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto result = GetSelected();
	if (!result || result->Pid == ::GetCurrentProcessId() || result->Pid <= 4)
		return 0;
	auto r = *result;
	auto question = std::format(L"Kill {} ({})? Unsaved work in it will be lost.", r.ProcessName, r.Pid);
	if (AtlMessageBox(m_hWnd, question.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;
	auto process = Process::OpenById(r.Pid, r.Created, ProcessAccessMask::Terminate);
	if (!process) {
		ShowOpenError(r);
		return 0;
	}
	if (!process->Terminate()) {
		AtlMessageBox(m_hWnd, std::format(L"Failed to kill the process: {}", WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	// all its results go with it
	RemoveResults([&](auto& x) { return x.Pid == r.Pid && x.Created == r.Created; });
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
