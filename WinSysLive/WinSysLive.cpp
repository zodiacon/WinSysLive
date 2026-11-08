// WinSysLive.cpp : main source file for WinSysLive.exe
//

#include "pch.h"
#include "MainFrm.h"
#include "ProcessSheet.h"
#include "WTLHelper.h"
#include "AppSettings.h"
#include "Symbols.h"
#include "AddressResolver.h"
#include <DpiHelper.h>
#include "SettingsTransfer.h"
#include "UpdateInstaller.h"
#include "ExplorerMenu.h"
#include "CommandLine.h"
#include <WinSys/SecurityHelper.h>
#include <WinSys/LogonTask.h>
#include <WinSys/Token.h>

CAppModule _Module;
AppSettings _Settings;

int Run(LPTSTR lpstrCmdLine = nullptr, int nCmdShow = SW_SHOWDEFAULT) {
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	CMainFrame wndMain;

	if (wndMain.CreateEx() == nullptr) {
		ATLTRACE(_T("Main window creation failed!\n"));
		return 0;
	}

	// restore the last position, but never start minimized (exiting from the tray saves a minimized window): as it was
	// before it was minimized
	auto& settings = AppSettings::Get();
	if (auto wp = settings.GetBinary<WINDOWPLACEMENT>(AppSettings::MainWindowPlacement); wp) {
		auto placement = *wp;
		if (placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE)
			placement.showCmd = (placement.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
		// as Settings::LoadWindowPosition: the first placement puts the window on its monitor, where it takes that
		// monitor's DPI (and its WM_DPICHANGED resizes it from the DPI it had); the second gives it the saved size at
		// that DPI (saved with the position; none before DPI awareness: 96)
		wndMain.SetWindowPlacement(&placement);
		auto dpi = DpiHelper::GetDpi(wndMain);
		auto saved = (UINT)settings.GetValueOrDefault<int>((std::wstring(AppSettings::MainWindowPlacement) + L"Dpi").c_str(), USER_DEFAULT_SCREEN_DPI);
		auto& rc = placement.rcNormalPosition;
		rc.right = rc.left + DpiHelper::Scale(rc.right - rc.left, dpi, saved ? saved : USER_DEFAULT_SCREEN_DPI);
		rc.bottom = rc.top + DpiHelper::Scale(rc.bottom - rc.top, dpi, saved ? saved : USER_DEFAULT_SCREEN_DPI);
		wndMain.SetWindowPlacement(&placement);
	}
	else {
		wndMain.ShowWindow(nCmdShow);
	}
	// started with Windows (Options > Run at Windows Startup): minimized, or to the tray with Minimize to Tray
	if (lpstrCmdLine && std::wstring_view(lpstrCmdLine).find(L"/minimized") != std::wstring_view::npos)
		wndMain.ShowWindow(SW_SHOWMINNOACTIVE);
	// Explorer's Who's Locking This? with no instance running (ActivateRunningInstance hands it to one)
	if (auto path = ExplorerMenu::GetLocksArgument(lpstrCmdLine))
		wndMain.ShowLocks(*path);

	int nRet = theLoop.Run();

	// the property sheets run on threads of their own
	CloseProcessProperties();
	SymbolEngine::Get().Shutdown();
	AddressResolver::Get().Shutdown();

	_Module.RemoveMessageLoop();
	return nRet;
}

// started as Task Manager (Options > Replace Task Manager: Windows passes taskmgr.exe's path), or at all with Options >
// Single Instance (single), while an instance runs: that one comes to the front (from the tray too), as Task Manager
// does. Not when Run as Administrator starts the elevated instance (/restart), while this one still runs
static bool ActivateRunningInstance(PCWSTR cmdLine, bool single) {
	std::wstring args(cmdLine ? cmdLine : L"");
	::CharLowerBuff(args.data(), (DWORD)args.size());
	if (args.find(L"/restart") != std::wstring::npos)
		return false;
	// Explorer's Who's Locking This? goes to a running instance, Single Instance or not: it has the processes already
	auto locks = ExplorerMenu::GetLocksArgument(cmdLine);
	if (!single && !locks && args.find(L"taskmgr.exe") == std::wstring::npos)
		return false;
	auto hWnd = ::FindWindow(CMainFrame::WindowClassName, nullptr);
	if (!hWnd)
		return false;
	// this process may set the foreground window (the user just started it), the running one may not
	DWORD pid = 0;
	::GetWindowThreadProcessId(hWnd, &pid);
	::AllowSetForegroundWindow(pid);
	if (locks) {
		// the path with its null; the running instance posts itself the search, so this returns at once
		COPYDATASTRUCT cds{ ExplorerMenu::CopyDataLocks, DWORD((locks->size() + 1) * sizeof(WCHAR)), locks->data() };
		DWORD_PTR result = 0;
		if (::SendMessageTimeout(hWnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 5000, &result) && result)
			return true;
		// a hung instance (or an older one, without the handler): this one searches
		return false;
	}
	::PostMessage(hWnd, CMainFrame::ActivateMessage, 0, 0);
	return true;
}

// started as Task Manager (taskmgr.exe's path) without elevation: elevated, as Task Manager starts, through the on-demand
// task Replace Task Manager registers (no UAC prompt), else for an administrator through the prompt. True if the
// elevated instance is on its way; otherwise (a standard user, the prompt declined) this instance runs
static bool StartElevatedAsTaskManager(PCWSTR cmdLine) {
	std::wstring args(cmdLine ? cmdLine : L"");
	::CharLowerBuff(args.data(), (DWORD)args.size());
	if (args.find(L"taskmgr.exe") == std::wstring::npos || WinSys::SecurityHelper::IsRunningElevated())
		return false;
	WCHAR exe[MAX_PATH];
	::GetModuleFileName(nullptr, exe, _countof(exe));
	// the user just started this one; the elevated instance comes from the Task Scheduler
	::AllowSetForegroundWindow(ASFW_ANY);
	// a task left for another copy of the exe isn't this one's (unreadable: run it anyway)
	auto task = CMainFrame::TaskManagerTaskName();
	auto action = WinSys::LogonTask::GetAction(task);
	if ((!action || _wcsicmp(action->Path.c_str(), exe) == 0) && WinSys::LogonTask::Run(task))
		return true;

	// an administrator's limited token has an elevated one linked
	auto token = WinSys::Token::Open(::GetCurrentProcessId());
	if (!token || token->GetElevationType() != WinSys::ElevationType::Limited)
		return false;
	SHELLEXECUTEINFO sei{ sizeof(sei) };
	sei.lpVerb = L"runas";
	sei.lpFile = exe;
	sei.lpParameters = L"/taskmgr";
	sei.nShow = SW_SHOWNORMAL;
	return ::ShellExecuteEx(&sei);
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow) {
	HRESULT hRes = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_SPEED_OVER_MEMORY | COINIT_DISABLE_OLE1DDE);
	ATLASSERT(SUCCEEDED(hRes));
	// the elevated half of installing an update (/applyupdate): no window, no settings
	if (int code = UpdateInstaller::RunCommandLine(); code >= 0) {
		::CoUninitialize();
		return code;
	}

	AtlInitCommonControls(ICC_BAR_CLASSES | ICC_LINK_CLASS);	// SysLink: the About box
	::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	// an elevated token has SeDebugPrivilege, disabled. Enabled, it opens more processes and threads, and since
	// Windows 11 24H2 the system handle table (SystemExtendedHandleInformation) has the objects' addresses only with it
	// (null otherwise, elevated or not): the Handles pages' Address column, the handle sheet, zombie processes
	WinSys::SecurityHelper::EnablePrivilege(SE_DEBUG_NAME, true);

	// /snapshot, /compare, /export, /help: no window, no settings, no running instance involved
	if (auto request = CommandLine::Parse(::GetCommandLine())) {
		int code = CommandLine::Run(*request);
		::CoUninitialize();
		return code;
	}

	hRes = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	bool loaded = AppSettings::Get().Load(L"Software\\ScorpioSoftware\\WinSysLive");
	// Options > Import Settings, applied now that nothing has read the settings yet
	if (SettingsTransfer::ApplyPendingImport())
		loaded = true;
	// what an installed update left behind
	UpdateInstaller::CleanupAsync();
	if (ActivateRunningInstance(lpstrCmdLine, loaded && AppSettings::Get().SingleInstance()) || StartElevatedAsTaskManager(lpstrCmdLine)) {
		_Module.Term();
		::CoUninitialize();
		return 0;
	}
	if (loaded)
		WTLHelper::InitDarkMode(AppSettings::Get().DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light);
	else
		WTLHelper::InitDarkMode();

	int nRet = Run(lpstrCmdLine, nCmdShow);

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
