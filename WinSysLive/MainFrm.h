// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "resource.h"
#include "WTLHelper.h"
#include <NativeCustomTabView.h>
#include <TabViewHelper.h>
#include "Interfaces.h"
#include "FindHandleDlg.h"
#include "TrayIcon.h"
#include "WindowFinder.h"

// the tabs have close buttons (the frame decides, TBVN_TABCLOSEBTN), but the fixed page (Processes) none; the image
// list is the frame's shared one
class CMainTabView : public CNativeCustomTabViewImpl<CMainTabView> {
public:
	DECLARE_WND_CLASS_EX(_T("WinSysLive_TabView"), 0, COLOR_APPWORKSPACE)

	CMainTabView() {
		m_bDestroyImageList = false;
	}

	// the base's, except for the fixed page: an empty rectangle, so its button can't be seen or clicked
	void CalcCloseButtonRect(int nItem, RECT& rcClose) {
		if (GetPageHWND(nItem) == FixedPage) {
			::SetRectEmpty(&rcClose);
			return;
		}
		CNativeCustomTabViewImpl::CalcCloseButtonRect(nItem, rcClose);
	}

	HWND FixedPage{ nullptr };
};

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public IMainFrame,
	public CMessageFilter,
	public CIdleHandler {
public:
	// a fixed class name, so a second instance started as Task Manager finds the first (ActivateMessage)
	static constexpr PCWSTR WindowClassName = L"WinSysLiveFrame";
	DECLARE_FRAME_WND_CLASS(WindowClassName, IDR_MAINFRAME)

	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnIdle();

	// posted by another instance to bring this one's window back (from the tray too)
	static inline UINT const ActivateMessage = ::RegisterWindowMessage(L"WinSysLive.Activate");
	// the notification icon's mouse messages
	static constexpr UINT WM_TRAYICON = WM_APP + 0x130;

	BEGIN_MSG_MAP(CMainFrame)
		COMMAND_TABVIEW_HANDLER(m_Tabs, 1)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_ENDSESSION, OnEndSession)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		MESSAGE_HANDLER(WM_TRAYICON, OnTrayIcon)
		MESSAGE_HANDLER(s_TaskbarCreated, OnTaskbarCreated)
		MESSAGE_HANDLER(ActivateMessage, OnActivateMessage)
		NOTIFY_CODE_HANDLER(TBVN_PAGEACTIVATED, OnPageActivated)
		NOTIFY_CODE_HANDLER(TBVN_TABCLOSEBTN, OnTabClose)
		NOTIFY_CODE_HANDLER(TTN_GETDISPINFOW, OnToolTipText)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		COMMAND_ID_HANDLER(ID_FILE_RUNASADMIN, OnRunAsAdmin)
		COMMAND_ID_HANDLER(ID_FILE_RUN, OnRun)
		COMMAND_ID_HANDLER(ID_FILE_RUNAS, OnRunAs)
		COMMAND_ID_HANDLER(ID_FILE_RUNLIMITED, OnRunLimited)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_VIEW_TOOLBAR, OnViewToolBar)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(ID_VIEW_PAUSE, OnPause)
		COMMAND_RANGE_HANDLER(ID_UPDATEINTERVAL_500, ID_UPDATEINTERVAL_5000, OnUpdateInterval)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSES, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_SERVICES, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_PERFORMANCE, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_USERS, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_NETWORK, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_KERNELMODULES, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_DRIVERS, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_IPC, OnViewTab)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnDarkMode)
		COMMAND_ID_HANDLER(ID_OPTIONS_FONT, OnFont)
		COMMAND_ID_HANDLER(ID_OPTIONS_DEFAULTFONT, OnDefaultFont)
		COMMAND_ID_HANDLER(ID_OPTIONS_PROCESSCOLORS, OnProcessColors)
		COMMAND_ID_HANDLER(ID_OPTIONS_RESOLVESYMBOLS, OnResolveSymbols)
		COMMAND_ID_HANDLER(ID_OPTIONS_SYMBOLSETTINGS, OnSymbolSettings)
		COMMAND_ID_HANDLER(ID_FIND_HANDLE, OnFindHandle)
		COMMAND_ID_HANDLER(ID_FILE_EXPORT, OnExportList)
		COMMAND_ID_HANDLER(ID_EDIT_COPYALL, OnCopyAllRows)
		COMMAND_ID_HANDLER(ID_OPTIONS_TRAYICON, OnTrayIconOption)
		COMMAND_ID_HANDLER(ID_OPTIONS_MINIMIZETOTRAY, OnMinimizeToTray)
		COMMAND_ID_HANDLER(ID_OPTIONS_ALWAYSONTOP, OnAlwaysOnTop)
		COMMAND_ID_HANDLER(ID_OPTIONS_SINGLEINSTANCE, OnSingleInstance)
		COMMAND_ID_HANDLER(ID_OPTIONS_RESOLVEADDRESSES, OnResolveAddresses)
		COMMAND_ID_HANDLER(ID_OPTIONS_REPLACETASKMANAGER, OnReplaceTaskManager)
		COMMAND_ID_HANDLER(ID_TRAY_SHOW, OnTrayShow)
		CHAIN_MSG_MAP(CAutoUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnEndSession(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPageActivated(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	// a tab's close button: 0 lets the tab view remove the page (and destroy the view), 1 keeps it
	LRESULT OnTabClose(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnToolTipText(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRun(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAs(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunLimited(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAsAdmin(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateInterval(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewTab(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFont(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDefaultFont(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProcessColors(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResolveSymbols(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolSettings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFindHandle(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnExportList(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyAllRows(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTrayIcon(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTaskbarCreated(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnActivateMessage(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTrayIconOption(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnMinimizeToTray(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSingleInstance(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResolveAddresses(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnReplaceTaskManager(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTrayShow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// Options > Replace Task Manager: the Debugger value of taskmgr.exe's Image File Execution Options names this exe
	static bool IsTaskManagerReplaced();

private:
	// IMainFrame
	HWND GetHwnd() const override;
	CUpdateUIBase& GetUI() override;
	void SetStatusText(int index, PCWSTR text) override;
	BOOL ShowContextMenu(HMENU hMenu, POINT const& pt, DWORD flags) override;
	HIMAGELIST GetImageList() const override;
	int GetIconIndex(UINT iconId) const override;
	int GetProcessIcon(ProcessInfoEx& process) override;
	ProcessManager& GetProcessManager() override;
	void GoToProcess(uint32_t pid) override;
	void GoToKernelModule(std::wstring const& path) override;
	void GoToDriver(std::wstring const& path) override;
	// switches to the tab, opening it if it's closed; its view
	IView* ActivateTab(TabType type);

	struct TabInfo {
		TabType Type;
		UINT CommandId;
		UINT Icon;
		PCWSTR Key;	// in the Tabs setting; must not change
	};
	// one entry per tab, in the View menu's order; each command also needs a COMMAND_ID_HANDLER for OnViewTab. Only
	// Processes is always open; the others are opened and closed (the Tabs setting has the open ones, in tab order)
	static constexpr TabInfo s_Tabs[] = {
		{ TabType::Processes, ID_VIEW_PROCESSES, IDI_PROCESSES, L"Processes" },
		{ TabType::Services, ID_VIEW_SERVICES, IDI_SERVICES, L"Services" },
		{ TabType::Performance, ID_VIEW_PERFORMANCE, IDI_PERFORMANCE, L"Performance" },
		{ TabType::Users, ID_VIEW_USERS, IDI_USERS, L"Users" },
		{ TabType::Network, ID_VIEW_NETWORK, IDI_NETWORK, L"Network" },
		{ TabType::KernelModules, ID_VIEW_KERNELMODULES, IDI_ATOM, L"KernelModules" },
		{ TabType::Drivers, ID_VIEW_DRIVERS, IDI_DRIVER, L"Drivers" },
		{ TabType::Ipc, ID_VIEW_IPC, IDI_ALPC, L"Ipc" },
	};
	static TabInfo const* FindTab(TabType type);

	// an open tab; the tab view's pages are found by their windows, as dragging reorders them
	struct OpenView {
		TabType Type;
		IView* View;
	};
	OpenView const* GetPageView(int page) const;
	// the page with the tab type, or -1 if it's closed
	int FindPage(TabType type) const;
	// creates the view and adds its tab (activated); its view
	IView* OpenTab(TabType type);
	// removes the tab, which destroys its view (not Processes)
	void CloseTab(int page);
	// before a page is removed: the frame stops talking to its view
	void ForgetView(int page);
	// the View menu's and the toolbar's check marks: the open tabs
	void UpdateTabCommands();
	// the open tabs, in tab order, into the Tabs setting
	void SaveTabs();

	struct IntervalInfo {
		UINT CommandId;
		int Interval;
	};
	static constexpr IntervalInfo s_Intervals[] = {
		{ ID_UPDATEINTERVAL_500, 500 },
		{ ID_UPDATEINTERVAL_1000, 1000 },
		{ ID_UPDATEINTERVAL_2000, 2000 },
		{ ID_UPDATEINTERVAL_5000, 5000 },
	};

	std::pair<IView*, CMessageMap*> CreateView(TabType type);	// ViewFactory.cpp
	void InitImageList();
	// the frame's icons into the image list, in the size for its DPI
	void AddImages();
	// the image list for a new DPI, the same indices (process icons too)
	void UpdateImageList();
	void InitMenu(HMENU hMenu);
	void RemoveRunAsAdminMenuItem();
	void InitStatusBar();
	void InitToolBar(bool elevated);
	// the images that follow the buttons' own: run (for the pause button) and the shield (Run as Administrator)
	void AddToolBarImages();
	// the toolbar's images and its band for a new DPI
	void UpdateToolBar();
	void CreateTabs();
	void SetDarkMode(bool dark);
	// creates the font from the Font setting (or the default) and hands it to the views
	void ApplyFont();
	void Refresh();
	void UpdateStatusBar();
	void SetUpdateInterval(int interval);
	void SetPaused(bool paused);
	void SaveSettings();
	// the tray icon as the TrayIcon setting has it
	void ShowTrayIcon(bool show);
	// View > Toolbar and Status Bar, as their settings have them
	void ShowToolBar(bool show);
	void ShowStatusBar(bool show);
	// the window back from the tray (or minimized), in front
	void RestoreWindow();
	void ShowTrayMenu();
	// the window finder: the window under the cursor in the status bar while dragging, the process when dropped
	void OnFinderHover(HWND hWnd);
	void OnWindowFound(HWND hWnd);
	// the list Export List and Copy All Rows act on: a list view of the active view's with the focus, else the view's
	// main list; nullptr if there's none
	HWND GetExportList();
	std::vector<std::vector<std::wstring>> GetExportRows(HWND hList);

	enum { TimerId = 1 };

	CMainTabView m_Tabs;
	CMultiPaneStatusBarCtrl m_StatusBar;
	CToolBarCtrl m_ToolBar;
	int m_PauseImage{ -1 }, m_RunImage{ -1 };	// the pause button shows the action it performs
	int m_ShieldImage{ -1 };
	CImageListManaged m_Images;
	std::unordered_map<UINT, int> m_IconIndices;
	std::unordered_map<std::wstring, int> m_ProcessIcons;	// image path (lowercase) to image list index
	std::vector<OpenView> m_Views;	// the open tabs' views, in no particular order
	IView* m_ActiveView{ nullptr };
	bool m_Started{ false };	// OnCreate is done: a view opened from now on gets the theme and font itself
	std::wstring m_TipText;	// a toolbar tooltip from the active view (OnToolTipText)
	std::unique_ptr<CFindHandleDlg> m_FindDlg;	// modeless; created again once closed
	CFont m_Font;	// the views' (see ApplyFont)
	CTrayIcon m_Tray;
	CWindowFinder m_WindowFinder;	// subclasses the toolbar
	std::optional<std::wstring> m_FinderSavedStatus;	// the status text before the drag, restored after it
	static inline UINT const s_TaskbarCreated = ::RegisterWindowMessage(L"TaskbarCreated");
	ProcessManager m_ProcessManager;
	bool m_Paused{ false };
	bool m_CpuValid{ false };
};
