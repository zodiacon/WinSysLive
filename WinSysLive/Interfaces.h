#pragma once

#include "ProcessInfoEx.h"

// Order matters: tabs are created in this order, and View menu commands map to it (see CMainFrame::s_Tabs)
enum class TabType {
	Processes,
	Services,
	Performance,
	Users,
};

struct IMainFrame abstract {
	virtual HWND GetHwnd() const = 0;
	virtual CUpdateUIBase& GetUI() = 0;
	virtual void SetStatusText(int index, PCWSTR text) = 0;
	virtual BOOL ShowContextMenu(HMENU hMenu, POINT const& pt, DWORD flags = 0) = 0;

	// shared 16x16 image list with the app's icons (views should use LVS_SHAREIMAGELISTS)
	virtual HIMAGELIST GetImageList() const = 0;
	virtual int GetIconIndex(UINT iconId) const = 0;
	// the process's icon in the image list (from its executable, cached by path), or the default process icon
	virtual int GetProcessIcon(ProcessInfoEx& process) = 0;

	// snapshot updated by the frame on every refresh tick, before IView::OnRefresh is called
	virtual ProcessManager& GetProcessManager() = 0;

	// switches to the Processes tab and selects the process
	virtual void GoToProcess(uint32_t pid) = 0;
};

struct IView abstract {
	virtual CString GetTitle() const = 0;
	virtual HWND GetHwnd() const = 0;

	// notifications from the frame
	virtual void OnActivate(bool /*active*/) {}
	// called on every refresh tick for all views, active or not; inactive views should update data only, not redraw
	virtual void OnRefresh(bool /*active*/) {}
	virtual void OnThemeChanged(bool /*dark*/) {}
	// views listing processes select the process (see IMainFrame::GoToProcess)
	virtual void ShowProcess(uint32_t /*pid*/) {}
};
