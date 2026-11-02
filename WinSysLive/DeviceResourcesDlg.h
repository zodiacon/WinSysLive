#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Devices.h>
#include <WinSys/FileVersion.h>
#include <array>
#include <atomic>
#include <functional>
#include <thread>

//
// The Devices tab's Resources and Drivers: a modal, resizable dialog of one device. Its class's icon and name as the
// title, the instance ID below. The resources it was given (WinSys Devices::GetResources: memory and I/O ranges with
// their sizes, interrupts (a negative one is message signaled, MSI/MSI-X, as Device Manager shows it), DMA channels,
// bus numbers) with their flags; then its driver stack from the top (Devices::GetDriverStack: the class's and the
// device's upper filters, the function driver, the lower filters, the bus driver), each with its file's description,
// version, company and signature (read on a thread: catalog checks take a while). Double-click a driver (or Go to
// Driver) for it in the Drivers tab, closing the dialog; Properties... opens Device Manager's sheet; Copy copies both
// lists as text.
//
class CDeviceResourcesDlg :
	public CDialogImpl<CDeviceResourcesDlg>,
	public CDialogResize<CDeviceResourcesDlg>,
	public CDpiDialogResize<CDeviceResourcesDlg> {
public:
	enum { IDD = IDD_DEVICERESOURCES };

	// icon: the class's, at the large icon size (the dialog destroys it); goToDriver: the driver's image path, in the
	// Drivers tab
	CDeviceResourcesDlg(WinSys::DeviceInfo device, HICON icon, std::function<void(std::wstring const&)> goToDriver)
		: m_Device(std::move(device)), m_Icon(icon), m_GoToDriver(std::move(goToDriver)) {}

	// a resource's cells as both resource lists show them: type, setting, size, flags
	static std::array<std::wstring, 4> ResourceCells(WinSys::DeviceResource const& r);

	BEGIN_DLGRESIZE_MAP(CDeviceResourcesDlg)
		DLGRESIZE_CONTROL(IDC_DR_TITLE, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_DR_ID, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_DR_RESOURCES, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_DR_DRIVERS_LABEL, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DR_DRIVERS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DR_PROPERTIES, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DR_GOTODRIVER, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DR_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CDeviceResourcesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_FILEINFO, OnFileInfo)
		NOTIFY_HANDLER(IDC_DR_DRIVERS, NM_DBLCLK, OnDriverDoubleClick)
		NOTIFY_HANDLER(IDC_DR_DRIVERS, LVN_ITEMCHANGED, OnDriverChanged)
		COMMAND_ID_HANDLER(IDC_DR_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(IDC_DR_GOTODRIVER, OnGoToDriver)
		COMMAND_ID_HANDLER(IDC_DR_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CDeviceResourcesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CDeviceResourcesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// a driver file's details, from the thread: wParam its row, lParam a FileInfo* (the handler deletes it)
	static constexpr UINT WM_FILEINFO = WM_APP + 0x1D0;
	struct FileInfo {
		WinSys::FileVersionInfo Version;
		std::wstring Signature;
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFileInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDriverDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDriverChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToDriver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void LoadResources();
	void LoadDrivers();
	// the selected driver's row, else the function driver's; -1 if there are none
	int DriverRow() const;
	static void InitColumns(CListViewCtrl& list, std::initializer_list<std::pair<PCWSTR, int>> columns);
	static std::wstring ListText(CListViewCtrl& list);

	WinSys::DeviceInfo m_Device;
	CIcon m_Icon;
	std::function<void(std::wstring const&)> m_GoToDriver;
	std::vector<WinSys::DeviceDriver> m_Drivers;
	CListViewCtrl m_Resources, m_DriverList;
	CFont m_TitleFont;
	std::thread m_Worker;
	std::atomic<bool> m_Cancel{ false };
};
