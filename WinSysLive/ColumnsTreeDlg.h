#pragma once

#include "resource.h"
#include "ColumnsDlg.h"

//
// Chooses which columns a list shows, for lists whose columns have categories (ColumnDef::Category): a tree of the
// categories (in the order given, else their first column's table order; collapsed, each titled with how many of its columns are shown) with
// the columns under them, all with check boxes. A category's box shows a partial check when only some of its columns
// are shown; clicking it clears a fully checked category and checks every column of any other (mandatory ones stay
// checked). The search box keeps the columns whose names match,
// with their categories expanded. There's no ordering here: the header's drag and drop orders the columns; a column
// checked here goes after the shown ones. Defaults restores the default columns. Items as CColumnsDlg's, with categories.
//
class CColumnsTreeDlg : public CDialogImpl<CColumnsTreeDlg> {
public:
	enum { IDD = IDD_COLUMNS_TREE };

	using Item = CColumnsDlg::Item;

	// categories: their order in the tree; categories not in it follow in their first column's table order
	CColumnsTreeDlg(std::vector<Item> items, std::vector<Item> defaults, std::vector<std::wstring> categories = {});

	// after IDOK: every column, the shown ones first in display order
	std::vector<Item> const& GetItems() const {
		return m_Items;
	}

	BEGIN_MSG_MAP(CColumnsTreeDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_DEFAULTS, OnDefaults)
		COMMAND_HANDLER(IDC_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_COLUMN_TREE, TVN_ITEMCHANGING, OnItemChanging)
		NOTIFY_HANDLER(IDC_COLUMN_TREE, TVN_ITEMCHANGED, OnItemChanged)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// the tree's state images (TVS_EX_PARTIALCHECKBOXES)
	enum { Unchecked = 1, Checked = 2, Partial = 3 };

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanging(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// the categories and their matching columns; item data: a column's index in m_Items, or -1 - the category's index
	void FillTree();
	// a category's check box and title from its columns
	void UpdateCategory(HTREEITEM category);
	void SetCheck(HTREEITEM item, int state);
	static int GetCheck(CTreeViewCtrl const& tree, HTREEITEM item);

	std::vector<Item> m_Items, m_Defaults;	// m_Items in display order (shown first)
	std::vector<std::wstring> m_Categories;	// in their first column's table order
	std::wstring m_Filter;	// lowercase
	CTreeViewCtrl m_Tree;
	bool m_Filling{ false };
};
