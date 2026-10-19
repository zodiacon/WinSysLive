// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "resource.h"
#include "WTLHelper.h"
#include <NativeCustomTabView.h>
#include <TabViewHelper.h>
#include "Interfaces.h"

// tabs are fixed, so no close button; the image list is the frame's shared one
class CMainTabView : public CNativeCustomTabViewImpl<CMainTabView> {
public:
	DECLARE_WND_CLASS_EX(_T("WinSysLive_TabView"), 0, COLOR_APPWORKSPACE)

	CMainTabView() {
		m_bTabCloseButton = false;
		m_bDestroyImageList = false;
	}
};

class CMainFrame :
	public CFrameWindowImpl<CMainFrame>,
	public CAutoUpdateUI<CMainFrame>,
	public IMainFrame,
	public CMessageFilter,
	public CIdleHandler {
public:
	DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME)

	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnIdle();

	BEGIN_MSG_MAP(CMainFrame)
		COMMAND_TABVIEW_HANDLER(m_Tabs, 1)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		NOTIFY_CODE_HANDLER(TBVN_PAGEACTIVATED, OnPageActivated)
		COMMAND_ID_HANDLER(ID_APP_EXIT, OnFileExit)
		COMMAND_ID_HANDLER(ID_FILE_RUNASADMIN, OnRunAsAdmin)
		COMMAND_ID_HANDLER(ID_VIEW_STATUS_BAR, OnViewStatusBar)
		COMMAND_ID_HANDLER(ID_VIEW_TOOLBAR, OnViewToolBar)
		COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(ID_VIEW_PAUSE, OnPause)
		COMMAND_RANGE_HANDLER(ID_UPDATEINTERVAL_500, ID_UPDATEINTERVAL_5000, OnUpdateInterval)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSES, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_SERVICES, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_PERFORMANCE, OnViewTab)
		COMMAND_ID_HANDLER(ID_VIEW_USERS, OnViewTab)
		COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAppAbout)
		COMMAND_ID_HANDLER(ID_OPTIONS_DARKMODE, OnDarkMode)
		CHAIN_MSG_MAP(CAutoUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPageActivated(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRunAsAdmin(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateInterval(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnViewTab(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDarkMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

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

	struct TabInfo {
		TabType Type;
		UINT CommandId;
		UINT Icon;
	};
	// one entry per tab, in tab order; each command also needs a COMMAND_ID_HANDLER for OnViewTab
	static constexpr TabInfo s_Tabs[] = {
		{ TabType::Processes, ID_VIEW_PROCESSES, IDI_PROCESSES },
		{ TabType::Services, ID_VIEW_SERVICES, IDI_SERVICES },
		{ TabType::Performance, ID_VIEW_PERFORMANCE, IDI_PERFORMANCE },
		{ TabType::Users, ID_VIEW_USERS, IDI_USERS },
	};

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
	void InitMenu(HMENU hMenu);
	void RemoveRunAsAdminMenuItem();
	void InitStatusBar();
	void InitToolBar(bool elevated);
	void CreateTabs();
	void SetDarkMode(bool dark);
	void Refresh();
	void UpdateStatusBar();
	void SetUpdateInterval(int interval);
	void SetPaused(bool paused);
	void SaveSettings();

	enum { TimerId = 1 };

	CMainTabView m_Tabs;
	CMultiPaneStatusBarCtrl m_StatusBar;
	CToolBarCtrl m_ToolBar;
	int m_PauseImage{ -1 }, m_RunImage{ -1 };	// the pause button shows the action it performs
	CImageListManaged m_Images;
	std::unordered_map<UINT, int> m_IconIndices;
	std::unordered_map<std::wstring, int> m_ProcessIcons;	// image path (lowercase) to image list index
	std::vector<IView*> m_Views;	// index == tab index
	ProcessManager m_ProcessManager;
	int m_ActivePage{ -1 };
	bool m_Paused{ false };
	bool m_CpuValid{ false };
};
