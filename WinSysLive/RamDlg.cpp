#include "pch.h"
#include "RamDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

namespace {
	// in CRamDlg::Column order (widths at 96 DPI)
	const struct {
		PCWSTR Name;
		int Width, Format;
	} s_Columns[] = {
		{ L"Usage", 130, LVCFMT_LEFT }, { L"Total", 110, LVCFMT_RIGHT }, { L"% of RAM", 70, LVCFMT_RIGHT },
		{ L"Active", 110, LVCFMT_RIGHT }, { L"Standby", 110, LVCFMT_RIGHT }, { L"Modified", 100, LVCFMT_RIGHT },
		{ L"Modified No-Write", 135, LVCFMT_RIGHT }, { L"Transition", 90, LVCFMT_RIGHT }, { L"Zeroed", 100, LVCFMT_RIGHT },
		{ L"Free", 100, LVCFMT_RIGHT }, { L"Bad", 90, LVCFMT_RIGHT },
	};

	// the list each list column shows, from Active on
	constexpr PageList s_ColumnLists[] = {
		PageList::Active, PageList::Standby, PageList::Modified, PageList::ModifiedNoWrite, PageList::Transition,
		PageList::Zeroed, PageList::Free, PageList::Bad,
	};

	// in PageUse order, as RAMMap names them
	PCWSTR const s_UseNames[] = {
		L"Process Private", L"Mapped File", L"Shareable", L"Page Table", L"Paged Pool", L"Nonpaged Pool",
		L"System PTE", L"Session Private", L"Metafile", L"AWE", L"Driver Locked", L"Kernel Stack", L"Hyperspace",
	};
	static_assert(_countof(s_UseNames) == (int)PageUse::_Known);

	int64_t Now() {
		int64_t now;
		::GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&now));
		return now;
	}
}

LRESULT CRamDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_MEMORY, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_MEMORY, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_List.Attach(GetDlgItem(IDC_RAM_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static_assert(_countof(s_Columns) == (int)Column::_Count);
	static_assert(_countof(s_ColumnLists) == (int)Column::_Count - (int)Column::Active);
	for (int i = 0; i < _countof(s_Columns); i++)
		m_List.InsertColumn(i, s_Columns[i].Name, s_Columns[i].Format, ::MulDiv(s_Columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	StartScan();
	return TRUE;
}

void CRamDlg::StartScan() {
	if (m_Scanning)
		return;
	m_Scanning = true;
	GetDlgItem(IDC_RAM_SCAN).EnableWindow(FALSE);
	// the previous scan is done (m_Scanning was off): joining it doesn't wait
	m_Worker = std::jthread([this, hWnd = m_hWnd] {
		auto start = ::GetTickCount64();
		auto counts = PageUseCounts::Read(&m_Cancel);
		auto error = counts ? ERROR_SUCCESS : ::GetLastError();
		{
			std::lock_guard lock(m_ResultLock);
			m_Result = std::move(counts);
			m_ResultError = error;
			m_ResultDuration = ::GetTickCount64() - start;
		}
		::PostMessage(hWnd, WM_RAM_SCANNED, 0, 0);
		});
	UpdateStatus();
}

LRESULT CRamDlg::OnScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Scanning = false;
	GetDlgItem(IDC_RAM_SCAN).EnableWindow(TRUE);
	std::optional<PageUseCounts> result;
	{
		std::lock_guard lock(m_ResultLock);
		result = std::move(m_Result);
		m_ScanError = m_ResultError;
		m_ScanDuration = m_ResultDuration;
	}
	if (result) {
		m_Counts = std::move(result);
		m_ScanTime = Now();
		ShowRows();
	}
	UpdateStatus();
	return 0;
}

void CRamDlg::ShowRows() {
	// the selection by key: the rows are made again (the list is multi-select, where WTL's GetSelectedIndex asserts)
	std::vector<int> selected;
	for (int index = m_List.GetNextItem(-1, LVNI_SELECTED); index >= 0; index = m_List.GetNextItem(index, LVNI_SELECTED))
		selected.push_back(m_Rows[m_List.GetItemData(index)].Key);

	m_Rows.clear();
	Row total{ TotalKey };
	auto add = [&](Row& row) {
		for (size_t list = 0; list < row.Pages.size(); list++) {
			row.Total += row.Pages[list];
			total.Pages[list] += row.Pages[list];
		}
		total.Total += row.Total;
		m_Rows.push_back(row);
	};
	for (int use = 0; use < (int)PageUse::_Count; use++) {
		Row row{ use, m_Counts->Pages[use] };
		// the known uses always have a row, others only with pages
		if (use < (int)PageUse::_Known || std::ranges::any_of(row.Pages, [](auto n) { return n > 0; }))
			add(row);
	}
	Row unused{ UnusedKey, m_Counts->Unused };
	add(unused);
	m_Rows.push_back(total);

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		m_List.InsertItem(i, GetText(m_Rows[i], Column::Usage).c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, GetText(m_Rows[i], (Column)c).c_str());
		m_List.SetItemData(i, i);
		if (std::ranges::find(selected, m_Rows[i].Key) != selected.end())
			m_List.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
	}
	m_List.SortItems(Compare, (LPARAM)this);
	m_List.SetRedraw(TRUE);
}

std::wstring CRamDlg::GetUseName(int key) {
	if (key == UnusedKey)
		return L"Unused";
	if (key == TotalKey)
		return L"Total";
	if (key < (int)PageUse::_Known)
		return s_UseNames[key];
	// a use this version doesn't know (Windows 11 has 12, unnamed)
	return std::format(L"Other ({})", key);
}

uint64_t CRamDlg::GetValue(Row const& row, Column column) const {
	auto page = m_Counts ? m_Counts->PageSize : 0;
	switch (column) {
		case Column::Total: return row.Total * page;
		case Column::Share:
			if (!m_Counts || m_Counts->TotalPages == 0)
				return 0;
			return row.Total * 10000 / m_Counts->TotalPages;
	}
	if (column >= Column::Active && column < Column::_Count)
		return row.Pages[(size_t)s_ColumnLists[(int)column - (int)Column::Active]] * page;
	return 0;
}

std::wstring CRamDlg::GetText(Row const& row, Column column) const {
	if (column == Column::Usage)
		return GetUseName(row.Key);
	auto value = GetValue(row, column);
	if (column == Column::Share)
		return row.Total ? std::format(L"{}.{:02}", value / 100, value % 100) : L"";
	// RAMMap's K; empty for nothing, which most cells are
	return value ? FormatHelper::FormatSize((int64_t)value) : L"";
}

int CALLBACK CRamDlg::Compare(LPARAM index1, LPARAM index2, LPARAM param) {
	auto self = reinterpret_cast<CRamDlg*>(param);
	auto& a = self->m_Rows[index1];
	auto& b = self->m_Rows[index2];
	// Total stays last, whichever way
	if (a.Key == TotalKey || b.Key == TotalKey)
		return a.Key == TotalKey ? (b.Key == TotalKey ? 0 : 1) : -1;
	int result;
	if (self->m_SortColumn < 0)
		result = a.Key - b.Key;
	else if ((Column)self->m_SortColumn == Column::Usage)
		result = ::_wcsicmp(GetUseName(a.Key).c_str(), GetUseName(b.Key).c_str());
	else {
		auto v1 = self->GetValue(a, (Column)self->m_SortColumn), v2 = self->GetValue(b, (Column)self->m_SortColumn);
		result = v1 < v2 ? -1 : (v2 < v1 ? 1 : 0);
	}
	return self->m_SortAscending ? result : -result;
}

LRESULT CRamDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		// the sizes largest first
		m_SortAscending = lv->iSubItem == (int)Column::Usage;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	return 0;
}

void CRamDlg::UpdateStatus() {
	std::wstring text;
	if (m_ScanError)
		text = std::format(L"Reading the PFN database failed: {}", Helpers::GetErrorText(m_ScanError));
	else if (!m_Counts)
		text = L"Scanning physical memory...";
	else {
		text = std::format(L"{} of RAM in {} pages; scanned at {} in {:.1f} s",
			FormatHelper::FormatBytes(m_Counts->TotalPages * m_Counts->PageSize), FormatHelper::FormatNumber((int64_t)m_Counts->TotalPages),
			FormatHelper::FormatDateTime(m_ScanTime), m_ScanDuration / 1000.0);
		if (m_Scanning)
			text += L"; scanning again...";
	}
	SetDlgItemText(IDC_RAM_STATUS, text.c_str());
}

LRESULT CRamDlg::OnScanAgain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	StartScan();
	return 0;
}

LRESULT CRamDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the selected rows, else all of them; the column names first
	bool selected = m_List.GetSelectedCount() > 0;
	std::wstring text;
	for (int c = 0; c < (int)Column::_Count; c++)
		text += (c ? L"\t" : L"") + std::wstring(s_Columns[c].Name);
	text += L"\r\n";
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& row = m_Rows[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(row, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CRamDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}

LRESULT CRamDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// a scan in progress stops at its next batch (its message goes nowhere) before the dialog goes
	m_Cancel = true;
	if (m_Worker.joinable())
		m_Worker.join();
	bHandled = FALSE;
	return 0;
}
