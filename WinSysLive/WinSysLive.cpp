// WinSysLive.cpp : main source file for WinSysLive.exe
//

#include "pch.h"
#include "MainFrm.h"
#include "ProcessSheet.h"
#include "WTLHelper.h"
#include "AppSettings.h"
#include "Symbols.h"
#include <DpiHelper.h>

CAppModule _Module;
AppSettings _Settings;

int Run(LPTSTR /*lpstrCmdLine*/ = nullptr, int nCmdShow = SW_SHOWDEFAULT) {
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	CMainFrame wndMain;

	if (wndMain.CreateEx() == nullptr) {
		ATLTRACE(_T("Main window creation failed!\n"));
		return 0;
	}

	// restore the last position, but never start minimized
	auto& settings = AppSettings::Get();
	if (auto wp = settings.GetBinary<WINDOWPLACEMENT>(AppSettings::MainWindowPlacement); wp) {
		auto placement = *wp;
		if (placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE)
			placement.showCmd = SW_SHOWNORMAL;
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

	int nRet = theLoop.Run();

	// the property sheets run on threads of their own
	CloseProcessProperties();
	SymbolEngine::Get().Shutdown();

	_Module.RemoveMessageLoop();
	return nRet;
}

// started as Task Manager (Options > Replace Task Manager: Windows passes taskmgr.exe's path) while an instance runs:
// that one comes to the front (from the tray too), as Task Manager does
static bool ActivateRunningInstance(PCWSTR cmdLine) {
	std::wstring args(cmdLine ? cmdLine : L"");
	::CharLowerBuff(args.data(), (DWORD)args.size());
	if (args.find(L"taskmgr.exe") == std::wstring::npos)
		return false;
	auto hWnd = ::FindWindow(CMainFrame::WindowClassName, nullptr);
	if (!hWnd)
		return false;
	// this process may set the foreground window (the user just started it), the running one may not
	DWORD pid = 0;
	::GetWindowThreadProcessId(hWnd, &pid);
	::AllowSetForegroundWindow(pid);
	::PostMessage(hWnd, CMainFrame::ActivateMessage, 0, 0);
	return true;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow) {
	if (ActivateRunningInstance(lpstrCmdLine))
		return 0;

	HRESULT hRes = ::CoInitialize(nullptr);
	ATLASSERT(SUCCEEDED(hRes));

	AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls
	::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	hRes = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	if (AppSettings::Get().Load(L"Software\\ScorpioSoftware\\WinSysLive"))
		WTLHelper::InitDarkMode(AppSettings::Get().DarkMode() ? DarkModeKind::Dark : DarkModeKind::Light);
	else
		WTLHelper::InitDarkMode();

	int nRet = Run(lpstrCmdLine, nCmdShow);

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
