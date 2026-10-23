// MainFrm.cpp : implmentation of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"

#include "aboutdlg.h"
#include "MainFrm.h"
#include "AppSettings.h"
#include "ProcessColorsDlg.h"
#include "SymbolSettingsDlg.h"
#include "FirmwareDlg.h"
#include "FormatHelper.h"
#include "ListExport.h"
#include "RunDlg.h"
#include "AddressResolver.h"
#include <ClipboardHelper.h>
#include "WTLHelper.h"
#include <IconHelper.h>
#include <ToolbarHelper.h>
#include <WinSys/SecurityHelper.h>
#include <WinSys/Helpers.h>
#include <DpiHelper.h>

namespace {
	// the frame's images, in their image list order (the process icons follow them)
	const UINT s_Icons[] = {
		IDI_PROCESSES, IDI_SERVICES, IDI_PERFORMANCE, IDI_USERS, IDI_NETWORK, IDI_RUN, IDI_KILL,
		IDI_HANDLES, IDI_MODULES, IDI_THREADS,
		// object types, for the Handles tab
		IDI_OBJECT, IDI_GRAPHICS, IDI_FILE, IDI_DESKTOP, IDI_DEVICE, IDI_EVENT, IDI_KEY, IDI_ALPC, IDI_MUTEX, IDI_SEMAPHORE, IDI_TIMER,
		IDI_DIRECTX, IDI_ETW, IDI_DIRECTORY, IDI_PORT, IDI_SECTION, IDI_SESSION, IDI_TOKEN, IDI_WINSTA, IDI_JOB,
		// thread states, for the Threads tab
		IDI_PAUSE, IDI_CONTINUE, IDI_MEMORY,
		// mapping types, for the Modules tab
		IDI_MODULE_IMAGE, IDI_MODULE_DATA,
		// the Kernel Modules and Drivers tabs; Kernel Modules' drivers
		IDI_ATOM, IDI_DRIVER, IDI_SYSTEM_DRIVER,
		// the Zombie Processes tab
		IDI_PROCESS_KILL,
	};

	// commands that belong to a view (process tree, kill) are routed to the active view; views enable them
	const ToolBarButtonInfo s_ToolBarButtons[] = {
		{ ID_VIEW_PAUSE, IDI_PAUSE },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		{ 0 },
		{ ID_VIEW_PROCESSTREE, IDI_TREE, BTNS_CHECK },
		{ ID_VIEW_LOWERPANE, IDI_LOWERPANE, BTNS_CHECK },
		{ ID_KILL, IDI_DELETE },	// the process, thread or handle selected in the list with the focus
		{ 0 },
		{ ID_FIND_WINDOW, IDI_TARGET },	// dragged onto a window, not clicked (CWindowFinder)
		{ 0 },
		// the tabs, as their View menu commands (checked: open); in CMainFrame::s_Tabs order
		{ ID_VIEW_PROCESSES, IDI_PROCESSES, BTNS_CHECK },
		{ ID_VIEW_SERVICES, IDI_SERVICES, BTNS_CHECK },
		{ ID_VIEW_PERFORMANCE, IDI_PERFORMANCE, BTNS_CHECK },
		{ ID_VIEW_USERS, IDI_USERS, BTNS_CHECK },
		{ ID_VIEW_NETWORK, IDI_NETWORK, BTNS_CHECK },
		{ ID_VIEW_KERNELMODULES, IDI_ATOM, BTNS_CHECK },
		{ ID_VIEW_DRIVERS, IDI_DRIVER, BTNS_CHECK },
		{ ID_VIEW_IPC, IDI_ALPC, BTNS_CHECK },
		{ ID_VIEW_ZOMBIES, IDI_PROCESS_KILL, BTNS_CHECK },
		{ ID_VIEW_OBJECTTYPES, IDI_OBJECT, BTNS_CHECK },
	};
	const int ToolBarIconSize = 24;	// at 96 DPI

	// pane widths come from the panes' string resources
	int s_Panes[] = { ID_DEFAULT_PANE, ID_PANE_PROCESSES, ID_PANE_THREADS, ID_PANE_CPU, ID_PANE_ELEVATION };

	// an executable's icon in a size (ExtractIconEx's small icons are of the system DPI)
	HICON ExtractFileIcon(std::wstring const& path, int size) {
		HICON hIcon = nullptr;
		return SUCCEEDED(::SHDefExtractIcon(path.c_str(), 0, 0, &hIcon, nullptr, size)) ? hIcon : nullptr;
	}

	HICON LoadDefaultProcessIcon(int size) {
		HICON hIcon = nullptr;
		return SUCCEEDED(::LoadIconWithScaleDown(nullptr, IDI_APPLICATION, size, size, &hIcon)) ? hIcon : nullptr;
	}
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg) {
	// keys that edit text belong to an edit control with the focus (a filter box, a label being edited), not to the
	// accelerators: Space (pause), Delete (kill), and the clipboard and undo keys (Ctrl+C is Copy)
	if (pMsg->message == WM_KEYDOWN) {
		auto key = pMsg->wParam;
		bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
		bool editing = (!ctrl && (key == VK_SPACE || key == VK_DELETE))
			|| (ctrl && (key == 'C' || key == 'V' || key == 'X' || key == 'A' || key == 'Z' || key == 'Y'));
		WCHAR className[16];
		if (editing && ::GetClassName(::GetFocus(), className, _countof(className)) && ::_wcsicmp(className, WC_EDIT) == 0)
			return FALSE;
	}

	// the modeless Find Handle or DLL dialog's keys (Tab, Enter, Escape) before the frame's accelerators
	if (m_FindDlg && m_FindDlg->IsWindow() && m_FindDlg->IsDialogMessage(pMsg))
		return TRUE;

	// Escape cancels dragging the window finder (the keys go to the focus, not the toolbar holding the capture)
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE && m_WindowFinder.IsDragging()) {
		m_WindowFinder.Cancel();
		return TRUE;
	}

	if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
		return TRUE;

	return m_Tabs.PreTranslateMessage(pMsg);
}

LRESULT CMainFrame::OnToolTipText(int idCtrl, LPNMHDR hdr, BOOL& bHandled) {
	// the toolbar's commands whose tooltips the active view words (Kill: what it acts on); the rest are the string
	// resources' (CFrameWindowImpl)
	auto di = reinterpret_cast<NMTTDISPINFOW*>(hdr);
	if ((di->uFlags & TTF_IDISHWND) || idCtrl != ID_KILL) {
		bHandled = FALSE;
		return 0;
	}
	if (m_ActiveView)
		m_TipText = m_ActiveView->GetCommandTip(idCtrl);
	if (m_TipText.empty())
		m_TipText = L"Kill (Del)";
	// never with TTF_DI_SETITEM (as CFrameWindowImpl's handler sets it), so the tooltip asks every time: the text a
	// view gives depends on its state
	di->lpszText = m_TipText.data();
	return 0;
}

BOOL CMainFrame::OnIdle() {
	// the active view's commands may depend on the focus, which changes without telling the views
	if (m_ActiveView)
		m_ActiveView->OnFrameIdle();
	bool list = GetExportList() != nullptr;
	UIEnable(ID_FILE_EXPORT, list);
	UIEnable(ID_EDIT_COPYALL, list);
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
	ShowToolBar(settings.ToolBar());
	ShowStatusBar(settings.StatusBar());
	UISetCheck(ID_OPTIONS_DARKMODE, settings.DarkMode());
	SetDarkMode(settings.DarkMode());
	// without a font of their own, the views keep their controls' default fonts
	if (settings.Font().lfFaceName[0])
		ApplyFont();
	UIEnable(ID_OPTIONS_DEFAULTFONT, settings.Font().lfFaceName[0] != 0);
	SymbolEngine::Get().SetSearchPath(BuildSymbolSearchPath(CSymbolSettingsDlg::LoadOptions()));
	SymbolEngine::Get().Enable(settings.ResolveSymbols());
	UISetCheck(ID_OPTIONS_RESOLVESYMBOLS, settings.ResolveSymbols());
	AddressResolver::Get().Enable(settings.ResolveAddresses());
	UISetCheck(ID_OPTIONS_RESOLVEADDRESSES, settings.ResolveAddresses());

	SetUpdateInterval(settings.UpdateInterval());

	// an elevated instance still gets these from the shell and a second, non-elevated instance (UIPI)
	::ChangeWindowMessageFilterEx(m_hWnd, s_TaskbarCreated, MSGFLT_ALLOW, nullptr);
	::ChangeWindowMessageFilterEx(m_hWnd, ActivateMessage, MSGFLT_ALLOW, nullptr);
	ShowTrayIcon(settings.TrayIcon());
	UISetCheck(ID_OPTIONS_MINIMIZETOTRAY, settings.MinimizeToTray());
	UIEnable(ID_OPTIONS_MINIMIZETOTRAY, settings.TrayIcon());
	if (settings.AlwaysOnTop())
		SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	UISetCheck(ID_OPTIONS_ALWAYSONTOP, settings.AlwaysOnTop());
	UISetCheck(ID_OPTIONS_SINGLEINSTANCE, settings.SingleInstance());
	// changing it writes to HKLM
	UISetCheck(ID_OPTIONS_REPLACETASKMANAGER, IsTaskManagerReplaced());
	UIEnable(ID_OPTIONS_REPLACETASKMANAGER, elevated);

	m_Started = true;
	return 0;
}

void CMainFrame::ShowTrayIcon(bool show) {
	if (show && !m_Tray.IsShown()) {
		m_Tray.SetTip(L"WinSysLive");
		m_Tray.Add(m_hWnd, WM_TRAYICON);
	}
	else if (!show) {
		m_Tray.Remove();
		// a window hidden in the tray would have no way back
		if (!IsWindowVisible())
			RestoreWindow();
	}
	UISetCheck(ID_OPTIONS_TRAYICON, show);
}

void CMainFrame::RestoreWindow() {
	if (!IsWindowVisible())
		ShowWindow(SW_SHOW);
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	SetForegroundWindow(m_hWnd);
}

LRESULT CMainFrame::OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	// minimized to the tray: the window goes from the taskbar, the icon brings it back
	if (wParam == SIZE_MINIMIZED && m_Tray.IsShown() && AppSettings::Get().MinimizeToTray())
		ShowWindow(SW_HIDE);
	bHandled = FALSE;	// the frame's layout
	return 0;
}

LRESULT CMainFrame::OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	// moved to a monitor with another DPI (or its DPI changed): the size Windows suggests for the new DPI. The child
	// windows get WM_DPICHANGED_AFTERPARENT after this (the lists scale their columns then, the filter boxes resize)
	auto rc = reinterpret_cast<RECT const*>(lParam);
	SetWindowPos(nullptr, rc, SWP_NOZORDER | SWP_NOACTIVATE);

	UpdateImageList();
	m_Tabs.SetImageList(m_Images);
	UpdateToolBar();
	InitMenu(GetMenu());
	// the pane widths for the status bar's font at the new DPI
	m_StatusBar.SetPanes(s_Panes, _countof(s_Panes), false);
	UpdateStatusBar();
	// a font of the user's choice, at the new DPI (the views' default fonts follow the DPI by themselves)
	if (!m_Font.IsNull())
		ApplyFont();
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	for (auto& v : m_Views)
		v.View->OnDpiChanged(dpi);
	UpdateLayout();
	return 0;
}

LRESULT CMainFrame::OnTrayIcon(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	switch ((UINT)lParam) {
		case WM_LBUTTONUP:
			// the click made the taskbar the foreground window, so there's no telling whether the window was in front:
			// it comes back, or to the front (the menu's Hide sends it away)
			RestoreWindow();
			break;

		case WM_RBUTTONUP:
			ShowTrayMenu();
			break;
	}
	return 0;
}

void CMainFrame::ShowTrayMenu() {
	CMenu menu;
	menu.CreatePopupMenu();
	bool shown = IsWindowVisible() && !IsIconic();
	menu.AppendMenu(MF_STRING, ID_TRAY_SHOW, shown ? L"&Hide WinSysLive" : L"&Show WinSysLive");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_Paused ? MF_CHECKED : 0), ID_VIEW_PAUSE, L"&Pause Updates");
	menu.AppendMenu(MF_STRING | (AppSettings::Get().AlwaysOnTop() ? MF_CHECKED : 0), ID_OPTIONS_ALWAYSONTOP, L"Always on &Top");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_APP_EXIT, L"E&xit");
	menu.SetMenuDefaultItem(ID_TRAY_SHOW);

	// so the menu closes when the user clicks elsewhere
	SetForegroundWindow(m_hWnd);
	POINT pt;
	::GetCursorPos(&pt);
	ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	PostMessage(WM_NULL);
}

LRESULT CMainFrame::OnTrayShow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (IsWindowVisible() && !IsIconic())
		ShowWindow(SW_MINIMIZE);	// to the tray, with Minimize to Tray
	else
		RestoreWindow();
	return 0;
}

LRESULT CMainFrame::OnTaskbarCreated(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// Explorer restarted, without the icons
	if (AppSettings::Get().TrayIcon()) {
		m_Tray.Remove();
		ShowTrayIcon(true);
	}
	return 0;
}

LRESULT CMainFrame::OnActivateMessage(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	RestoreWindow();
	return 0;
}

LRESULT CMainFrame::OnTrayIconOption(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	bool show = !settings.TrayIcon();
	settings.TrayIcon(show);
	settings.Save();
	ShowTrayIcon(show);
	UIEnable(ID_OPTIONS_MINIMIZETOTRAY, show);
	return 0;
}

LRESULT CMainFrame::OnMinimizeToTray(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	settings.MinimizeToTray(!settings.MinimizeToTray());
	settings.Save();
	UISetCheck(ID_OPTIONS_MINIMIZETOTRAY, settings.MinimizeToTray());
	return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	bool top = !settings.AlwaysOnTop();
	settings.AlwaysOnTop(top);
	settings.Save();
	SetWindowPos(top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	UISetCheck(ID_OPTIONS_ALWAYSONTOP, top);
	return 0;
}

LRESULT CMainFrame::OnSingleInstance(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// read by the next instance started (ActivateRunningInstance)
	auto& settings = AppSettings::Get();
	settings.SingleInstance(!settings.SingleInstance());
	settings.Save();
	UISetCheck(ID_OPTIONS_SINGLEINSTANCE, settings.SingleInstance());
	return 0;
}

LRESULT CMainFrame::OnResolveAddresses(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	bool resolve = !settings.ResolveAddresses();
	settings.ResolveAddresses(resolve);
	settings.Save();
	// shared with the property sheets' threads, whose TCP/IP pages show the change on their next sample
	AddressResolver::Get().Enable(resolve);
	UISetCheck(id, resolve);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
	return 0;
}

namespace {
	PCWSTR const TaskManagerOptionsKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\taskmgr.exe";

	std::wstring ExePath() {
		WCHAR path[MAX_PATH];
		::GetModuleFileName(nullptr, path, _countof(path));
		return path;
	}

	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}
}

bool CMainFrame::IsTaskManagerReplaced() {
	CRegKey key;
	if (key.Open(HKEY_LOCAL_MACHINE, TaskManagerOptionsKey, KEY_QUERY_VALUE) != ERROR_SUCCESS)
		return false;
	WCHAR value[MAX_PATH * 2];
	ULONG chars = _countof(value);
	if (key.QueryStringValue(L"Debugger", value, &chars) != ERROR_SUCCESS)
		return false;
	// another tool's (Process Explorer's) isn't this one
	return Lowercase(value).find(Lowercase(ExePath())) != std::wstring::npos;
}

LRESULT CMainFrame::OnReplaceTaskManager(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// Windows starts the "debugger" instead of taskmgr.exe (Ctrl+Shift+Esc too), with taskmgr's path as an argument
	bool replace = !IsTaskManagerReplaced();
	CRegKey key;
	auto error = key.Create(HKEY_LOCAL_MACHINE, TaskManagerOptionsKey, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE);
	if (error == ERROR_SUCCESS)
		error = replace ? key.SetStringValue(L"Debugger", std::format(L"\"{}\"", ExePath()).c_str()) : key.DeleteValue(L"Debugger");
	if (error != ERROR_SUCCESS)
		AtlMessageBox(m_hWnd, std::format(L"Failed to {} Task Manager: {}", replace ? L"replace" : L"restore",
			WinSys::Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
	UISetCheck(ID_OPTIONS_REPLACETASKMANAGER, IsTaskManagerReplaced());
	return 0;
}

void CMainFrame::InitImageList() {
	auto size = DpiHelper::SmallIconSize(m_hWnd);
	m_Images.Create(size, size, ILC_COLOR32 | ILC_MASK, _countof(s_Icons) + 1, 16);
	AddImages();
}

void CMainFrame::AddImages() {
	auto size = DpiHelper::SmallIconSize(m_hWnd);
	for (auto id : s_Icons)
		m_IconIndices[id] = m_Images.AddIcon(IconHelper::LoadCached(id, size));
	// processes without an icon of their own get the system's default application icon
	CIcon hDefault(LoadDefaultProcessIcon(size));
	m_IconIndices[IDI_PROCESS] = m_Images.AddIcon(hDefault);
}

void CMainFrame::UpdateImageList() {
	// the same image list (the views and the tab view have it) at the same indices, in the new size: SetIconSize
	// empties it, and the images are added again in their order, the process icons after the frame's
	auto size = DpiHelper::SmallIconSize(m_hWnd);
	m_Images.SetIconSize(size, size);
	AddImages();
	std::vector<std::pair<int, std::wstring const*>> processIcons;
	for (auto& [path, index] : m_ProcessIcons)
		if (index != GetIconIndex(IDI_PROCESS))
			processIcons.push_back({ index, &path });
	std::ranges::sort(processIcons);
	CIcon hDefault(LoadDefaultProcessIcon(size));
	for (auto& [index, path] : processIcons) {
		CIcon hIcon(ExtractFileIcon(*path, size));
		ATLVERIFY(m_Images.AddIcon(hIcon ? hIcon.m_hIcon : hDefault.m_hIcon) == index);
	}
}

void CMainFrame::InitToolBar(bool elevated) {
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	// the button images' size is at 96 DPI, scaled to the frame's
	m_ToolBar = ToolbarHelper::CreateAndInitToolBar(m_hWnd, s_ToolBarButtons, _countof(s_ToolBarButtons), ToolBarIconSize);
	AddToolBarImages();
	if (!elevated) {
		m_ToolBar.AddSeparator(0);
		m_ToolBar.AddButton(ID_FILE_RUNASADMIN, BTNS_BUTTON, TBSTATE_ENABLED, m_ShieldImage, nullptr, 0);
	}

	AddSimpleReBarBand(m_ToolBar);
	UIAddToolBar(m_ToolBar);
	m_WindowFinder.Init(m_ToolBar, ID_FIND_WINDOW,
		[this](HWND hWnd) { OnFinderHover(hWnd); }, [this](HWND hWnd) { OnWindowFound(hWnd); });
}

void CMainFrame::AddToolBarImages() {
	// after the buttons' own images
	auto size = DpiHelper::Scale(ToolBarIconSize, m_ToolBar.m_hWnd);
	CImageList images = m_ToolBar.GetImageList();
	// the pause button switches to a "run" image while paused
	m_PauseImage = 0;	// first button's image
	m_RunImage = images.AddIcon(IconHelper::LoadCached(IDI_RUN, size));
	CIcon hShield(IconHelper::GetStockIcon(SIID_SHIELD, true, DpiHelper::GetDpi(m_ToolBar)));
	m_ShieldImage = images.AddIcon(hShield);
}

void CMainFrame::UpdateToolBar() {
	// the buttons' images in the new size (the buttons keep their indices)
	ToolbarHelper::UpdateToolBarImages(m_ToolBar, s_ToolBarButtons, _countof(s_ToolBarButtons), ToolBarIconSize);
	AddToolBarImages();
	m_ToolBar.ChangeBitmap(ID_VIEW_PAUSE, m_Paused ? m_RunImage : m_PauseImage);
	m_ToolBar.AutoSize();
	// the band for the toolbar's new size, as AddSimpleReBarBand sets it: a button's height, the width to the last one's end
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_CHILD };
		if (rebar.GetBandInfo(i, &band) && band.hwndChild == m_ToolBar) {
			CRect rc;
			m_ToolBar.GetItemRect(m_ToolBar.GetButtonCount() - 1, &rc);
			band.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
			band.cyMinChild = rc.Height();
			band.cxIdeal = rc.right;
			rebar.SetBandInfo(i, &band);
			break;
		}
	}
}

void CMainFrame::InitMenu(HMENU hMenu) {
	// GetShieldIcon creates a new icon on every call, and InitMenu caches its bitmaps by icon handle: one for each DPI
	static std::unordered_map<UINT, HICON> s_Shields;
	auto dpi = DpiHelper::GetDpi(m_hWnd);
	auto& hShield = s_Shields[dpi];
	if (!hShield)
		hShield = IconHelper::GetShieldIcon(dpi);

	std::vector<MenuItemData> commands{
		{ ID_FILE_RUNASADMIN, 0, hShield },
		{ ID_FILE_RUN, IDI_RUN_COMMAND },
		{ ID_FILE_RUNAS, IDI_USERS },
		{ ID_VIEW_PAUSE, IDI_PAUSE },
		{ ID_VIEW_REFRESH, IDI_REFRESH },
		// as in the process and thread context menus
		{ ID_PROCESS_KILL, IDI_DELETE },
		{ ID_PROCESS_SUSPEND, IDI_PAUSE },
		{ ID_PROCESS_RESUME, IDI_RUN },
	};
	for (auto& tab : s_Tabs)
		commands.push_back({ (int)tab.CommandId, (int)tab.Icon });

	WTLHelper::InitMenu(hMenu, commands.data(), (int)commands.size(), DpiHelper::SmallIconSize(m_hWnd));
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
	m_StatusBar.SetPanes(s_Panes, _countof(s_Panes), false);
	m_StatusBar.SetPaneText(ID_PANE_CPU, L"CPU: --");
	m_StatusBar.SetPaneText(ID_PANE_ELEVATION, WinSys::SecurityHelper::IsRunningElevated() ? L"Administrator" : L"Standard User");
}

void CMainFrame::CreateTabs() {
	// the open tabs by their keys, in tab order; Processes is always there (first, if the setting lacks it)
	std::vector<TabType> types{ TabType::Processes };
	std::wstring keys = AppSettings::Get().Tabs();
	for (size_t start = 0; start <= keys.size(); ) {
		auto end = keys.find(L',', start);
		if (end == std::wstring::npos)
			end = keys.size();
		auto key = keys.substr(start, end - start);
		start = end + 1;
		auto tab = std::ranges::find_if(s_Tabs, [&](auto& t) { return key == t.Key; });
		if (tab == std::end(s_Tabs))
			continue;
		if (tab->Type == TabType::Processes)
			// where the setting has it
			std::erase(types, TabType::Processes);
		if (!std::ranges::contains(types, tab->Type))
			types.push_back(tab->Type);
	}
	if (!std::ranges::contains(types, TabType::Processes))
		types.insert(types.begin(), TabType::Processes);

	for (auto type : types)
		OpenTab(type);
	for (auto& v : m_Views)
		if (v.Type == TabType::Processes)
			m_Tabs.FixedPage = v.View->GetHwnd();
	int active = AppSettings::Get().ActiveTab();
	m_Tabs.SetActivePage(active >= 0 && active < m_Tabs.GetPageCount() ? active : 0);
	UpdateTabCommands();
}

CMainFrame::TabInfo const* CMainFrame::FindTab(TabType type) {
	auto tab = std::ranges::find(s_Tabs, type, &TabInfo::Type);
	return tab == std::end(s_Tabs) ? nullptr : &*tab;
}

CMainFrame::OpenView const* CMainFrame::GetPageView(int page) const {
	if (page < 0 || page >= m_Tabs.GetPageCount())
		return nullptr;
	auto hWnd = m_Tabs.GetPageHWND(page);
	auto it = std::ranges::find_if(m_Views, [&](auto& v) { return v.View->GetHwnd() == hWnd; });
	return it == m_Views.end() ? nullptr : &*it;
}

int CMainFrame::FindPage(TabType type) const {
	for (int i = 0; i < m_Tabs.GetPageCount(); i++)
		if (auto v = GetPageView(i); v && v->Type == type)
			return i;
	return -1;
}

IView* CMainFrame::OpenTab(TabType type) {
	auto tab = FindTab(type);
	auto [view, map] = CreateView(type);
	ATLASSERT(view);
	// known before the page is added, which activates it (TBVN_PAGEACTIVATED)
	m_Views.push_back({ type, view });
	// a view opened later gets what the others got at startup (OnCreate)
	if (m_Started) {
		view->OnThemeChanged(AppSettings::Get().DarkMode());
		if (!m_Font.IsNull())
			view->OnFontChanged(m_Font);
	}
	m_Tabs.AddPage(view->GetHwnd(), view->GetTitle(), GetIconIndex(tab->Icon), map);
	return view;
}

void CMainFrame::ForgetView(int page) {
	auto v = GetPageView(page);
	if (!v)
		return;
	auto view = v->View;
	if (view == m_ActiveView) {
		view->OnActivate(false);
		m_ActiveView = nullptr;
	}
	std::erase_if(m_Views, [&](auto& open) { return open.View == view; });
}

void CMainFrame::CloseTab(int page) {
	auto v = GetPageView(page);
	if (!v || v->Type == TabType::Processes)
		return;
	ForgetView(page);
	// the tab view destroys the page's window, and the view deletes itself
	m_Tabs.RemovePage(page);
	UpdateTabCommands();
	SaveTabs();
}

LRESULT CMainFrame::OnTabClose(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	int page = (int)hdr->idFrom;
	auto v = GetPageView(page);
	if (!v || v->Type == TabType::Processes)
		return 1;
	ForgetView(page);
	// the tab view removes the page once this returns: the page without a view is left out of both
	UpdateTabCommands();
	SaveTabs();
	return 0;
}

void CMainFrame::UpdateTabCommands() {
	for (auto& tab : s_Tabs)
		UISetCheck(tab.CommandId, std::ranges::contains(m_Views, tab.Type, &OpenView::Type));
}

void CMainFrame::SaveTabs() {
	std::wstring keys;
	for (int i = 0; i < m_Tabs.GetPageCount(); i++)
		if (auto v = GetPageView(i))
			keys += (keys.empty() ? L"" : L",") + std::wstring(FindTab(v->Type)->Key);
	auto& settings = AppSettings::Get();
	settings.Tabs(keys);
	settings.Save();
}

LRESULT CMainFrame::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	KillTimer(TimerId);
	SaveSettings();
	m_Tray.Remove();

	// the views delete themselves as their windows are destroyed, and the tab view activates pages while
	// removing them (TBVN_PAGEACTIVATED), so stop talking to the views now
	m_Views.clear();
	m_ActiveView = nullptr;

	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	bHandled = FALSE;
	return 1;
}

LRESULT CMainFrame::OnEndSession(UINT /*uMsg*/, WPARAM ending, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// logoff or shutdown: the process ends after this returns, without WM_DESTROY, so the views save now
	// what they'd save as they're destroyed
	if (ending) {
		KillTimer(TimerId);
		for (auto& v : m_Views)
			v.View->SaveSettings();
		SaveSettings();
	}
	return 0;
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
	for (auto& v : m_Views)
		v.View->OnRefresh(v.View == m_ActiveView);
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
		if (m_Tray.IsShown()) {
			MEMORYSTATUSEX memory{ sizeof(memory) };
			::GlobalMemoryStatusEx(&memory);
			m_Tray.AddSample((float)cpu);
			m_Tray.SetTip(std::format(L"WinSysLive\nCPU: {:.1f}%\nMemory: {}%", cpu, memory.dwMemoryLoad));
			m_Tray.Update();
		}
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
	settings.ActiveTab(m_Tabs.GetActivePage());
	settings.Save();
	// dragging may have reordered them
	SaveTabs();
}

LRESULT CMainFrame::OnPageActivated(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// by the view, not the index: removing a page and dragging tabs shift the indices
	auto v = GetPageView(static_cast<int>(hdr->idFrom));
	auto view = v ? v->View : nullptr;
	if (view == m_ActiveView)
		return 0;
	if (m_ActiveView)
		m_ActiveView->OnActivate(false);
	m_ActiveView = view;
	if (view)
		view->OnActivate(true);
	return 0;
}

LRESULT CMainFrame::OnRun(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CRunDlg dlg;
	dlg.DoModal(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnRunLimited(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CRunDlg dlg(true);
	dlg.DoModal(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnRunAs(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CRunAsDlg dlg;
	dlg.DoModal(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnFileExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnRunAsAdmin(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// save first, so the elevated instance starts with the current settings; /restart: it starts while this one
	// still runs, so Single Instance doesn't apply to it
	SaveSettings();
	if (WinSys::SecurityHelper::RunElevated(L"/restart"))
		PostMessage(WM_CLOSE);
	return 0;
}

LRESULT CMainFrame::OnViewStatusBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	bool show = !::IsWindowVisible(m_hWndStatusBar);
	ShowStatusBar(show);
	auto& settings = AppSettings::Get();
	settings.StatusBar(show);
	settings.Save();
	return 0;
}

LRESULT CMainFrame::OnViewToolBar(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	bool show = !::IsWindowVisible(m_hWndToolBar);
	ShowToolBar(show);
	auto& settings = AppSettings::Get();
	settings.ToolBar(show);
	settings.Save();
	return 0;
}

void CMainFrame::ShowStatusBar(bool show) {
	::ShowWindow(m_hWndStatusBar, show ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_STATUS_BAR, show);
	UpdateLayout();
}

void CMainFrame::ShowToolBar(bool show) {
	// m_hWndToolBar is the rebar hosting the toolbar
	::ShowWindow(m_hWndToolBar, show ? SW_SHOWNOACTIVATE : SW_HIDE);
	UISetCheck(ID_VIEW_TOOLBAR, show);
	UpdateLayout();
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
	// a closed tab opens; an open one becomes active, and the active one closes (except Processes)
	auto tab = std::ranges::find(s_Tabs, (UINT)id, &TabInfo::CommandId);
	if (tab == std::end(s_Tabs))
		return 0;
	int page = FindPage(tab->Type);
	if (page < 0) {
		OpenTab(tab->Type);
		UpdateTabCommands();
		SaveTabs();
	}
	else if (page != m_Tabs.GetActivePage())
		m_Tabs.SetActivePage(page);
	else
		CloseTab(page);
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

LRESULT CMainFrame::OnFont(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the setting's height is at 96 DPI; the dialog takes it at the screen's DPI, and the current font if there's no setting
	CClientDC dc(m_hWnd);
	int dpi = dc.GetDeviceCaps(LOGPIXELSY);
	LOGFONT lf;
	m_Font.IsNull() ? (void)::SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0, dpi) : (void)m_Font.GetLogFont(&lf);
	if (!m_Font.IsNull())
		lf.lfHeight = ::MulDiv(lf.lfHeight, dpi, ::GetDpiForWindow(m_hWnd));

	CFontDialog dlg(&lf, CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;

	lf = dlg.m_lf;
	lf.lfHeight = ::MulDiv(lf.lfHeight, 96, dpi);
	lf.lfWidth = 0;
	auto& settings = AppSettings::Get();
	settings.Font(lf);
	settings.Save();
	ApplyFont();
	UIEnable(ID_OPTIONS_DEFAULTFONT, true);
	return 0;
}

LRESULT CMainFrame::OnDefaultFont(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	settings.Font(LOGFONT{});
	settings.Save();
	ApplyFont();
	UIEnable(ID_OPTIONS_DEFAULTFONT, false);
	return 0;
}

LRESULT CMainFrame::OnProcessColors(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CProcessColorsDlg dlg(ProcessColors::Load());
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	ProcessColors::Save(dlg.GetEntries());
	for (auto& v : m_Views)
		v.View->OnProcessColorsChanged();
	return 0;
}

LRESULT CMainFrame::OnResolveSymbols(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	bool resolve = !settings.ResolveSymbols();
	settings.ResolveSymbols(resolve);
	settings.Save();
	// the engine is shared with the property sheets' threads, whose lists show the change on their next refresh
	SymbolEngine::Get().Enable(resolve);
	UISetCheck(id, resolve);
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
	return 0;
}

LRESULT CMainFrame::OnFirmware(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CFirmwareDlg dlg;
	dlg.DoModal(m_hWnd);
	return 0;
}

LRESULT CMainFrame::OnSymbolSettings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CSymbolSettingsDlg dlg;
	if (dlg.DoModal(m_hWnd) == IDOK) {
		// the engine resolves again what's shown (the property sheets' lists on their next refresh)
		SymbolEngine::Get().SetSearchPath(BuildSymbolSearchPath(CSymbolSettingsDlg::LoadOptions()));
		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
	return 0;
}

HWND CMainFrame::GetExportList() {
	auto view = m_ActiveView;
	if (!view)
		return nullptr;
	auto focus = ::GetFocus();
	WCHAR className[32];
	if (focus && ::IsChild(view->GetHwnd(), focus) && ::GetClassName(focus, className, _countof(className))
		&& ::_wcsicmp(className, WC_LISTVIEW) == 0)
		return focus;
	return view->GetExportList();
}

std::vector<std::vector<std::wstring>> CMainFrame::GetExportRows(HWND hList) {
	auto view = m_ActiveView;
	// a TreeListView's tree column comes from the view
	if (view->GetTreeColumnText(hList, 0))
		return ListExport::GetRows(hList, [&](int row) { return view->GetTreeColumnText(hList, row).value_or(L""); });
	return ListExport::GetRows(hList);
}

LRESULT CMainFrame::OnExportList(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto hList = GetExportList();
	if (!hList)
		return 0;
	// read before the dialog: the list goes on changing while it's open
	auto rows = GetExportRows(hList);
	CString title = m_ActiveView->GetTitle();
	SuspendResumeHook sr;
	CSimpleFileDialog dlg(FALSE, L"csv", title + L".csv", OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER,
		L"CSV Files (*.csv)\0*.csv\0Text Files, Tab Separated (*.txt)\0*.txt\0", m_hWnd);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	std::wstring path = dlg.m_szFileName;
	bool tabs = dlg.m_ofn.nFilterIndex == 2;
	if (!ListExport::Save(path, tabs ? ListExport::ToText(rows) : ListExport::ToCsv(rows))) {
		AtlMessageBox(m_hWnd, std::format(L"Failed to write {}: {}", path, WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	SetStatusText(0, std::format(L"{} rows exported to {}", rows.size() - 1, path).c_str());
	return 0;
}

LRESULT CMainFrame::OnCopyAllRows(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto hList = GetExportList()) {
		auto rows = GetExportRows(hList);
		ClipboardHelper::CopyText(m_hWnd, ListExport::ToText(rows).c_str());
		SetStatusText(0, std::format(L"{} rows copied", rows.size() - 1).c_str());
	}
	return 0;
}

LRESULT CMainFrame::OnFindHandle(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_FindDlg || !m_FindDlg->IsWindow()) {
		m_FindDlg = std::make_unique<CFindHandleDlg>(this);
		m_FindDlg->Create(m_hWnd);
	}
	m_FindDlg->ShowWindow(SW_SHOW);
	m_FindDlg->SetActiveWindow();
	m_FindDlg->GotoDlgCtrl(m_FindDlg->GetDlgItem(IDC_FIND_TEXT));
	return 0;
}

void CMainFrame::ApplyFont() {
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto lf = AppSettings::Get().Font();
	if (lf.lfFaceName[0])
		lf.lfHeight = ::MulDiv(lf.lfHeight, dpi, 96);
	else
		// the list views' own default
		::SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0, dpi);

	// the views switch to the new font before the old one is deleted
	CFont font;
	font.CreateFontIndirect(&lf);
	for (auto& v : m_Views)
		v.View->OnFontChanged(font);
	m_Font.Attach(font.Detach());
}

void CMainFrame::SetDarkMode(bool dark) {
	WTLHelper::SwitchToMode(dark ? DarkModeKind::Dark : DarkModeKind::Light, m_hWnd);

	// menu icon bitmaps are drawn on the current menu background, so rebuild them for the new mode
	InitMenu(GetMenu());

	CStatusBarCtrl(m_hWndStatusBar).SetBkColor(dark ? DarkMode::getBackgroundColor() : CLR_DEFAULT);

	for (auto& v : m_Views)
		v.View->OnThemeChanged(dark);

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
	return GetProcessIcon(process.GetImagePath());
}

int CMainFrame::GetProcessIcon(std::wstring const& path) {
	auto defaultIcon = GetIconIndex(IDI_PROCESS);
	if (path.empty())
		return defaultIcon;

	CString key(path.c_str());
	key.MakeLower();
	if (auto it = m_ProcessIcons.find((PCWSTR)key); it != m_ProcessIcons.end())
		return it->second;

	CIcon hIcon(ExtractFileIcon(path, DpiHelper::SmallIconSize(m_hWnd)));
	int index = hIcon ? m_Images.AddIcon(hIcon) : defaultIcon;
	m_ProcessIcons.insert({ (PCWSTR)key, index });
	return index;
}

ProcessManager& CMainFrame::GetProcessManager() {
	return m_ProcessManager;
}

IView* CMainFrame::ActivateTab(TabType type) {
	int page = FindPage(type);
	if (page < 0) {
		// opening it activates it
		auto view = OpenTab(type);
		UpdateTabCommands();
		SaveTabs();
		return view;
	}
	m_Tabs.SetActivePage(page);
	return GetPageView(page)->View;
}

void CMainFrame::GoToKernelModule(std::wstring const& path) {
	if (auto view = ActivateTab(TabType::KernelModules); view && !view->ShowKernelModule(path))
		AtlMessageBox(m_hWnd, std::format(L"{} isn't loaded.", path).c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
}

void CMainFrame::GoToDriver(std::wstring const& path) {
	if (auto view = ActivateTab(TabType::Drivers); view && !view->ShowDriver(path))
		AtlMessageBox(m_hWnd, std::format(L"No driver service has {} as its image.", path).c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
}

void CMainFrame::OnFinderHover(HWND hWnd) {
	if (!hWnd) {
		if (m_FinderSavedStatus)
			m_StatusBar.SetPaneText(ID_DEFAULT_PANE, m_FinderSavedStatus->c_str());
		m_FinderSavedStatus.reset();
		return;
	}
	if (!m_FinderSavedStatus) {
		CString text;
		m_StatusBar.GetPaneText(ID_DEFAULT_PANE, text);
		m_FinderSavedStatus = (PCWSTR)text;
	}
	DWORD pid = 0;
	::GetWindowThreadProcessId(hWnd, &pid);
	// InternalGetWindowText sends no message, so a hung window can't hang the drag
	WCHAR title[128]{}, className[64]{};
	::InternalGetWindowText(hWnd, title, _countof(title));
	::GetClassName(hWnd, className, _countof(className));
	auto name = m_ProcessManager.GetProcessNameById(pid);
	m_StatusBar.SetPaneText(ID_DEFAULT_PANE, std::format(L"{} ({}): {} [{}]",
		name.empty() ? L"<unknown>" : name, pid, *title ? title : L"<no title>", className).c_str());
}

void CMainFrame::OnWindowFound(HWND hWnd) {
	DWORD pid = 0;
	if (::GetWindowThreadProcessId(hWnd, &pid))
		GoToProcess(pid);
}

void CMainFrame::GoToProcess(uint32_t pid) {
	if (auto view = ActivateTab(TabType::Processes))
		view->ShowProcess(pid);
}
