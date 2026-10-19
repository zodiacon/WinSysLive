#pragma once

#include "resource.h"

//
// Chooses which columns a list shows and in what order: a check list with Move Up/Move Down and a search box that
// narrows the list. Mandatory columns stay checked. The caller passes every column, the shown ones first in display order.
//
class CColumnsDlg : public CDialogImpl<CColumnsDlg> {
public:
	enum { IDD = IDD_COLUMNS };

	struct Item {
		int Id;
		std::wstring Name;
		bool Visible;
		bool Mandatory{ false };
	};

	// defaults: what the Defaults button restores, in the same form as items
	CColumnsDlg(std::vector<Item> items, std::vector<Item> defaults);

	// after IDOK: every column, the shown ones first in display order
	std::vector<Item> const& GetItems() const;

	BEGIN_MSG_MAP(CColumnsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_MOVEUP, OnMove)
		COMMAND_ID_HANDLER(IDC_MOVEDOWN, OnMove)
		COMMAND_ID_HANDLER(IDC_DEFAULTS, OnDefaults)
		COMMAND_HANDLER(IDC_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_COLUMNS, LVN_ITEMCHANGING, OnItemChanging)
		NOTIFY_HANDLER(IDC_COLUMNS, LVN_ITEMCHANGED, OnItemChanged)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnMove(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanging(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// shows the items that match the search text; selects the given item if it's shown, else the first row
	void FillList(int selectedItem);
	int GetSelectedItem() const;	// index in m_Items, or -1
	void UpdateButtons();

	std::vector<Item> m_Items, m_Defaults;	// m_Items is in display order
	std::vector<int> m_Rows;	// the list's rows: indices in m_Items
	std::wstring m_Filter;		// lowercase
	CListViewCtrl m_List;
	bool m_Filling{ false };
};
