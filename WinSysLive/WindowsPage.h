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
// With a root window (the Window Stations dialog's Child Windows and Properties, in a sheet of its own) it shows that
// window and its descendants only (ProcessWindows::EnumTree), the root expanded and selected at first.
//
// a window's tree and properties: the Windows page alone, rooted at the window, in a resizable sheet of its own (Close
// only), modal over the owner (the Window Stations dialog's Child Windows and Properties, the window finder's drop).
// processName names a window's process (by ID) in the properties; with goToProcess, a Go to Process button closes the
// sheet and calls it with the selected window's process
void ShowWindowTree(HWND owner, HWND window, std::function<std::wstring(uint32_t)> processName = {},
	std::function<void(uint32_t)> goToProcess = {});

class CProcWindowsPage : public CPropertyPageImpl<CProcWindowsPage> {
public:
	enum { IDD = IDD_PROC_WINDOWS };

	explicit CProcWindowsPage(ProcessProperties const& props, HWND root = nullptr);
	// a window's process name, by its ID (without it: the sheet's process's name, if it's that process)
	void SetProcessName(std::function<std::wstring(uint32_t)> processName) { m_ProcessName = std::move(processName); }
	// shows Go to Process, which closes the sheet: the selected window's process is then GetGoToProcessId's
	void EnableGoToProcess() { m_GoToProcessEnabled = true; }
	uint32_t GetGoToProcessId() const { return m_GoToProcessId; }

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
		COMMAND_ID_HANDLER(IDC_WIN_GOTOPROCESS, OnGoToProcess)
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
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	void Layout(int cx, int cy);
	// the windows read again into the tree, keeping the selection and the expanded windows (by handle)
	void Load();
	HWND m_Root{ nullptr };	// the window whose tree is shown; none: the process's windows
	bool m_RootSelected{ false };	// the root expanded and selected once (the first Load)
	WinSys::WindowInfo const* GetSelected() const;
	// the selected window's properties into the list below, and the buttons for it
	void ShowDetails();
	std::wstring GetText(WinSys::WindowInfo const& w, Column column) const;
	std::wstring GetProcessName(uint32_t pid) const;

	ProcessProperties const& m_Props;
	CCustomHorSplitterWindow m_Splitter;
	CTreeListViewCtrl m_Tree;
	CListViewCtrl m_Details;
	std::vector<WinSys::WindowInfo> m_Windows;	// in tree order; each item's data is its index here
	bool m_Loaded{ false };
	std::function<std::wstring(uint32_t)> m_ProcessName;
	bool m_GoToProcessEnabled{ false };
	uint32_t m_GoToProcessId{ 0 };
};
