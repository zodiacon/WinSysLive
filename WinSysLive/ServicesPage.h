#pragma once

#include "ProcessPages.h"
#include <WinSys/ServiceInfo.h>
#include <functional>

namespace WinSys {
	class Service;
	enum class ServiceAccessMask : uint32_t;
}

//
// The Service(s) page, added only for a process hosting running services: the services in a list, with their DLLs for
// svchost.exe, and the selected service's details below it (configuration, description, DLL version, privileges,
// triggers...). The services are read when the page is first shown and on Refresh, which keeps the selection by name.
// Start and Stop control the selected service; a service stopped from the page stays listed (so it can be started
// again), and the states are polled until no service is pending, then read again in full.
//
class CProcServicesPage :
	public CPropertyPageImpl<CProcServicesPage>,
	public CDynamicDialogLayout<CProcServicesPage> {
public:
	enum { IDD = IDD_PROC_SERVICES };

	// title: kept (not copied) until the page is created
	CProcServicesPage(ProcessProperties const& props, PCWSTR title);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcServicesPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_ID_HANDLER(IDC_SVC_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_SVC_START, OnStart)
		COMMAND_ID_HANDLER(IDC_SVC_STOP, OnStop)
		NOTIFY_HANDLER(IDC_SVC_LIST, LVN_ITEMCHANGED, OnItemChanged)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcServicesPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcServicesPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	struct ServiceData {
		std::wstring Name, DisplayName, State, StartType, Dll;
		WinSys::ServiceStatusProcess Status;
		std::vector<std::pair<std::wstring, std::wstring>> Details;
	};

	ServiceData ReadService(std::wstring const& name, std::wstring const& displayName, WinSys::ServiceStatusProcess const& status) const;
	// "Running", or "Running (PID 1234)" for a service that runs in another process now
	std::wstring StateText(WinSys::ServiceStatusProcess const& status) const;

	// reads the services; the one with the given name is selected afterwards, else the first
	void Load(std::wstring const& select = L"");
	void ShowDetails(int index);
	void UpdateButtons();
	// queries the services' states; false once none is pending
	bool UpdateStates();
	// opens the selected service and runs the action on it; false (after saying why) if either fails
	bool Control(WinSys::ServiceAccessMask access, PCWSTR operation, std::function<bool(WinSys::Service&)> const& action);
	int GetSelected() const;

	ProcessProperties const& m_Props;
	CListViewCtrl m_List, m_Details;
	std::vector<ServiceData> m_Services;	// in the list's order
	std::vector<std::wstring> m_Known;	// every service listed, so one stopped from here stays listed
	int m_Polls{ 0 };	// state polls left after a Start or Stop
	bool m_Svchost;	// the list has a Service DLL column
	bool m_Loaded{ false };
};
