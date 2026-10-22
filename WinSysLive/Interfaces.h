#pragma once

#include "ProcessInfoEx.h"
#include "IconSource.h"

// Order matters: tabs are created in this order, and View menu commands map to it (see CMainFrame::s_Tabs)
enum class TabType {
	Processes,
	Services,
	Performance,
	Users,
	Network,
	KernelModules,
	Drivers,	// the driver services (the Services view for drivers)
	Ipc,		// RPC/ALPC
};

// the shared 16x16 image list with the app's icons (IIconSource: views should use LVS_SHAREIMAGELISTS)
struct IMainFrame abstract : IIconSource {
	virtual HWND GetHwnd() const = 0;
	virtual CUpdateUIBase& GetUI() = 0;
	virtual void SetStatusText(int index, PCWSTR text) = 0;
	virtual BOOL ShowContextMenu(HMENU hMenu, POINT const& pt, DWORD flags = 0) = 0;

	// the process's icon in the image list (from its executable, cached by path), or the default process icon
	virtual int GetProcessIcon(ProcessInfoEx& process) = 0;

	// snapshot updated by the frame on every refresh tick, before IView::OnRefresh is called
	virtual ProcessManager& GetProcessManager() = 0;

	// switches to the Processes tab and selects the process
	virtual void GoToProcess(uint32_t pid) = 0;
	// switches to the Kernel Modules tab and selects the module loaded from the file (a Win32 path), or says it isn't loaded
	virtual void GoToKernelModule(std::wstring const& path) = 0;
	// switches to the Drivers tab and selects the driver service whose image is the file, or says there's none
	virtual void GoToDriver(std::wstring const& path) = 0;
};

struct IView abstract {
	virtual CString GetTitle() const = 0;
	virtual HWND GetHwnd() const = 0;

	// notifications from the frame
	virtual void OnActivate(bool /*active*/) {}
	// called on every refresh tick for all views, active or not; inactive views should update data only, not redraw
	virtual void OnRefresh(bool /*active*/) {}
	virtual void OnThemeChanged(bool /*dark*/) {}
	// the font for the view's lists and text (Options > Font), owned by the frame until the next call
	virtual void OnFontChanged(HFONT /*font*/) {}
	// the ProcessColors setting changed (Options > Process Colors)
	virtual void OnProcessColorsChanged() {}
	// the frame moved to a monitor with another DPI: its image list has the new size (the same handle and indices), which
	// the lists that use it are given again so their rows take its height. Called before the views' own child windows
	// get WM_DPICHANGED_AFTERPARENT (and after OnFontChanged, when there's a font of the user's)
	virtual void OnDpiChanged(UINT /*dpi*/) {}
	// the active view, when the frame's message loop is idle: to update commands that depend on the focus
	virtual void OnFrameIdle() {}
	// the toolbar tooltip of a command whose text depends on the view's state; empty: the command's string resource
	virtual std::wstring GetCommandTip(UINT /*id*/) const { return {}; }
	// views listing processes select the process (see IMainFrame::GoToProcess)
	virtual void ShowProcess(uint32_t /*pid*/) {}
	// the Kernel Modules and Drivers views select the module or driver service of the file (see IMainFrame::GoToKernelModule,
	// GoToDriver); false if there's none
	virtual bool ShowKernelModule(std::wstring const& /*path*/) { return false; }
	virtual bool ShowDriver(std::wstring const& /*path*/) { return false; }
	// the view's main list, for File > Export List and Copy All Rows (a list in the view with the focus comes first);
	// nullptr if it has none
	virtual HWND GetExportList() { return nullptr; }
	// for a list of the view's that's a TreeListView's: column 0's text of a row (the TreeListView draws that column
	// itself, so the list has none); nullopt for other lists
	virtual std::optional<std::wstring> GetTreeColumnText(HWND /*hList*/, int /*row*/) { return std::nullopt; }
	// saves what the view saves when it's destroyed (columns, splitter...): the session is ending, and no window
	// will be destroyed
	virtual void SaveSettings() {}
};
