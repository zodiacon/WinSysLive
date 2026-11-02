#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Devices.h>
#include <array>
#include <functional>

//
// The Devices tab's Resources by Type: a modal, resizable dialog of every present device's resources (Devices::Enum,
// then Devices::GetResources for each), as Device Manager's View > Resources by type, in one list: type, setting, size,
// device and flags (CDeviceResourcesDlg::ResourceCells), by type and then address (an interrupt by its number, the
// message-signaled ones after the line-based ones), so a bridge window's range comes before the devices' ranges in it
// and a shared interrupt's devices are together. A header sorts by its column (the setting and size by value). The
// filter box matches every cell; double-click a row (or Enter) for its device's Resources and Drivers; Copy copies the
// selected rows (or all) with the header.
//
class CDeviceResourceMapDlg :
	public CDialogImpl<CDeviceResourceMapDlg>,
	public CDialogResize<CDeviceResourceMapDlg>,
	public CDpiDialogResize<CDeviceResourceMapDlg> {
public:
	enum { IDD = IDD_DEVICERESOURCEMAP };

	// showDevice: the device's Resources and Drivers, over this dialog (its window, the owner)
	explicit CDeviceResourceMapDlg(std::function<void(WinSys::DeviceInfo const&, HWND)> showDevice) : m_ShowDevice(std::move(showDevice)) {}

	BEGIN_DLGRESIZE_MAP(CDeviceResourceMapDlg)
		DLGRESIZE_CONTROL(IDC_RM_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_RM_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_RM_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CDeviceResourceMapDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_RM_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_RM_LIST, LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_HANDLER(IDC_RM_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_RM_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_HANDLER(IDC_RM_LIST, LVN_KEYDOWN, OnListKeyDown)
		COMMAND_ID_HANDLER(IDC_RM_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CDeviceResourceMapDlg>)
		CHAIN_MSG_MAP(CDialogResize<CDeviceResourceMapDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum Column { Type, Setting, Size, Device, Flags, _ColumnCount };

	struct Row {
		WinSys::DeviceResource Resource;
		int Device;		// in m_Devices
		std::array<std::wstring, _ColumnCount> Cells;
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListKeyDown(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Load();
	// the rows the filter keeps, in the sort's order
	void ShowRows();
	void ShowDevice(int row);
	// a resource's place in the default order: its type, then its start (an interrupt: line-based first, then MSI)
	static std::pair<int, int64_t> OrderKey(WinSys::DeviceResource const& r);

	std::function<void(WinSys::DeviceInfo const&, HWND)> m_ShowDevice;
	std::vector<WinSys::DeviceInfo> m_Devices;
	std::vector<Row> m_All;
	std::vector<Row const*> m_Rows;
	CListViewCtrl m_List;
	int m_SortColumn{ -1 };	// -1: the default order
	bool m_SortAscending{ true };
	DWORD m_LoadMs{ 0 };
};
