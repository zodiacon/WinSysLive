#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "ColumnLayout.h"
#include "FilterBox.h"
#include <VirtualListView.h>
#include <WinSys/ServiceManager.h>
#include <WinSys/Service.h>
#include <functional>

class CServicesView :
	public CViewBase<CServicesView>,
	public CVirtualListView<CServicesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;

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

	CListViewCtrl m_List;
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::vector<std::shared_ptr<ServiceItem>> m_All;	// every service, in enumeration order
	std::vector<std::shared_ptr<ServiceItem>> m_Items;	// the list's rows (those matching the filter), in display order
	CFilterBox m_Filter;	// in the view's rebar
	std::unordered_map<std::wstring, std::shared_ptr<ServiceItem>> m_ItemsByName;

	// restart: stop, then start once the service has stopped (polled with a timer, so the UI doesn't block)
	enum { RestartTimerId = 1 };
	std::wstring m_RestartName;
	ULONGLONG m_RestartDeadline{ 0 };
};
