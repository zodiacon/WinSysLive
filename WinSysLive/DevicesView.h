#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <TreeListView/include/TreeListView.h>
#include <WinSys/Devices.h>
#include <cfgmgr32.h>
#include <SetupAPI.h>
#include <deque>
#include <thread>
#include <mutex>
#include <unordered_set>

//
// The Devices tab: the device tree as Device Manager shows it (WinSys Devices::Enum), by type (a row per device class,
// "Other devices" for those without one, the class's devices under it) or by connection (the tree from the computer
// down; disconnected devices under a row of their own). Columns: name, class, status (a problem code in words, disabled,
// not connected), driver service, manufacturer, driver version, date and provider, INF, location, instance ID, PDO name,
// enumerator. Each row has its class's icon, loaded at the tab's icon size from where the class says it is
// (Devices::GetClassIconPath, "setupapi.dll,-5": SHDefExtractIcon), so it's sharp at any DPI; else SetupDiLoadClassIcon's
// (32x32, scaled), else IDI_DEVICE. The view's own image list, made again on a DPI change. A device with a problem is highlighted, a disabled or disconnected one grayed. The toolbar right of the
// filter box: By Connection, Problem Devices (only those with a problem or disabled), Hidden Devices (DN_NO_SHOW_IN_DM,
// which Device Manager hides too), Disconnected Devices and Scan Again; all but Problem Devices are saved (DevicesBy-
// Connection, DevicesShowHidden, DevicesShowDisconnected). The filter matches name, class, instance ID, driver,
// manufacturer and provider; in the connection tree a match's ancestors stay, grayed. Scanned on a thread of its own
// (WM_DEVICES_SCANNED; about half a second with the disconnected devices) when the tab opens, on Scan Again, and a second
// after the devices change (CM_Register_Notification for every device instance, WM_DEVICES_CHANGED); the selection and
// the expanded rows are kept by instance ID (a class row by name). Context menu: Properties (Device Manager's property
// sheet, devmgr.dll's DeviceProperties_RunDLL), Enable Device / Disable Device... (WinSys Devices::SetEnabled, after a
// confirmation for disabling, a stronger one for the classes the machine depends on; elevation needed; a restart said
// when one is needed), Go to Driver (the Drivers tab, by the service's image), Copy Instance ID, Copy, and Scan for
// Hardware Changes (Devices::ScanForChanges: the arrivals come as device notifications).
//
class CDevicesView : public CViewBase<CDevicesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	bool CanCopy() const override {
		return GetSelectedRow() != nullptr;
	}
	HWND GetExportList() override {
		return m_Tree.GetListCtrl();
	}
	std::optional<std::wstring> GetTreeColumnText(HWND hList, int row) override {
		if (hList != m_Tree.GetListCtrl())
			return std::nullopt;
		auto item = m_Tree.GetVisibleItem(row);
		return item ? m_Tree.GetCellText(item, 0) : std::wstring();
	}
	void OnActivate(bool active) override;
	void OnThemeChanged(bool dark) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	// a class's icon at a size (the caller destroys it): from its icon path, else SetupAPI's, else IDI_DEVICE
	static HICON LoadClassIcon(std::wstring const& classGuid, int size);
	void OnDpiChanged(UINT /*dpi*/) override {
		// the class icons again at the new size
		CreateClassImages();
		m_Tree.GetListCtrl().Invalidate();
		LayoutBar();
	}
	// the toolbar's band takes the rest of the row
	void UpdateLayout(BOOL resizeBars = TRUE);

	static constexpr UINT WM_DEVICES_SCANNED = WM_APP + 0x1C0;
	static constexpr UINT WM_DEVICES_CHANGED = WM_APP + 0x1C1;

	BEGIN_MSG_MAP(CDevicesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_DEVICES_SCANNED, OnScanned)
		MESSAGE_HANDLER(WM_DEVICES_CHANGED, OnDevicesChanged)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		// the toolbar's, which come to the view itself (through the rebar), not routed by the frame
		COMMAND_RANGE_HANDLER(ID_DEVICES_BYCONNECTION, ID_DEVICES_RESCAN, OnBarCommand)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CViewBase<CDevicesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_COLUMN_RESET, OnResetColumns)
		COMMAND_ID_HANDLER(ID_DEVICE_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_DEVICE_RESOURCES, OnResources)
		COMMAND_ID_HANDLER(ID_DEVICE_GOTODRIVER, OnGoToDriver)
		COMMAND_ID_HANDLER(ID_DEVICE_COPYID, OnCopyId)
		COMMAND_ID_HANDLER(ID_DEVICE_ENABLE, OnEnable)
		COMMAND_ID_HANDLER(ID_DEVICE_SCANHARDWARE, OnScanHardware)
		COMMAND_RANGE_HANDLER(ID_DEVICES_BYCONNECTION, ID_DEVICES_RESCAN, OnBarCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CDevicesView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Name, Class, Status, Driver, Manufacturer, DriverVersion, DriverDate, Provider, Inf, Location, InstanceId, PdoName,
		Enumerator,
		_Count
	};

	enum class RowKind { Device, Class, Disconnected };

	struct Row {
		RowKind Kind{ RowKind::Device };
		int Device{ -1 };		// into m_Devices
		std::wstring Name;		// a class row's (and the disconnected group's)
		int Count{ 0 };			// a class row's devices
		int Problems{ 0 };		// a class row's devices with a problem
		bool Match{ true };		// a device shown only as the ancestor of a match is grayed
		HTLITEM Item{ nullptr };
		std::wstring Key() const;	// across scans: the instance ID, or "class:" and the name
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDevicesChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBarCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSort(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResources(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToDriver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyId(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEnable(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnScanHardware(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void StartScan();
	// the tree again from m_Devices, keeping the selection and the expanded rows
	void Build();
	// a device the toolbar's options and the filter keep
	bool IsShown(WinSys::DeviceInfo const& d) const;
	bool MatchesFilter(WinSys::DeviceInfo const& d) const;
	void LayoutBar();
	void UpdateBar();
	void UpdateStatus();
	std::wstring GetColumnText(Row const& row, Column column) const;
	std::wstring StatusText(WinSys::DeviceInfo const& d) const;
	std::wstring ComputerName() const;
	Row* GetSelectedRow() const;
	WinSys::DeviceInfo const* GetSelectedDevice() const;
	void ShowContextMenu(POINT pt);
	void Sort();
	static int CALLBACK CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam);
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	CTreeListViewCtrl m_Tree;
	CFilterBox m_Filter;
	CToolBarCtrl m_Bar;
	CFont m_BarFont;
	std::vector<WinSys::DeviceInfo> m_Devices;	// the last scan's
	std::deque<Row> m_Rows;	// the tree's (a deque: the items' data stay valid as rows are added)
	std::unordered_set<std::wstring> m_Expanded;	// rows' keys, before a rebuild
	bool m_Built{ false };	// the first tree was built (it starts with its default expansion)
	std::vector<Column> m_Columns;	// by list column
	int m_MenuColumn{ -1 };
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
	bool m_ByConnection{ false }, m_ProblemsOnly{ false }, m_ShowHidden{ false }, m_ShowDisconnected{ false };
	bool m_Active{ false };
	// the scan
	std::jthread m_Worker;
	bool m_Scanning{ false };
	bool m_ScanAgain{ false };	// a change came during the scan
	std::mutex m_ResultLock;
	std::optional<std::vector<WinSys::DeviceInfo>> m_Result;
	int64_t m_ScanTime{ 0 };
	HCMNOTIFICATION m_Notification{ nullptr };
	// the class icons at the frame's image list's size, the tree's (shared: the view destroys it)
	CImageList m_ClassImages;
	std::unordered_map<std::wstring, int> m_ClassImageIndex;	// by class GUID (text)
	void CreateClassImages();
	// a class's icon in the tree's image list (an empty GUID: the unknown class's)
	int ClassImage(std::wstring const& classGuid);
};
