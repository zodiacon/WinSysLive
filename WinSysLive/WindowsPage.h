#pragma once

#include "ProcessPages.h"
#include <TreeListView/include/TreeListView.h>
#include <CustomSplitterWindow.h>
#include <WinSys/ProcessWindows.h>

//
// The Windows page: the process's windows (WinSys ProcessWindows) in a TreeListView, top-level windows (in Z order, then
// the message-only ones) with their child windows below them, collapsed; the selected window's properties (styles
// decoded, rectangles, state, DPI...) in a list below it, in a splitter. Read when the page is first shown and on Refresh,
// which keeps the selection and the expanded windows; again shortly after an action, as the windows change
// asynchronously. Show, Hide and Bring to Front act on the selected window at once, Close Window (WM_CLOSE: the window
// decides) after a confirmation. The page lays itself out (WM_SIZE): the panes above the status line, the buttons below.
//
class CProcWindowsPage : public CPropertyPageImpl<CProcWindowsPage> {
public:
	enum { IDD = IDD_PROC_WINDOWS };

	explicit CProcWindowsPage(ProcessProperties const& props);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcWindowsPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_ID_HANDLER(IDC_WIN_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_WIN_SHOW, OnAction)
		COMMAND_ID_HANDLER(IDC_WIN_HIDE, OnAction)
		COMMAND_ID_HANDLER(IDC_WIN_FRONT, OnAction)
		COMMAND_ID_HANDLER(IDC_WIN_CLOSE, OnAction)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelChanged)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcWindowsPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Handle, Class, Text, Visible, Thread, Rectangle, Style, ExStyle, _Count };
	enum { RefreshTimerId = 1, RefreshDelay = 400 };	// after an action

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAction(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	void Layout(int cx, int cy);
	// the windows read again into the tree, keeping the selection and the expanded windows (by handle)
	void Load();
	WinSys::WindowInfo const* GetSelected() const;
	// the selected window's properties into the list below, and the buttons for it
	void ShowDetails();
	std::wstring GetText(WinSys::WindowInfo const& w, Column column) const;

	ProcessProperties const& m_Props;
	CCustomHorSplitterWindow m_Splitter;
	CTreeListViewCtrl m_Tree;
	CListViewCtrl m_Details;
	std::vector<WinSys::WindowInfo> m_Windows;	// in tree order; each item's data is its index here
	bool m_Loaded{ false };
};
