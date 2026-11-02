#include "pch.h"
#include "HeapsPage.h"
#include "Highlight.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <WTLHelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

CProcHeapsPage::CProcHeapsPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Props(props), m_Snapshot(snapshot) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_HEAP);
}

LRESULT CProcHeapsPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_HEAPS_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
	auto cm = GetColumnManager(m_List);
	// widths at 96 DPI: the column manager scales them to the list's DPI
	cm->AddColumn(L"Address", LVCFMT_RIGHT, 125, Column::Address);
	cm->AddColumn(L"Kind", LVCFMT_LEFT, 95, Column::Kind);
	cm->AddColumn(L"Class", LVCFMT_LEFT, 70, Column::Class);
	cm->AddColumn(L"Committed", LVCFMT_RIGHT, 85, Column::Committed);
	cm->AddColumn(L"Reserved", LVCFMT_RIGHT, 85, Column::Reserved);
	cm->AddColumn(L"Used", LVCFMT_RIGHT, 85, Column::Used);
	cm->AddColumn(L"Used %", LVCFMT_RIGHT, 55, Column::UsedPercent);
	cm->AddColumn(L"Segments", LVCFMT_RIGHT, 65, Column::Segments);
	cm->AddColumn(L"Flags", LVCFMT_LEFT, 160, Column::Flags);
	UpdateButtons();

	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcHeapsPage::OnSetActive() {
	// the sheet's last snapshot; the sheet feeds the page only while it's active
	OnSample(m_Snapshot);
	return 0;
}

void CProcHeapsPage::OnSample(ProcessSnapshot const& snapshot) {
	if (m_Exited)
		return;
	if (snapshot.Exited()) {
		// the last heaps stay, as they were
		m_Exited = true;
		std::erase_if(m_Rows, [](auto& r) { return r.GoneTicks > 0; });
		for (auto& r : m_Rows)
			r.NewTicks = 0;
		m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
		SetDlgItemText(IDC_HEAPS_STATUS, L"The process has exited");
		return;
	}

	auto selected = GetSelectedAddresses();
	if (!Load())
		return;

	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectAddresses(selected);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	UpdateStatus();
	GetDlgItem(IDC_HEAPS_COPYALL).EnableWindow(!m_Rows.empty());
	UpdateButtons();
}

bool CProcHeapsPage::Load() {
	auto heaps = ProcessHeaps::Query(m_Props.Id);
	if (!heaps) {
		auto error = ::GetLastError();
		// a refusal now (or after the process went) leaves no rows to mislead
		m_Rows.clear();
		m_Loaded = false;
		m_List.SetItemCount(0);
		GetDlgItem(IDC_HEAPS_COPYALL).EnableWindow(FALSE);
		SetDlgItemText(IDC_HEAPS_STATUS, ErrorText(error).c_str());
		return false;
	}

	// gone ones counted down and dropped; the rest marked gone, then found again by address
	std::erase_if(m_Rows, [](auto& r) { return r.GoneTicks > 0 && --r.GoneTicks == 0; });
	std::unordered_map<uint64_t, size_t> index;
	for (size_t i = 0; i < m_Rows.size(); i++) {
		auto& r = m_Rows[i];
		if (r.NewTicks > 0)
			r.NewTicks--;
		if (r.GoneTicks == 0)
			index[r.Address] = i;
	}
	std::vector<bool> seen(m_Rows.size());
	for (auto& heap : *heaps) {
		if (auto it = index.find(heap.Address); it != index.end()) {
			auto& r = m_Rows[it->second];
			static_cast<HeapInfo&>(r) = heap;
			seen[it->second] = true;
		}
		else {
			Row r;
			static_cast<HeapInfo&>(r) = heap;
			// on the first read, all of them are new to the page, not created
			r.NewTicks = m_Loaded ? Highlight::NewTicks() : 0;
			m_Rows.push_back(r);
			seen.push_back(true);
		}
	}
	for (size_t i = 0; i < seen.size(); i++)
		if (!seen[i] && m_Rows[i].GoneTicks == 0) {
			m_Rows[i].GoneTicks = std::max(1, Highlight::ExitedTicks());
			m_Rows[i].NewTicks = 0;
		}
	m_Loaded = true;
	return true;
}

std::wstring CProcHeapsPage::ErrorText(DWORD error) const {
	if (error == ERROR_INVALID_PARAMETER) {
		// the non-invasive query reads 64-bit heaps only
		USHORT machine = IMAGE_FILE_MACHINE_UNKNOWN, native = IMAGE_FILE_MACHINE_UNKNOWN;
		if (auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION);
			hProcess && ::IsWow64Process2(hProcess.get(), &machine, &native) && machine != IMAGE_FILE_MACHINE_UNKNOWN)
			return L"The heaps of a 32-bit process can't be read";
	}
	if (error == ERROR_ACCESS_DENIED)
		return SecurityHelper::IsRunningElevated() ? L"Can't read the heaps: access denied (a protected process)"
			: L"Can't read the heaps: access denied (run as administrator)";
	return std::format(L"Can't read the heaps: {}", Helpers::GetErrorText(error));
}

void CProcHeapsPage::UpdateStatus() {
	size_t count = 0, segment = 0;
	uint64_t committed = 0, reserved = 0;
	for (auto& r : m_Rows) {
		if (r.GoneTicks > 0)
			continue;
		count++;
		if (r.Kind == HeapKind::Segment)
			segment++;
		committed += r.Committed;
		reserved += r.Reserved;
	}
	auto status = std::format(L"{} heap{}", count, count == 1 ? L"" : L"s");
	if (segment)
		status += std::format(L" ({} segment heap{})", segment, segment == 1 ? L"" : L"s");
	status += std::format(L"; {} committed, {} reserved", FormatHelper::FormatSize(committed), FormatHelper::FormatSize(reserved));
	SetDlgItemText(IDC_HEAPS_STATUS, status.c_str());
}

std::wstring CProcHeapsPage::GetText(Row const& r, Column column) const {
	switch (column) {
		case Column::Address: return std::format(L"0x{:X}", r.Address);
		case Column::Kind: return r.Kind == HeapKind::Segment ? L"Segment heap" : L"NT heap";
		case Column::Class: return r.Default ? L"Default" : ProcessHeaps::ClassText(r.Flags);
		case Column::Committed: return FormatHelper::FormatSize(r.Committed);
		case Column::Reserved: return r.Reserved ? FormatHelper::FormatSize(r.Reserved) : L"";
		// the segment heap doesn't report its blocks
		case Column::Used: return r.Kind == HeapKind::Segment ? L"" : FormatHelper::FormatSize(r.Allocated);
		case Column::UsedPercent:
			if (r.Kind == HeapKind::Segment || r.Committed == 0)
				return L"";
			return std::format(L"{:.1f}", 100.0 * r.Allocated / r.Committed);
		case Column::Segments: return r.Segments ? std::to_wstring(r.Segments) : L"";
		case Column::Flags: return ProcessHeaps::FlagsText(r.Flags);
	}
	return L"";
}

CString CProcHeapsPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

void CProcHeapsPage::DoSort(SortInfo const* si) {
	auto selected = GetSelectedAddresses();

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	// the share in use, with the segment heaps (which don't report it) below every NT heap
	auto used = [](Row const& r) { return r.Kind == HeapKind::Segment || r.Committed == 0 ? -1.0 : double(r.Allocated) / r.Committed; };
	std::ranges::stable_sort(m_Rows, [&](Row const& r1, Row const& r2) {
		int result = 0;
		switch (column) {
			case Column::Address: result = compare(r1.Address, r2.Address); break;
			case Column::Committed: result = compare(r1.Committed, r2.Committed); break;
			case Column::Reserved: result = compare(r1.Reserved, r2.Reserved); break;
			case Column::Used: result = compare(r1.Kind == HeapKind::Segment ? -1 : (int64_t)r1.Allocated, r2.Kind == HeapKind::Segment ? -1 : (int64_t)r2.Allocated); break;
			case Column::UsedPercent: result = compare(used(r1), used(r2)); break;
			case Column::Segments: result = compare(r1.Segments, r2.Segments); break;
			default: result = ::_wcsicmp(GetText(r1, column).c_str(), GetText(r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectAddresses(selected);
}

std::vector<uint64_t> CProcHeapsPage::GetSelectedAddresses() const {
	// the focused row first, so it keeps the focus
	std::vector<uint64_t> addresses;
	int focused = m_List.GetNextItem(-1, LVNI_FOCUSED | LVNI_SELECTED);
	if (focused >= 0 && focused < (int)m_Rows.size())
		addresses.push_back(m_Rows[focused].Address);
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size(); i = m_List.GetNextItem(i, LVNI_SELECTED))
		if (i != focused)
			addresses.push_back(m_Rows[i].Address);
	return addresses;
}

void CProcHeapsPage::SelectAddresses(std::vector<uint64_t> const& addresses) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	for (size_t k = 0; k < addresses.size(); k++) {
		auto it = std::ranges::find_if(m_Rows, [&](auto& r) { return r.Address == addresses[k]; });
		if (it != m_Rows.end())
			m_List.SetItemState((int)(it - m_Rows.begin()), k == 0 ? LVIS_SELECTED | LVIS_FOCUSED : LVIS_SELECTED,
				LVIS_SELECTED | LVIS_FOCUSED);
	}
}

void CProcHeapsPage::OnStateChanged(HWND, int, int, UINT, UINT) {
	// a range of rows selected with Shift
	UpdateButtons();
}

LRESULT CProcHeapsPage::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	UpdateButtons();
	bHandled = FALSE;
	return 0;
}

void CProcHeapsPage::UpdateButtons() {
	GetDlgItem(IDC_HEAPS_COPY).EnableWindow(m_List.GetSelectedCount() > 0);
}

LRESULT CProcHeapsPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
		{
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto& row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and closed connections on the TCP/IP page
			if (row.GoneTicks > 0) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CProcHeapsPage::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, IDC_HEAPS_COPY, L"&Copy");
	::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return true;
}

LRESULT CProcHeapsPage::OnCopy(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a line per heap, its columns separated by tabs
	std::wstring text;
	for (int i = id == IDC_HEAPS_COPYALL ? 0 : m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size();
		i = id == IDC_HEAPS_COPYALL ? i + 1 : m_List.GetNextItem(i, LVNI_SELECTED)) {
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
