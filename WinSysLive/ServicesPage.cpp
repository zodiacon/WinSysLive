#include "pch.h"
#include "ServicesPage.h"
#include <ListViewhelper.h>
#include "FormatHelper.h"
#include "VersionInfo.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/Service.h>
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

namespace {
	const UINT_PTR PollTimerId = 1, StepTimerId = 2;
	const int PollInterval = 500, MaxPolls = 60;	// 30 seconds

	bool IsPending(ServiceState state) {
		return state != ServiceState::Running && state != ServiceState::Stopped && state != ServiceState::Paused;
	}

	enum class Column {
		Name, DisplayName, State, StartType, Dll
	};

	void InitList(CListViewCtrl& list, HWND hList, std::initializer_list<std::pair<PCWSTR, int>> columns) {
		list.Attach(hList);
		list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		auto dpi = ::GetDpiForWindow(hList);
		int i = 0;
		for (auto& [name, width] : columns)
			list.InsertColumn(i++, name, LVCFMT_LEFT, ::MulDiv(width, dpi, 96));
		// the widths follow the list's DPI from now on
		ListViewHelper::EnableColumnDpiScaling(list);
	}

	std::wstring ControlsAcceptedToString(ServiceControlsAccepted controls) {
		static const std::pair<ServiceControlsAccepted, PCWSTR> names[] = {
			{ ServiceControlsAccepted::Stop, L"Stop" },
			{ ServiceControlsAccepted::PauseContinue, L"Pause/Continue" },
			{ ServiceControlsAccepted::Shutdown, L"Shutdown" },
			{ ServiceControlsAccepted::Preshutdown, L"Preshutdown" },
			{ ServiceControlsAccepted::ParamChange, L"Parameter change" },
			{ ServiceControlsAccepted::NetBindChange, L"Net bind change" },
			{ ServiceControlsAccepted::HardwareProfileChange, L"Hardware profile change" },
			{ ServiceControlsAccepted::PowerEvent, L"Power event" },
			{ ServiceControlsAccepted::SessionChange, L"Session change" },
			{ ServiceControlsAccepted::TimeChanged, L"Time change" },
			{ ServiceControlsAccepted::TriggerEvent, L"Trigger event" },
			{ ServiceControlsAccepted::UserLogOff, L"User logoff" },
			{ ServiceControlsAccepted::LowResources, L"Low resources" },
			{ ServiceControlsAccepted::SystemLowResources, L"System low resources" },
		};
		std::wstring text;
		for (auto& [flag, name] : names)
			if ((controls & flag) == flag)
				text += (text.empty() ? L"" : L", ") + std::wstring(name);
		return text.empty() ? L"None" : text;
	}

	PCWSTR SidTypeToString(ServiceSidType type) {
		switch (type) {
			case ServiceSidType::None: return L"None";
			case ServiceSidType::Restricted: return L"Restricted";
			case ServiceSidType::Unrestricted: return L"Unrestricted";
		}
		return L"";
	}

	template<typename T, typename F>
	std::wstring Join(std::vector<T> const& items, F&& format) {
		std::wstring text;
		for (auto& item : items)
			text += (text.empty() ? L"" : L", ") + std::wstring(format(item));
		return text;
	}
}

CProcServicesPage::CProcServicesPage(ProcessProperties const& props, PCWSTR title) : CPropertyPageImpl(title), m_Props(props) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_SERVICES);
	m_Svchost = ::_wcsicmp(m_Props.ImageName.c_str(), L"svchost.exe") == 0;
}

LRESULT CProcServicesPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_Svchost)
		InitList(m_List, GetDlgItem(IDC_SVC_LIST), { { L"Name", 150 }, { L"Display Name", 230 }, { L"State", 120 },
			{ L"Start Type", 110 }, { L"Service DLL", 300 } });
	else
		InitList(m_List, GetDlgItem(IDC_SVC_LIST), { { L"Name", 150 }, { L"Display Name", 260 }, { L"State", 120 },
			{ L"Start Type", 160 } });
	InitList(m_Details, GetDlgItem(IDC_SVC_DETAILS), { { L"Property", 150 }, { L"Value", 480 } });
	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcServicesPage::OnSetActive() {
	if (!m_Loaded) {
		m_Loaded = true;
		CWaitCursor wait;
		Load();
	}
	return 0;
}

LRESULT CProcServicesPage::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	CWaitCursor wait;
	Load(selected >= 0 ? m_Services[selected].Name : L"");
	return 0;
}

LRESULT CProcServicesPage::OnItemChanged(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(pnmh);
	if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
		ShowDetails(m_List.GetNextItem(-1, LVNI_SELECTED));
	return 0;
}

void CProcServicesPage::Load(std::wstring const& select) {
	m_List.DeleteAllItems();
	m_Details.DeleteAllItems();
	m_Services.clear();

	auto services = ServiceManager::EnumServices(ServiceEnumType::AllServices, ServiceEnumState::Active);
	// there are always running services
	auto error = services.empty() ? ::GetLastError() : ERROR_SUCCESS;
	std::erase_if(services, [&](auto& svc) { return svc.GetStatusProcess().ProcessId != m_Props.Id; });
	auto hosted = [&](std::wstring const& name) {
		return std::ranges::any_of(services, [&](auto& svc) { return ::_wcsicmp(svc.GetName().c_str(), name.c_str()) == 0; });
	};

	for (auto& svc : services)
		m_Services.push_back(ReadService(svc.GetName(), svc.GetDisplayName(), svc.GetStatusProcess()));
	// those stopped from the page (or moved to another process since)
	for (auto& name : m_Known)
		if (!hosted(name)) {
			auto config = ServiceManager::GetServiceConfiguration(name);
			m_Services.push_back(ReadService(name, config ? config->DisplayName : name, ServiceManager::GetServiceStatus(name)));
		}
	std::ranges::sort(m_Services, [](auto& a, auto& b) { return ::_wcsicmp(a.Name.c_str(), b.Name.c_str()) < 0; });
	m_Known.clear();
	for (auto& data : m_Services)
		m_Known.push_back(data.Name);

	int selected = 0;
	for (int i = 0; i < (int)m_Services.size(); i++) {
		auto& data = m_Services[i];
		m_List.InsertItem(i, data.Name.c_str());
		m_List.SetItemText(i, (int)Column::DisplayName, data.DisplayName.c_str());
		m_List.SetItemText(i, (int)Column::State, data.State.c_str());
		m_List.SetItemText(i, (int)Column::StartType, data.StartType.c_str());
		if (m_Svchost)
			m_List.SetItemText(i, (int)Column::Dll, data.Dll.c_str());
		if (::_wcsicmp(data.Name.c_str(), select.c_str()) == 0)
			selected = i;
	}
	SetDlgItemText(IDC_SVC_LIST_LABEL, std::format(L"Services ({}):", m_Services.size()).c_str());
	if (m_Services.empty()) {
		SetDlgItemText(IDC_SVC_STATUS, error != ERROR_SUCCESS
			? (L"Can't enumerate the services: " + Helpers::GetErrorText(error)).c_str() : L"The process hosts no running services now.");
		UpdateButtons();
		return;
	}
	SetDlgItemText(IDC_SVC_STATUS, L"");
	m_List.SetItemState(selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.EnsureVisible(selected, FALSE);
}

std::wstring CProcServicesPage::StateText(ServiceStatusProcess const& status) const {
	std::wstring text = FormatHelper::ServiceStateToString(status.CurrentState);
	if (status.ProcessId && status.ProcessId != m_Props.Id)
		text += std::format(L" (PID {})", status.ProcessId);
	return text;
}

CProcServicesPage::ServiceData CProcServicesPage::ReadService(std::wstring const& name, std::wstring const& displayName,
	ServiceStatusProcess const& status) const {
	ServiceData data{ name, displayName, StateText(status) };
	data.Status = status;
	auto& details = data.Details;
	auto add = [&](PCWSTR property, std::wstring value) {
		details.emplace_back(property, std::move(value));
	};
	add(L"Name", name);
	add(L"Display name", data.DisplayName);
	add(L"Description", ServiceManager::GetServiceDescription(name));
	add(L"State", data.State);
	add(L"Type", FormatHelper::ServiceTypeToString(status.Type));
	auto config = ServiceManager::GetServiceConfiguration(name);
	if (config) {
		data.StartType = FormatHelper::ServiceStartTypeToString(*config);
		add(L"Start type", data.StartType);
		add(L"Error control", FormatHelper::ServiceErrorControlToString(config->ErrorControl));
		add(L"Command line", config->BinaryPathName);
	}
	data.Dll = ServiceManager::GetServiceDll(name, status.Type);
	if (!data.Dll.empty()) {
		add(L"Service DLL", data.Dll);
		// on the sheet's thread: GetCachedVersionInfo belongs to the UI thread
		auto version = WinSys::FileVersion::Read(data.Dll);
		if (!version.Description.empty())
			add(L"DLL description", version.Description);
		if (!version.Company.empty())
			add(L"DLL company", version.Company);
		if (!version.Version.empty())
			add(L"DLL version", version.Version);
	}
	if (config) {
		add(L"Account", config->AccountName);
		if (!config->LoadOrderGroup.empty())
			add(L"Load order group", config->LoadOrderGroup);
		add(L"Dependencies", config->Dependencies.empty() ? L"None" : Join(config->Dependencies, [](auto& d) { return d; }));
	}
	add(L"Controls accepted", ControlsAcceptedToString(status.ControlsAccepted));
	if (auto service = WinSys::Service::Open(name, ServiceAccessMask::QueryConfig)) {
		add(L"SID type", SidTypeToString(service->GetSidType()));
		auto privileges = service->GetRequiredPrivileges();
		add(L"Required privileges", privileges.empty() ? L"Default" : Join(privileges, [](auto& p) { return p; }));
		// triggers of the same type and action (different subtypes) shown once, with their count
		std::vector<std::pair<std::wstring, int>> triggers;
		for (auto& t : service->GetTriggerDetails()) {
			auto text = FormatHelper::ServiceTriggerToString(t.Type, t.Action);
			if (auto it = std::ranges::find(triggers, text, &std::pair<std::wstring, int>::first); it != triggers.end())
				it->second++;
			else
				triggers.emplace_back(std::move(text), 1);
		}
		if (!triggers.empty())
			add(L"Triggers", Join(triggers, [](auto& t) { return t.second == 1 ? t.first : std::format(L"{} ({})", t.first, t.second); }));
	}
	add(L"Service SID", ServiceManager::GetServiceSid(name.c_str()).AsString());
	return data;
}

void CProcServicesPage::ShowDetails(int index) {
	m_Details.SetRedraw(FALSE);
	m_Details.DeleteAllItems();
	if (index >= 0 && index < (int)m_Services.size()) {
		int row = 0;
		for (auto& [property, value] : m_Services[index].Details) {
			m_Details.InsertItem(row, property.c_str());
			m_Details.SetItemText(row++, 1, value.c_str());
		}
		SetDlgItemText(IDC_SVC_DETAILS_LABEL, std::format(L"{}:", m_Services[index].DisplayName).c_str());
	}
	else {
		SetDlgItemText(IDC_SVC_DETAILS_LABEL, L"Details:");
	}
	m_Details.SetRedraw(TRUE);
	UpdateButtons();
}

int CProcServicesPage::GetSelected() const {
	int index = m_List.GetNextItem(-1, LVNI_SELECTED);
	return index < (int)m_Services.size() ? index : -1;
}

void CProcServicesPage::UpdateButtons() {
	int index = GetSelected();
	auto status = index < 0 ? nullptr : &m_Services[index].Status;
	// nothing while a stop's steps run
	bool busy = m_Control.IsBusy();
	GetDlgItem(IDC_SVC_START).EnableWindow(!busy && status && status->CurrentState == ServiceState::Stopped);
	GetDlgItem(IDC_SVC_STOP).EnableWindow(!busy && status && status->CurrentState == ServiceState::Running
		&& (status->ControlsAccepted & ServiceControlsAccepted::Stop) == ServiceControlsAccepted::Stop);
}

bool CProcServicesPage::Control(ServiceAccessMask access, PCWSTR operation, std::function<bool(WinSys::Service&)> const& action) {
	int index = GetSelected();
	if (index < 0)
		return false;
	auto service = WinSys::Service::Open(m_Services[index].Name, access);
	if (!service || !action(*service)) {
		auto error = ::GetLastError();
		auto text = std::format(L"{} {} failed", operation, m_Services[index].Name);
		if (error)
			text += L": " + Helpers::GetErrorText(error);
		if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
			text += L"\nControlling most services requires running as administrator (File > Run as Administrator).";
		AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}
	// the service is pending now; the states are polled until it isn't
	UpdateStates();
	m_Polls = MaxPolls;
	SetTimer(PollTimerId, PollInterval);
	return true;
}

LRESULT CProcServicesPage::OnStart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Control(ServiceAccessMask::Start | ServiceAccessMask::QueryStatus, L"Starting", [](auto& svc) { return svc.Start(); });
	return 0;
}

LRESULT CProcServicesPage::OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int index = GetSelected();
	if (index >= 0 && m_Control.Plan(m_hWnd, m_Services[index].Name, m_Services[index].DisplayName, false))
		RunSteps();
	return 0;
}

void CProcServicesPage::RunSteps() {
	bool more = m_Control.Run(m_hWnd);
	if (more) {
		UpdateStates();
		SetTimer(StepTimerId, 250);
		return;
	}
	KillTimer(StepTimerId);
	int index = GetSelected();
	Load(index >= 0 ? m_Services[index].Name : L"");
}

bool CProcServicesPage::UpdateStates() {
	bool pending = false;
	for (int i = 0; i < (int)m_Services.size(); i++) {
		auto& data = m_Services[i];
		data.Status = ServiceManager::GetServiceStatus(data.Name);
		pending |= IsPending(data.Status.CurrentState);
		auto state = StateText(data.Status);
		if (state == data.State)
			continue;
		data.State = std::move(state);
		m_List.SetItemText(i, (int)Column::State, data.State.c_str());
		if (auto row = std::ranges::find(data.Details, L"State", &std::pair<std::wstring, std::wstring>::first); row != data.Details.end())
			row->second = data.State;
	}
	if (int index = GetSelected(); index >= 0)
		ShowDetails(index);
	else
		UpdateButtons();
	return pending;
}

LRESULT CProcServicesPage::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id == StepTimerId) {
		RunSteps();
		return 0;
	}
	if (id != PollTimerId) {
		bHandled = FALSE;
		return 0;
	}
	if (UpdateStates() && --m_Polls > 0)
		return 0;
	KillTimer(PollTimerId);
	// all of it again: a started service has a new process, perhaps another one
	int index = GetSelected();
	Load(index >= 0 ? m_Services[index].Name : L"");
	return 0;
}
