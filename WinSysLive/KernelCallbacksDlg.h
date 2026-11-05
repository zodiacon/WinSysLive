#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/KernelCallbacks.h>

//
// System > Kernel Callbacks and Minifilters: a modal, resizable dialog, read-only (WinSys KernelCallbacks). Two tabs:
// Minifilters, the Filter Manager's filters by altitude (needs administrator rights) above the selected one's instances
// on volumes; Callback Registrations, the loaded drivers that import a callback registration function above the
// selected driver's registration imports, with what each hooks. That an import says a driver can register a callback,
// not that it did, is said in the status line: the kernel's arrays of registered callbacks need a driver to be read.
// "Security-relevant only" (on by default) keeps the drivers that watch processes, threads, images, the registry,
// handles, files or the network, or start before the other boot drivers (ELAM); the rest register for Plug and Play,
// power, bug checks and the like. The filter matches any cell of the upper list's rows (a driver's registration
// functions too); both lists sort by any column (numbers as numbers); Copy copies the list that last had the focus.
// Go to Driver (the button, double-click, the context menu) closes the dialog and selects the driver in the Kernel
// Modules tab: the caller does that with GetDriverPath when DoModal returns IDC_KC_GOTO.
//
class CKernelCallbacksDlg :
	public CDialogImpl<CKernelCallbacksDlg>,
	public CDialogResize<CKernelCallbacksDlg>,
	public CDpiDialogResize<CKernelCallbacksDlg> {
public:
	enum { IDD = IDD_KERNELCALLBACKS };

	// after DoModal returned IDC_KC_GOTO: the driver to go to
	std::wstring const& GetDriverPath() const {
		return m_GoTo;
	}

	BEGIN_DLGRESIZE_MAP(CKernelCallbacksDlg)
		// not a group: the details keep their height (see the ETW Sessions dialog)
		DLGRESIZE_CONTROL(IDC_KC_TABS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_KC_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_KC_RELEVANT, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_KC_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_KC_DETAILS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_KC_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_KC_GOTO, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_KC_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_KC_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CKernelCallbacksDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_HANDLER(IDC_KC_TABS, TCN_SELCHANGE, OnTabChanged)
		NOTIFY_HANDLER(IDC_KC_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_KC_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_HANDLER(IDC_KC_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_KC_RELEVANT, OnRelevant)
		COMMAND_ID_HANDLER(IDC_KC_GOTO, OnGoTo)
		COMMAND_ID_HANDLER(IDC_KC_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_KC_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CKernelCallbacksDlg>)
		CHAIN_MSG_MAP(CDialogResize<CKernelCallbacksDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Tab { Minifilters, Callbacks, _Count };

	// a list's columns, rows and sorting
	struct Table {
		struct Column {
			PCWSTR Name;
			int Width;	// at 96 DPI
			bool Numeric{ false };
		};
		std::vector<Column> Columns;
		std::vector<std::vector<std::wstring>> Rows;	// cells
		std::vector<int> Shown;	// row indices in display order; the list's item data
		int SortColumn{ 0 };
		bool SortAscending{ true };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRelevant(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Load();
	Tab CurrentTab() const;
	// the tab's columns in both lists, then its rows
	void ShowTab();
	// the rows the filter (and Security-relevant only) keeps, sorted, keeping the selection
	void ShowRows();
	// the selected row's instances or registration imports
	void ShowDetails();
	static void InitColumns(CListViewCtrl& list, Table const& table);
	static void Fill(CListViewCtrl& list, Table& table);
	static void Sort(Table& table);
	bool Keep(int row) const;
	// the selected upper row's index in its tab's source vector, or -1
	int GetSelectedRow() const;
	std::wstring GetSelectedPath() const;
	void UpdateStatus();

	CTabCtrl m_Tabs;
	CListViewCtrl m_List, m_Details;
	Table m_Tables[(int)Tab::_Count];			// the upper list's, by tab
	Table m_DetailTables[(int)Tab::_Count];		// the lower list's (its rows: the selected row's)
	std::vector<WinSys::Minifilter> m_Filters;	// m_Tables[Minifilters].Rows' order
	WinSys::CallbackDrivers m_Drivers;			// m_Tables[Callbacks].Rows' order is Drivers'
	std::vector<std::wstring> m_SearchText;		// lowercase, by callback driver: its function names
	DWORD m_FilterError{ 0 };	// why the minifilters couldn't be read
	std::wstring m_Filter;		// lowercase
	std::wstring m_GoTo;
	HWND m_CopyList{ nullptr };
	bool m_Filling{ false };	// selection changes while the upper list is filled aren't the user's
};
