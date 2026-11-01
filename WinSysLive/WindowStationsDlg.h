#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/WindowStations.h>
#include <functional>

//
// System > Window Stations and Desktops: a modal, resizable dialog. A tree of every session's window stations (WinSys
// WindowStations::Enum; another session's by name only) and, for this session's, their desktops (the input desktop and
// heap sizes said), beside the top-level windows of the selected desktop of the app's own window station, in Z order
// (WindowStations::EnumDesktopWindows: title, class, process, thread, state, rectangle). Another station's desktops
// can't have their windows listed (the status line says why). Visible Only (on at first) and the filter (title, class,
// process, handle) choose the rows; any column sorts (# is the Z order). Double-click or Go to Process selects the
// window's process in the Processes tab, closing the dialog; Bring to Front; Copy copies the selected rows, or all shown.
//
class CWindowStationsDlg :
	public CDialogImpl<CWindowStationsDlg>,
	public CDialogResize<CWindowStationsDlg>,
	public CDpiDialogResize<CWindowStationsDlg> {
public:
	enum { IDD = IDD_WINSTATIONS };

	// images: the frame's (shared); iconIndex: an icon's index in it; processName: a process's image name by ID;
	// goToProcess: selects it in the Processes tab
	CWindowStationsDlg(HIMAGELIST images, std::function<int(UINT)> iconIndex, std::function<std::wstring(uint32_t)> processName,
		std::function<void(uint32_t)> goToProcess)
		: m_Images(images), m_IconIndex(std::move(iconIndex)), m_ProcessName(std::move(processName)), m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CWindowStationsDlg)
		DLGRESIZE_CONTROL(IDC_WS_TREE, DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_WS_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_WS_VISIBLE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_WS_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_WS_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_WS_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_WS_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CWindowStationsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_HANDLER(IDC_WS_TREE, TVN_SELCHANGED, OnTreeSelChanged)
		NOTIFY_HANDLER(IDC_WS_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_WS_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_HANDLER(IDC_WS_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_WS_VISIBLE, OnVisibleOnly)
		COMMAND_ID_HANDLER(IDC_WS_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_WS_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_WINDOW_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_WINDOW_BRINGTOFRONT, OnBringToFront)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CWindowStationsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CWindowStationsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Order, Handle, Title, Class, Process, Pid, Tid, State, Rect, _Count };

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTreeSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnVisibleOnly(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBringToFront(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the stations again, the tree rebuilt with the same desktop selected (by station and name)
	void Load();
	// the selected tree item's station and desktop (indices), -1 for none
	std::pair<int, int> GetSelection() const;
	// the selected desktop's windows (read again)
	void LoadWindows();
	// the rows the filter and Visible Only keep, sorted
	void ShowRows();
	std::wstring GetText(WinSys::WindowInfo const& w, Column column) const;
	WinSys::WindowInfo const* GetSelectedWindow() const;
	void UpdateStatus();

	HIMAGELIST m_Images;
	std::function<int(UINT)> m_IconIndex;
	std::function<std::wstring(uint32_t)> m_ProcessName;
	std::function<void(uint32_t)> m_GoToProcess;
	CTreeViewCtrl m_Tree;
	CListViewCtrl m_List;
	std::vector<WinSys::WindowStationInfo> m_Stations;
	std::vector<WinSys::WindowInfo> m_Windows;	// the selected desktop's, in Z order (the list's item data index it)
	DWORD m_WindowsError{ 0 };	// why the selected desktop's windows couldn't be listed
	std::wstring m_Filter;	// lowercase
	bool m_VisibleOnly{ true };
	int m_SortColumn{ (int)Column::Order };
	bool m_SortAscending{ true };
};
