#include "pch.h"
#include "UnloadedModulesPage.h"
#include "Highlight.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <WTLHelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

CProcUnloadedPage::CProcUnloadedPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Props(props), m_Snapshot(snapshot) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_DLL_UNLOADED);
}

LRESULT CProcUnloadedPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_UNLOADED_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
	auto cm = GetColumnManager(m_List);
	// widths at 96 DPI: the column manager scales them to the list's DPI
	cm->AddColumn(L"Name", LVCFMT_LEFT, 180, Column::Name);
	cm->AddColumn(L"Base Address", LVCFMT_RIGHT, 125, Column::BaseAddress);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, 75, Column::Size);
	cm->AddColumn(L"Version", LVCFMT_LEFT, 110, Column::Version);
	cm->AddColumn(L"Time Stamp", LVCFMT_LEFT, 140, Column::TimeStamp);
	cm->AddColumn(L"Checksum", LVCFMT_RIGHT, 80, Column::CheckSum);
	cm->AddColumn(L"Unload #", LVCFMT_RIGHT, 65, Column::Sequence);
	UpdateButtons();

	// the 32-bit ntdll keeps its own list, which isn't read
	USHORT machine = IMAGE_FILE_MACHINE_UNKNOWN, native = IMAGE_FILE_MACHINE_UNKNOWN;
	if (auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION);
		hProcess && ::IsWow64Process2(hProcess.get(), &machine, &native))
		m_Wow64 = machine != IMAGE_FILE_MACHINE_UNKNOWN;

	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcUnloadedPage::OnSetActive() {
	// the sheet's last snapshot; the sheet feeds the page only while it's active
	OnSample(m_Snapshot);
	return 0;
}

void CProcUnloadedPage::OnSample(ProcessSnapshot const& snapshot) {
	if (m_Exited)
		return;
	if (snapshot.Exited()) {
		// the last list stays, as it was
		m_Exited = true;
		for (auto& r : m_Rows)
			r.NewTicks = 0;
		m_List.Invalidate();
		SetDlgItemText(IDC_UNLOADED_STATUS, L"The process has exited");
		return;
	}

	auto selected = GetSelectedSequences();
	if (!Load())
		return;
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectSequences(selected);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	UpdateStatus();
	GetDlgItem(IDC_UNLOADED_COPYALL).EnableWindow(!m_Rows.empty());
	UpdateButtons();
}

bool CProcUnloadedPage::Load() {
	auto modules = UnloadedModules::Query(m_Props.Id);
	if (!modules) {
		auto error = ::GetLastError();
		m_Rows.clear();
		m_Loaded = false;
		m_List.SetItemCount(0);
		GetDlgItem(IDC_UNLOADED_COPYALL).EnableWindow(FALSE);
		SetDlgItemText(IDC_UNLOADED_STATUS, ErrorText(error).c_str());
		return false;
	}
	// the trace is a ring: what dropped out of it goes; the rest keep their highlight, new ones get one
	std::vector<Row> rows;
	rows.reserve(modules->size());
	for (auto& m : *modules) {
		Row r;
		static_cast<UnloadedModule&>(r) = m;
		auto it = std::ranges::find_if(m_Rows, [&](auto& old) { return old.Sequence == m.Sequence && old.BaseAddress == m.BaseAddress; });
		if (it != m_Rows.end())
			r.NewTicks = it->NewTicks > 0 ? it->NewTicks - 1 : 0;
		else
			r.NewTicks = m_Loaded ? Highlight::NewTicks() : 0;
		rows.push_back(std::move(r));
	}
	m_Rows = std::move(rows);
	m_Loaded = true;
	return true;
}

std::wstring CProcUnloadedPage::ErrorText(DWORD error) const {
	if (error == ERROR_ACCESS_DENIED)
		return SecurityHelper::IsRunningElevated() ? L"Can't read the unloaded modules: access denied (a protected process)"
			: L"Can't read the unloaded modules: access denied (run as administrator)";
	return std::format(L"Can't read the unloaded modules: {}", Helpers::GetErrorText(error));
}

void CProcUnloadedPage::UpdateStatus() {
	std::wstring status = L"No module unloaded yet";
	if (!m_Rows.empty()) {
		auto newest = std::ranges::max(m_Rows, {}, &Row::Sequence).Sequence;
		status = std::format(L"{} unloaded module{}, the newest unload #{} (the loader keeps the last 64)", m_Rows.size(),
			m_Rows.size() == 1 ? L"" : L"s", newest);
	}
	if (m_Wow64)
		status += L"; a 32-bit process: the 64-bit DLLs only";
	SetDlgItemText(IDC_UNLOADED_STATUS, status.c_str());
}

std::wstring CProcUnloadedPage::GetText(Row const& r, Column column) const {
	switch (column) {
		case Column::Name: return r.Name;
		case Column::BaseAddress: return std::format(L"0x{:X}", r.BaseAddress);
		case Column::Size: return FormatHelper::FormatSize(r.Size);
		case Column::Version:
			return r.Version ? std::format(L"{}.{}.{}.{}", r.Version >> 48, (r.Version >> 32) & 0xFFFF, (r.Version >> 16) & 0xFFFF, r.Version & 0xFFFF) : L"";
		case Column::TimeStamp:
		{
			if (!r.TimeDateStamp)
				return L"";
			// seconds since 1970; outside 1990 to tomorrow it's a reproducible build's hash, not a time
			auto time = (int64_t(r.TimeDateStamp) + 11644473600LL) * 10000000LL;
			int64_t now;
			::GetSystemTimeAsFileTime((FILETIME*)&now);
			if (r.TimeDateStamp < 631152000 || time > now + 864000000000LL)
				return std::format(L"0x{:08X}", r.TimeDateStamp);
			return FormatHelper::FormatDateTime(time);
		}
		case Column::CheckSum: return std::format(L"0x{:X}", r.CheckSum);
		case Column::Sequence: return std::to_wstring(r.Sequence);
	}
	return L"";
}

CString CProcUnloadedPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

void CProcUnloadedPage::DoSort(SortInfo const* si) {
	auto selected = GetSelectedSequences();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Row const& r1, Row const& r2) {
		int result = 0;
		switch (column) {
			case Column::Name: result = ::_wcsicmp(r1.Name.c_str(), r2.Name.c_str()); break;
			case Column::BaseAddress: result = compare(r1.BaseAddress, r2.BaseAddress); break;
			case Column::Size: result = compare(r1.Size, r2.Size); break;
			case Column::Version: result = compare(r1.Version, r2.Version); break;
			case Column::TimeStamp: result = compare(r1.TimeDateStamp, r2.TimeDateStamp); break;
			case Column::CheckSum: result = compare(r1.CheckSum, r2.CheckSum); break;
			case Column::Sequence: result = compare(r1.Sequence, r2.Sequence); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectSequences(selected);
}

std::vector<uint32_t> CProcUnloadedPage::GetSelectedSequences() const {
	// the focused row first, so it keeps the focus
	std::vector<uint32_t> sequences;
	int focused = m_List.GetNextItem(-1, LVNI_FOCUSED | LVNI_SELECTED);
	if (focused >= 0 && focused < (int)m_Rows.size())
		sequences.push_back(m_Rows[focused].Sequence);
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size(); i = m_List.GetNextItem(i, LVNI_SELECTED))
		if (i != focused)
			sequences.push_back(m_Rows[i].Sequence);
	return sequences;
}

void CProcUnloadedPage::SelectSequences(std::vector<uint32_t> const& sequences) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	for (size_t k = 0; k < sequences.size(); k++) {
		auto it = std::ranges::find_if(m_Rows, [&](auto& r) { return r.Sequence == sequences[k]; });
		if (it != m_Rows.end())
			m_List.SetItemState((int)(it - m_Rows.begin()), k == 0 ? LVIS_SELECTED | LVIS_FOCUSED : LVIS_SELECTED,
				LVIS_SELECTED | LVIS_FOCUSED);
	}
}

void CProcUnloadedPage::OnStateChanged(HWND, int, int, UINT, UINT) {
	// a range of rows selected with Shift
	UpdateButtons();
}

LRESULT CProcUnloadedPage::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	UpdateButtons();
	bHandled = FALSE;
	return 0;
}

void CProcUnloadedPage::UpdateButtons() {
	GetDlgItem(IDC_UNLOADED_COPY).EnableWindow(m_List.GetSelectedCount() > 0);
}

LRESULT CProcUnloadedPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index >= 0 && index < (int)m_Rows.size() && m_Rows[index].NewTicks > 0) {
				// the color of new items elsewhere
				bool dark = WTLHelper::IsDarkMode();
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CProcUnloadedPage::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, IDC_UNLOADED_COPY, L"&Copy");
	::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return true;
}

LRESULT CProcUnloadedPage::OnCopy(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a line per module, its columns separated by tabs
	std::wstring text;
	for (int i = id == IDC_UNLOADED_COPYALL ? 0 : m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size();
		i = id == IDC_UNLOADED_COPYALL ? i + 1 : m_List.GetNextItem(i, LVNI_SELECTED)) {
		for (int c = 0; c < (int)Column::_Count; c++) {
			if (c > 0)
				text += L'\t';
			text += GetText(m_Rows[i], (Column)c);
		}
		text += L"\r\n";
	}
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
