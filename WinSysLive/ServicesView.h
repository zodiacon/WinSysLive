#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "ColumnLayout.h"
#include "FilterBox.h"
#include <VirtualListView.h>
#include <WinSys/ServiceManager.h>
#include <WinSys/Service.h>
#include <functional>
#include <deque>

//
// The Services tab, and the Drivers tab: the same view for Win32 services or driver services (kernel and file system
// drivers), which differ in what's enumerated, the columns shown by default (drivers have no process or account), the
// setting the columns are kept in (ServiceColumns, DriverServiceColumns) and the start types (a driver has Boot and System,
// and no delayed start).
//
class CServicesView :
	public CViewBase<CServicesView>,
	public CVirtualListView<CServicesView> {
public:
	enum class Kind { Services, Drivers };

	explicit CServicesView(IMainFrame* frame, Kind kind = Kind::Services) : CViewBase(frame), m_Kind(kind) {}

	CString GetTitle() const override;
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void SaveSettings() override {
		SaveColumns();
	}
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override {
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}
	// Properties (Alt+Enter) for the selected service
	void OnFrameIdle() override;
	// the Drivers tab: the driver service whose image is the file
	bool ShowDriver(std::wstring const& path) override;

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CServicesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CServicesView>)
		CHAIN_MSG_MAP(CViewBase<CServicesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_SERVICE_START, OnStart)
		COMMAND_ID_HANDLER(ID_SERVICE_STOP, OnStop)
		COMMAND_ID_HANDLER(ID_SERVICE_PAUSE, OnPause)
		COMMAND_ID_HANDLER(ID_SERVICE_CONTINUE, OnContinue)
		COMMAND_ID_HANDLER(ID_SERVICE_RESTART, OnRestart)
		COMMAND_ID_HANDLER(ID_SERVICE_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_SERVICE_GOTOKMODULE, OnGoToKernelModule)
		COMMAND_RANGE_HANDLER(ID_SERVICE_START_AUTO, ID_SERVICE_START_DISABLED, OnStartType)
		COMMAND_ID_HANDLER(ID_SERVICE_START_BOOT, OnStartType)
		COMMAND_ID_HANDLER(ID_SERVICE_START_SYSTEM, OnStartType)
		COMMAND_ID_HANDLER(ID_VIEW_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		CHAIN_MSG_MAP_ALT(CViewBase<CServicesView>, 1)
	END_MSG_MAP()

private:
	enum class Column {
		Name, DisplayName, State, StartType, Type, ProcessId, ProcessName, Account, BinaryPath, Description,
		ErrorControl, Dependencies, Privileges, Triggers,
		_Count
	};

	struct ServiceItem {
		explicit ServiceItem(WinSys::ServiceInfo info) : Info(std::move(info)) {}

		WinSys::ServiceInfo Info;
		int NewTicks{ 0 };		// > 0: highlighted as new
		int ChangedTicks{ 0 };	// > 0: highlighted as changed (state or process)

		// loaded on first use and cached: each takes a call to the SCM
		WinSys::ServiceConfiguration const* GetConfig();
		std::wstring const& GetDescription();
		std::wstring const& GetPrivileges();	// the required privileges, comma separated
		std::wstring const& GetTriggers();		// "Start: Network endpoint, Stop: ..."
		// the configuration changed: everything is loaded again when next used
		void Reload();

	private:
		std::unique_ptr<WinSys::ServiceConfiguration> m_Config;
		std::wstring m_Description, m_Privileges, m_Triggers;
		bool m_ConfigLoaded{ false }, m_DescriptionLoaded{ false }, m_PrivilegesLoaded{ false }, m_TriggersLoaded{ false };
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnStart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnContinue(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRestart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToKernelModule(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// a driver service's image as a Win32 path (WinSys KernelModules::DriverImagePath); empty if its configuration can't be read
	std::wstring DriverPath(ServiceItem& item) const;
	LRESULT OnStartType(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's order, which the user can change by dragging)
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	std::wstring GetText(ServiceItem& item, Column column) const;
	// the filter: a service matches if its name, display name, description, process (ID or name) or binary path
	// contains the text
	bool Matches(ServiceItem& item) const;
	// the list's rows: the services that match the filter, sorted as the list is; the selection is kept by name
	void ShowItems();
	// the matches' count in the status line
	void UpdateFilterStatus();
	int CompareServices(ServiceItem& s1, ServiceItem& s2, Column column) const;
	ServiceItem* GetSelectedService() const;
	void SelectService(std::wstring const& name);
	bool Control(WinSys::ServiceAccessMask access, PCWSTR operation, std::function<bool(WinSys::Service&)> const& action);
	void ShowError(PCWSTR operation, DWORD error) const;

	// the column table of the view's kind (the same columns, other defaults)
	std::span<ColumnLayout::ColumnDef const> Columns() const;
	WinSys::ServiceEnumType EnumType() const;

	Kind m_Kind;
	CListViewCtrl m_List;
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::vector<std::shared_ptr<ServiceItem>> m_All;	// every service, in enumeration order
	std::vector<std::shared_ptr<ServiceItem>> m_Items;	// the list's rows (those matching the filter), in display order
	bool m_ShowPending{ false };	// refreshed while hidden: the rows are chosen and sorted when shown
	CFilterBox m_Filter;	// in the view's rebar
	std::unordered_map<std::wstring, std::shared_ptr<ServiceItem>> m_ItemsByName;

	// Stop and Restart with running dependents: stop them first (in the SCM's order), then the service; a restart then
	// starts the service and the dependents again, in the reverse order. One step at a time, each waited for (polled
	// with a timer, so the UI doesn't block)
	struct ControlStep {
		std::wstring Name;
		bool Start;
	};
	// the selected service's stop (and start, for a restart) with its running dependents, after a confirmation if it has
	// any; false if canceled or they can't be listed
	bool PlanStop(ServiceItem const& item, bool restart);
	// runs the steps until one has to be waited for; the timer runs while steps remain
	void RunSteps();
	enum { StepTimerId = 1 };
	std::deque<ControlStep> m_Steps;
	bool m_StepIssued{ false };	// the first step's control was sent: waiting for its state
	ULONGLONG m_StepDeadline{ 0 };
	std::wstring m_StepErrors;	// starts that failed, shown when the steps are done
};
