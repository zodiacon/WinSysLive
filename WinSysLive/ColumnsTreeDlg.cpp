#include "pch.h"
#include "ColumnsTreeDlg.h"

namespace {
	std::wstring ToLower(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}
}

CColumnsTreeDlg::CColumnsTreeDlg(std::vector<Item> items, std::vector<Item> defaults, std::vector<std::wstring> categories) :
	m_Items(std::move(items)), m_Defaults(std::move(defaults)) {
	// the categories given that have columns, then the others in table order (the IDs give it: the items have the
	// shown ones first)
	auto ordered = m_Items;
	std::ranges::sort(ordered, {}, &Item::Id);
	for (auto& category : categories)
		if (std::ranges::any_of(ordered, [&](auto& item) { return item.Category == category; }))
			m_Categories.push_back(category);
	for (auto& item : ordered)
		if (!std::ranges::contains(m_Categories, item.Category))
			m_Categories.push_back(item.Category);
}

LRESULT CColumnsTreeDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CEdit filter(GetDlgItem(IDC_FILTER));
	filter.SetCueBannerText(L"Search columns", TRUE);
	m_Tree.Attach(GetDlgItem(IDC_COLUMN_TREE));
	// the partial check box gives the tree its check boxes too
	m_Tree.SetExtendedStyle(TVS_EX_PARTIALCHECKBOXES | TVS_EX_DOUBLEBUFFER, TVS_EX_PARTIALCHECKBOXES | TVS_EX_DOUBLEBUFFER);
	FillTree();
	CenterWindow(GetParent());
	// typing searches right away
	filter.SetFocus();
	return FALSE;
}

int CColumnsTreeDlg::GetCheck(CTreeViewCtrl const& tree, HTREEITEM item) {
	return (int)(tree.GetItemState(item, TVIS_STATEIMAGEMASK) >> 12);
}

void CColumnsTreeDlg::SetCheck(HTREEITEM item, int state) {
	m_Tree.SetItemState(item, INDEXTOSTATEIMAGEMASK(state), TVIS_STATEIMAGEMASK);
}

void CColumnsTreeDlg::FillTree() {
	m_Filling = true;
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	// the columns in table order under their categories
	std::vector<int> order(m_Items.size());
	for (int i = 0; i < (int)order.size(); i++)
		order[i] = i;
	std::ranges::sort(order, {}, [&](int i) { return m_Items[i].Id; });
	for (int c = 0; c < (int)m_Categories.size(); c++) {
		HTREEITEM category = nullptr;
		for (int i : order) {
			auto& item = m_Items[i];
			if (item.Category != m_Categories[c] || (!m_Filter.empty() && ToLower(item.Name).find(m_Filter) == std::wstring::npos))
				continue;
			if (!category)
				category = m_Tree.InsertItem(TVIF_TEXT | TVIF_PARAM, m_Categories[c].c_str(), 0, 0, 0, 0, -1 - c, TVI_ROOT, TVI_LAST);
			// a mandatory column says so, since its check box doesn't respond
			auto text = item.Mandatory ? item.Name + L" (always shown)" : item.Name;
			auto node = m_Tree.InsertItem(TVIF_TEXT | TVIF_PARAM, text.c_str(), 0, 0, 0, 0, i, category, TVI_LAST);
			SetCheck(node, item.Visible ? Checked : Unchecked);
		}
		if (category) {
			UpdateCategory(category);
			// collapsed, unless searching
			if (!m_Filter.empty())
				m_Tree.Expand(category, TVE_EXPAND);
		}
	}
	m_Tree.SetRedraw(TRUE);
	if (auto first = m_Tree.GetRootItem())
		m_Tree.SelectItem(first);
	m_Filling = false;
}

void CColumnsTreeDlg::UpdateCategory(HTREEITEM category) {
	// counted over the category's columns, also those the search hides
	auto index = -1 - (int)m_Tree.GetItemData(category);
	int shown = 0, total = 0;
	for (auto& item : m_Items)
		if (item.Category == m_Categories[index]) {
			total++;
			shown += item.Visible;
		}
	bool filling = m_Filling;
	m_Filling = true;
	SetCheck(category, shown == 0 ? Unchecked : shown == total ? Checked : Partial);
	m_Tree.SetItemText(category, std::format(L"{} ({} of {})", m_Categories[index], shown, total).c_str());
	m_Filling = filling;
}

LRESULT CColumnsTreeDlg::OnItemChanging(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto change = reinterpret_cast<NMTVITEMCHANGE*>(hdr);
	if (m_Filling || !(change->uChanged & TVIF_STATE) || !((change->uStateNew ^ change->uStateOld) & TVIS_STATEIMAGEMASK))
		return FALSE;
	auto data = (int)m_Tree.GetItemData(change->hItem);
	// a mandatory column can't be unchecked
	return data >= 0 && m_Items[data].Mandatory;
}

LRESULT CColumnsTreeDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto change = reinterpret_cast<NMTVITEMCHANGE*>(hdr);
	if (m_Filling || !(change->uChanged & TVIF_STATE) || !((change->uStateNew ^ change->uStateOld) & TVIS_STATEIMAGEMASK))
		return 0;
	auto data = (int)m_Tree.GetItemData(change->hItem);
	int state = (int)((change->uStateNew & TVIS_STATEIMAGEMASK) >> 12);
	if (data >= 0) {
		m_Items[data].Visible = state == Checked;
		UpdateCategory(m_Tree.GetParentItem(change->hItem));
		return 0;
	}
	// a category: the tree cycles its box (unchecked, checked, partial); a checked one is cleared, else all are shown
	bool show = ((change->uStateOld & TVIS_STATEIMAGEMASK) >> 12) != Checked;
	auto& category = m_Categories[-1 - data];
	for (auto& item : m_Items)
		if (item.Category == category && !item.Mandatory)
			item.Visible = show;
	m_Filling = true;
	for (auto child = m_Tree.GetChildItem(change->hItem); child; child = m_Tree.GetNextSiblingItem(child))
		SetCheck(child, m_Items[m_Tree.GetItemData(child)].Visible ? Checked : Unchecked);
	m_Filling = false;
	UpdateCategory(change->hItem);
	return 0;
}

LRESULT CColumnsTreeDlg::OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Items = m_Defaults;
	FillTree();
	return 0;
}

LRESULT CColumnsTreeDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_FILTER, text);
	text.Trim();
	m_Filter = ToLower((PCWSTR)text);
	FillTree();
	return 0;
}

LRESULT CColumnsTreeDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		// the shown columns first, keeping their order: the new ones after those shown before
		std::ranges::stable_partition(m_Items, &Item::Visible);
	}
	EndDialog(wID);
	return 0;
}
