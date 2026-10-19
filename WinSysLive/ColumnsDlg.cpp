#include "pch.h"
#include "ColumnsDlg.h"

namespace {
	std::wstring ToLower(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}
}

CColumnsDlg::CColumnsDlg(std::vector<Item> items, std::vector<Item> defaults) : m_Items(std::move(items)), m_Defaults(std::move(defaults)) {
}

std::vector<CColumnsDlg::Item> const& CColumnsDlg::GetItems() const {
	return m_Items;
}

LRESULT CColumnsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CEdit filter(GetDlgItem(IDC_FILTER));
	filter.SetCueBannerText(L"Search columns", TRUE);

	m_List.Attach(GetDlgItem(IDC_COLUMNS));
	m_List.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_List.InsertColumn(0, L"Column", LVCFMT_LEFT, 100);
	FillList(0);
	CenterWindow(GetParent());
	// typing searches right away
	filter.SetFocus();
	return FALSE;
}

void CColumnsDlg::FillList(int selectedItem) {
	m_Rows.clear();
	for (int i = 0; i < (int)m_Items.size(); i++)
		if (m_Filter.empty() || ToLower(m_Items[i].Name).find(m_Filter) != std::wstring::npos)
			m_Rows.push_back(i);

	m_Filling = true;
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int selectedRow = m_Rows.empty() ? -1 : 0;
	for (int row = 0; row < (int)m_Rows.size(); row++) {
		auto& item = m_Items[m_Rows[row]];
		// a mandatory column says so, since its check box doesn't respond
		auto text = item.Mandatory ? item.Name + L" (always shown)" : item.Name;
		m_List.InsertItem(row, text.c_str());
		m_List.SetCheckState(row, item.Visible);
		if (m_Rows[row] == selectedItem)
			selectedRow = row;
	}
	// the width the list has, with or without a scroll bar
	m_List.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
	m_List.SetRedraw(TRUE);
	m_Filling = false;
	if (selectedRow >= 0) {
		m_List.SelectItem(selectedRow);
		m_List.EnsureVisible(selectedRow, FALSE);
	}
	UpdateButtons();
}

int CColumnsDlg::GetSelectedItem() const {
	int row = m_List.GetSelectedIndex();
	return row < 0 ? -1 : m_Rows[row];
}

void CColumnsDlg::UpdateButtons() {
	int row = m_List.GetSelectedIndex();
	GetDlgItem(IDC_MOVEUP).EnableWindow(row > 0);
	GetDlgItem(IDC_MOVEDOWN).EnableWindow(row >= 0 && row < (int)m_Rows.size() - 1);
}

LRESULT CColumnsDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		// the shown columns first, keeping their order
		std::ranges::stable_partition(m_Items, &Item::Visible);
	}
	EndDialog(wID);
	return 0;
}

LRESULT CColumnsDlg::OnMove(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int row = m_List.GetSelectedIndex();
	int targetRow = wID == IDC_MOVEUP ? row - 1 : row + 1;
	if (row < 0 || targetRow < 0 || targetRow >= (int)m_Rows.size())
		return 0;

	// moves the item before (or after) its neighbor in the list; with a search, that also takes it past the columns
	// in between that the search hides, while they keep their order
	int from = m_Rows[row], neighbor = m_Rows[targetRow];
	auto item = std::move(m_Items[from]);
	m_Items.erase(m_Items.begin() + from);
	// up: before the neighbor, at its index; down: after it, which is also its old index, since erasing the item moved it back
	int to = neighbor;
	m_Items.insert(m_Items.begin() + to, std::move(item));
	FillList(to);
	// keep the focus in the list, so the button can be pressed again with the keyboard after it's disabled at an end
	if (!::IsWindowEnabled(GetDlgItem(wID)))
		m_List.SetFocus();
	return 0;
}

LRESULT CColumnsDlg::OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Items = m_Defaults;
	FillList(0);
	return 0;
}

LRESULT CColumnsDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_FILTER, text);
	text.Trim();
	m_Filter = ToLower((PCWSTR)text);
	FillList(GetSelectedItem());
	return 0;
}

LRESULT CColumnsDlg::OnItemChanging(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (m_Filling || lv->iItem < 0 || !m_Items[m_Rows[lv->iItem]].Mandatory || !(lv->uChanged & LVIF_STATE))
		return FALSE;
	// a mandatory column can't be unchecked (state image 1 is unchecked, 2 checked)
	return (lv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1);
}

LRESULT CColumnsDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	if (m_Filling)
		return 0;
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	// the check boxes are the state images; the rows change with the search, so the items keep the state
	if (lv->iItem >= 0 && (lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK))
		m_Items[m_Rows[lv->iItem]].Visible = (lv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
	UpdateButtons();
	return 0;
}
