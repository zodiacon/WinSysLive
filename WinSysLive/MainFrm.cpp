// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"

#include "aboutdlg.h"
#include "MainFrm.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include "WTLHelper.h"
#include <IconHelper.h>
#include <ToolbarHelper.h>
#include <WinSys/SecurityHelper.h>

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	// Space pauses updates, but not while typing in an edit control (such as a label being edited)
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_SPACE) {
		WCHAR className[16];
		if (::GetClassName(::GetFocus(), className, _countof(className)) && ::_wcsicmp(className, WC_EDIT) == 0)
			return FALSE;
	}

	if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	return m_Tabs.PreTranslateMessage(pMsg);
}

BOOL CMainFrame::OnIdle() {
	UIUpdateToolBar();
	return FALSE;
}

LRESULT CMainFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	bool elevated = WinSys::SecurityHelper::IsRunningElevated();

	InitImageList();
	InitToolBar(elevated);
	InitStatusBar();

	m_hWndClient = m_Tabs.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE);
	m_Tabs.SetImageList(m_Images);

	if (elevated) {
		CString title;
		GetWindowText(title);
		SetWindowText(title + L" (Administrator)");
		RemoveRunAsAdminMenuItem();
	}

	UIAddMenu(GetMenu());
	UISetCheck(ID_VIEW_STATUS_BAR, 1);
	UISetCheck(ID_VIEW_TOOLBAR, 1);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	// initial snapshot, so views have data when created; CPU usage is only valid from the next update
	m_ProcessManager.Update();
	UpdateStatusBar();

	CreateTabs();

	auto& settings = AppSettings::Get();
	UISetCheck(ID_OPTIONS_DARKMODE, settings.DarkMode());
	SetDarkMode(settings.DarkMode());

	SetUpdateInterval(settings.UpdateInterval());

	return 0;
}

void CMainFrame::InitImageList() {
	static const UINT icons[] = {
		IDI_PROCESSES, IDI_SERVICES, IDI_PERFORMANCE, IDI_USERS, IDI_RUN, IDI_KILL,
		IDI_HANDLES, IDI_MODULES, IDI_THREADS,
		// object types, for the Handles tab
		IDI_OBJECT, IDI_GRAPHICS, IDI_FILE, IDI_DESKTOP, IDI_DEVICE, IDI_EVENT, IDI_KEY, IDI_ALPC, IDI_MUTEX, IDI_SEMAPHORE, IDI_TIMER,
		IDI_DIRECTX, IDI_ETW, IDI_DIRECTORY, IDI_PORT, IDI_SECTION, IDI_SESSION, IDI_TOKEN, IDI_WINSTA,
		// thread states, for the Threads tab
		IDI_PAUSE, IDI_CONTINUE, IDI_MEMORY,
	};

	m_Images.Create(16, 16, ILC_COLOR32 | ILC_MASK, _countof(icons) + 1, 16);
	for (auto id : icons)
		m_IconIndices[id] = m_Images.AddIcon(IconHelper::LoadCached(id, 16));
	// processes without an icon of their own get the system's default application icon
	m_IconIndices[IDI_PROCESS] = m_Images.AddIcon(::LoadIcon(nullptr, IDI_APPLICATION));
}

void CMainFrame::InitToolBar(bool elevated) {
	// commands that belong to a view (process tree, kill) are routed to the active view; views enable them
	ToolBarButtonInfo const buttons[] = {
		{ ID_VIEW_PAUSE, IDI_PAUSE },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ 0 },
		{ ID_VIEW_PROCESSTREE, IDI_TREE, BTNS_CHECK },
		{ ID_VIEW_LOWERPANE, IDI_LOWERPANE, BTNS_CHECK },
		{ ID_PROCESS_KILL, IDI_PROCESS_KILL },
	};
	const int iconSize = 24;
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_ToolBar = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), iconSize);

	// the pause button switches to a "run" image while paused
	CImageList images = m_ToolBar.GetImageList();
	m_PauseImage = 0;	// first button's image
	m_RunImage = images.AddIcon(IconHelper::LoadCached(IDI_RUN, iconSize));

	if (!elevated) {
		static const HICON hShield = IconHelper::GetStockIcon(SIID_SHIELD, true);
		m_ToolBar.AddSeparator(0);
		m_ToolBar.AddButton(ID_FILE_RUNASADMIN, BTNS_BUTTON, TBSTATE_ENABLED, images.AddIcon(hShield), nullptr, 0);
	}

	AddSimpleReBarBand(m_ToolBar);
	UIAddToolBar(m_ToolBar);
}

void CMainFrame::InitMenu(HMENU hMenu) {
	// GetShieldIcon creates a new icon on every call, and InitMenu caches its bitmaps by icon handle
	static const HICON hShield = IconHelper::GetShieldIcon();

	std::vector<MenuItemData> commands{
		{ ID_FILE_RUNASADMIN, 0, hShield },
		{ ID_VIEW_PAUSE, IDI_PAUSE },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
	};
	for (auto& tab : s_Tabs)
		commands.push_back({ (int)tab.CommandId, (int)tab.Icon });

	WTLHelper::InitMenu(hMenu, commands.data(), (int)commands.size());
}

void CMainFrame::RemoveRunAsAdminMenuItem() {
	CMenuHandle fileMenu = CMenuHandle(GetMenu()).GetSubMenu(0);
	for (int i = 0; i < fileMenu.GetMenuItemCount(); i++) {
		if (fileMenu.GetMenuItemID(i) == ID_FILE_RUNASADMIN) {
			fileMenu.DeleteMenu(i, MF_BYPOSITION);
			// and the separator that followed it
			if (i < fileMenu.GetMenuItemCount() && (fileMenu.GetMenuState(i, MF_BYPOSITION) & MF_SEPARATOR))
				fileMenu.DeleteMenu(i, MF_BYPOSITION);
			break;
		}
	}
}

void CMainFrame::InitStatusBar() {
	CreateSimpleStatusBar();
	m_StatusBar.SubclassWindow(m_hWndStatusBar);

	// pane widths come from the panes' string resources
	int panes[] = { ID_DEFAULT_PANE, ID_PANE_PROCESSES, ID_PANE_THREADS, ID_PANE_CPU, ID_PANE_ELEVATION };
	m_StatusBar.SetPanes(panes, _countof(panes), false);
	m_StatusBar.SetPaneText(ID_PANE_CPU, L"CPU: --");
	m_StatusBar.SetPaneText(ID_PANE_ELEVATION, WinSys::SecurityHelper::IsRunningElevated() ? L"Administrator" : L"Standard User");
}

void CMainFrame::CreateTabs() {
	for (auto& tab : s_Tabs) {
		auto [view, map] = CreateView(tab.Type);
		ATLASSERT(view);
		m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), GetIconIndex(tab.Icon), map);
		m_Views.push_back(view);
	}
	int active = AppSettings::Get().ActiveTab();
	m_Tabs.SetActivePage(active >= 0 && active < (int)m_Views.size() ? active : 0);
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	KillTimer(TimerId);
	SaveSettings();

	// the views delete themselves as their windows are destroyed, and the tab view activates pages while
	// removing them (TBVN_PAGEACTIVATED), so stop talking to the views now
	m_Views.clear();
	m_ActivePage = -1;

	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 1;
}

LRESULT CMainFrame::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != TimerId) {
		bHandled = FALSE;
		return 0;
	}
	Refresh();
	return 0;
}

void CMainFrame::Refresh() {
	m_ProcessManager.Update();
	for (int i = 0; i < (int)m_Views.size(); i++)
		m_Views[i]->OnRefresh(i == m_ActivePage);
	UpdateStatusBar();
}

void CMainFrame::UpdateStatusBar() {
	m_StatusBar.SetPaneText(ID_PANE_PROCESSES, std::format(L"Processes: {}", m_ProcessManager.GetProcessCount()).c_str());
	int64_t threads = 0;
	for (auto& p : m_ProcessManager.GetProcesses())
		threads += p->NativeInfo->NumberOfThreads;
	m_StatusBar.SetPaneText(ID_PANE_THREADS, (L"Threads: " + FormatHelper::FormatNumber(threads)).c_str());

	// the Idle process's CPU is computed relative to the previous update, so there's nothing to show after the first one
	if (!m_CpuValid) {
		m_CpuValid = true;
		return;
	}
	// ProcessInfo::CPU is in units of 1/10000 percent
	if (auto idle = m_ProcessManager.GetProcessById(0); idle) {
		auto cpu = std::clamp(100.0 - idle->CPU / 10000.0, 0.0, 100.0);
		m_StatusBar.SetPaneText(ID_PANE_CPU, std::format(L"CPU: {:.1f}%", cpu).c_str());
	}
}

void CMainFrame::SetUpdateInterval(int interval) {
	auto it = std::ranges::find(s_Intervals, interval, &IntervalInfo::Interval);
	if (it == std::end(s_Intervals))
		it = std::ranges::find(s_Intervals, 1000, &IntervalInfo::Interval);

	AppSettings::Get().UpdateInterval(it->Interval);
	UISetRadioMenuItem(it->CommandId, ID_UPDATEINTERVAL_500, ID_UPDATEINTERVAL_5000);
	if (!m_Paused)
		SetTimer(TimerId, it->Interval);
}

void CMainFrame::SetPaused(bool paused) {
	m_Paused = paused;
	UISetCheck(ID_VIEW_PAUSE, paused);
	m_ToolBar.ChangeBitmap(ID_VIEW_PAUSE, paused ? m_RunImage : m_PauseImage);
	if (paused)
		KillTimer(TimerId);
	else
		SetTimer(TimerId, AppSettings::Get().UpdateInterval());
	m_StatusBar.SetPaneText(ID_DEFAULT_PANE, paused ? L"Updates paused" : L"Ready");
}

void CMainFrame::SaveSettings() {
	auto& settings = AppSettings::Get();
	settings.SaveWindowPosition(m_hWnd, AppSettings::MainWindowPlacement);
	settings.ActiveTab(m_ActivePage);
	settings.Save();
}

LRESULT CMainFrame::OnPageActivated(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	int page = static_cast<int>(hdr->idFrom);
	if (m_ActivePage >= 0 && m_ActivePage < (int)m_Views.size())
		m_Views[m_ActivePage]->OnActivate(false);
	m_ActivePage = page;
	if (page >= 0 && page < (int)m_Views.size())
		m_Views[page]->OnActivate(true);

	for (int i = 0; i < _countof(s_Tabs); i++)
		UISetCheck(s_Tabs[i].CommandId, i == page);

	return 0;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnRunAsAdmin(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// save first, so the elevated instance starts with the current settings
	SaveSettings();
	if (WinSys::SecurityHelper::RunElevated())
		PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	BOOL bVisible = !::IsWindowVisible(m_hWndStatusBar);
	::ShowWindow(m_hWndStatusBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// m_hWndToolBar is the rebar hosting the toolbar
	BOOL bVisible = !::IsWindowVisible(m_hWndToolBar);
	::ShowWindow(m_hWndToolBar, bVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_TOOLBAR, bVisible);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Refresh();
	return 0;
}

LRESULT CMainFrame::OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SetPaused(!m_Paused);
	return 0;
}

LRESULT CMainFrame::OnUpdateInterval(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto it = std::ranges::find(s_Intervals, id, &IntervalInfo::CommandId);
	ATLASSERT(it != std::end(s_Intervals));
	SetUpdateInterval(it->Interval);
	return 0;
}

LRESULT CMainFrame::OnViewTab(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	for (int i = 0; i < _countof(s_Tabs); i++)
		if (s_Tabs[i].CommandId == id) {
			m_Tabs.SetActivePage(i);
			break;
		}
	return 0;
}

LRESULT CMainFrame::OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CAboutDlg dlg;
	dlg.DoModal();
	return 0;
}

LRESULT CMainFrame::OnDarkMode(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	bool dark = !settings.DarkMode();
	settings.DarkMode(dark);
	settings.Save();
	SetDarkMode(dark);
	UISetCheck(id, dark);

	return 0;
}

void CMainFrame::SetDarkMode(bool dark) {
	WTLHelper::SwitchToMode(dark ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd);

	// menu icon bitmaps are drawn on the current menu background, so rebuild them for the new mode
	InitMenu(GetMenu());

	CStatusBarCtrl(m_hWndStatusBar).SetBkColor(dark ? DarkMode::getBackgroundColor() : CLR_DEFAULT);

	for (auto view : m_Views)
		view->OnThemeChanged(dark);

	::EnumThreadWindows(::GetCurrentThreadId(), [](auto h, auto) {
		::RedrawWindow(h, nullptr, nullptr, RDW_ERASENOW | RDW_INTERNALPAINT | RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		return TRUE;
		}, 0);

	DrawMenuBar();
}

HWND CMainFrame::GetHwnd() const {
	return m_hWnd;
}

CUpdateUIBase& CMainFrame::GetUI() {
	return *this;
}

void CMainFrame::SetStatusText(int index, PCWSTR text) {
	m_StatusBar.SetText(index, text);
}

BOOL CMainFrame::ShowContextMenu(HMENU hMenu, POINT const& pt, DWORD flags) {
	// commands land in the frame and are routed to the active view
	return ::TrackPopupMenuEx(hMenu, flags, pt.x, pt.y, m_hWnd, nullptr);
}

HIMAGELIST CMainFrame::GetImageList() const {
	return m_Images;
}

int CMainFrame::GetIconIndex(UINT iconId) const {
	auto it = m_IconIndices.find(iconId);
	return it == m_IconIndices.end() ? -1 : it->second;
}

int CMainFrame::GetProcessIcon(ProcessInfoEx& process) {
	auto defaultIcon = GetIconIndex(IDI_PROCESS);
	auto& path = process.GetImagePath();
	if (path.empty())
		return defaultIcon;

	CString key(path.c_str());
	key.MakeLower();
	if (auto it = m_ProcessIcons.find((PCWSTR)key); it != m_ProcessIcons.end())
		return it->second;

	HICON hIcon = nullptr;
	::ExtractIconEx(path.c_str(), 0, nullptr, &hIcon, 1);
	int index = defaultIcon;
	if (hIcon) {
		index = m_Images.AddIcon(hIcon);
		::DestroyIcon(hIcon);
	}
	m_ProcessIcons.insert({ (PCWSTR)key, index });
	return index;
}

ProcessManager& CMainFrame::GetProcessManager() {
	return m_ProcessManager;
}

void CMainFrame::GoToProcess(uint32_t pid) {
	for (int i = 0; i < _countof(s_Tabs); i++) {
		if (s_Tabs[i].Type == TabType::Processes) {
			m_Tabs.SetActivePage(i);
			m_Views[i]->ShowProcess(pid);
			break;
		}
	}
}
