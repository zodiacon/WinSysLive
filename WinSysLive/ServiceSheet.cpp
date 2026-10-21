#include "pch.h"
#include "ServiceSheet.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <span>

using namespace WinSys;

namespace {
	// the start types a Win32 service can have, in the combo box's order
	struct StartType {
		PCWSTR Name;
		ServiceStartType Type;
		bool Delayed;
	};
	const StartType s_StartTypes[] = {
		{ L"Automatic (Delayed Start)", ServiceStartType::Auto, true },
		{ L"Automatic", ServiceStartType::Auto, false },
		{ L"Manual", ServiceStartType::Demand, false },
		{ L"Disabled", ServiceStartType::Disabled, false },
	};
	// a driver's: loaded by the boot loader or the kernel's initialization too, and no delayed start
	const StartType s_DriverStartTypes[] = {
		{ L"Boot", ServiceStartType::Boot, false },
		{ L"System", ServiceStartType::System, false },
		{ L"Automatic", ServiceStartType::Auto, false },
		{ L"Manual", ServiceStartType::Demand, false },
		{ L"Disabled", ServiceStartType::Disabled, false },
	};

	std::span<StartType const> StartTypes(bool driver) {
		if (driver)
			return s_DriverStartTypes;
		return s_StartTypes;
	}

	// the built-in accounts, in the radio buttons' order (IDC_SVC_LOCALSYSTEM...); the name ChangeServiceConfig takes
	PCWSTR const s_Accounts[] = { L"LocalSystem", L"NT AUTHORITY\\LocalService", L"NT AUTHORITY\\NetworkService" };

	// the failure actions, in the combo boxes' order
	struct Action {
		PCWSTR Name;
		ServiceFailureActionType Type;
	};
	const Action s_Actions[] = {
		{ L"Take No Action", ServiceFailureActionType::None },
		{ L"Restart the Service", ServiceFailureActionType::Restart },
		{ L"Run a Program", ServiceFailureActionType::RunCommand },
		{ L"Restart the Computer", ServiceFailureActionType::Reboot },
	};
	UINT const s_ActionCombos[] = { IDC_SVC_FIRST, IDC_SVC_SECOND, IDC_SVC_SUBSEQUENT };

	std::wstring GetText(CWindow win) {
		CString text;
		win.GetWindowText(text);
		return std::wstring(text);
	}

	// a whole number of minutes or days in an edit; nothing if it isn't one
	std::optional<uint32_t> GetNumber(CWindow edit) {
		auto text = GetText(edit);
		if (text.empty() || !std::ranges::all_of(text, iswdigit) || text.size() > 6)
			return std::nullopt;
		return (uint32_t)std::stoul(text);
	}
}

template<typename T>
std::unique_ptr<WinSys::Service> CServicePageBase<T>::OpenForChange(ServiceAccessMask extra) {
	auto service = WinSys::Service::Open(m_Data.Name, ServiceAccessMask::ChangeConfig | ServiceAccessMask::QueryConfig | extra);
	if (!service)
		ShowError(L"Opening the service", ::GetLastError());
	return service;
}

template<typename T>
void CServicePageBase<T>::ShowError(PCWSTR operation, DWORD error) {
	auto text = std::format(L"{} failed: {}", operation, Helpers::GetErrorText(error));
	if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
		text += L"\nChanging a service's configuration requires running as administrator (File > Run as Administrator).";
	AtlMessageBox(this->m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

CServiceSheet::CServiceSheet(std::wstring const& name) {
	m_Data.Name = name;
	if (auto config = ServiceManager::GetServiceConfiguration(name))
		m_Data.Config = *config;
	m_Data.Description = ServiceManager::GetServiceDescription(name);
	m_Data.Status = ServiceManager::GetServiceStatus(name);
	if (auto service = WinSys::Service::Open(name, ServiceAccessMask::QueryConfig)) {
		m_Data.Failure = service->GetFailureActions();
		m_Data.Triggers = service->GetTriggerDetails();
	}

	m_Title = std::format(L"{} Properties", m_Data.Config.DisplayName.empty() ? name : m_Data.Config.DisplayName);
	SetTitle(m_Title.c_str());
	m_psh.dwFlags |= PSH_NOCONTEXTHELP;
	// a driver has no account and no recovery actions
	m_Data.Driver = ((DWORD)m_Data.Config.Type & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER)) != 0;
	AddPage(m_General);
	if (!m_Data.Driver) {
		AddPage(m_LogOn);
		AddPage(m_Recovery);
	}
	AddPage(m_Dependencies);
	if (!m_Data.Triggers.empty())
		AddPage(m_Triggers);
	m_Security = CServiceSecurityInfo::Open(name, m_Data.Config.DisplayName.empty() ? name : m_Data.Config.DisplayName);
	if (auto page = ::CreateSecurityPage(m_Security))
		AddPage(page);
}

CServiceSheet::~CServiceSheet() {
	if (m_Security)
		m_Security->Release();
}

void CServiceSheet::OnSheetInitialized() {
	CenterWindow(GetParent());
	// the ACL editor's dialogs, when they come
	m_AclTheming.Install(m_hWnd);
}

LRESULT CServiceGeneralPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& config = m_Data.Config;
	SetDlgItemText(IDC_SVC_NAME, m_Data.Name.c_str());
	SetDlgItemText(IDC_SVC_DISPLAYNAME, config.DisplayName.c_str());
	SetDlgItemText(IDC_SVC_DESCRIPTION, m_Data.Description.c_str());
	SetDlgItemText(IDC_SVC_PATH, config.BinaryPathName.c_str());
	SetDlgItemText(IDC_SVC_STATE, FormatHelper::ServiceStateToString(m_Data.Status.CurrentState));

	CComboBox start(GetDlgItem(IDC_SVC_STARTTYPE));
	auto types = StartTypes(m_Data.Driver);
	for (auto& t : types)
		start.AddString(t.Name);
	auto it = std::ranges::find_if(types, [&](auto& t) {
		return t.Type == config.StartType && t.Delayed == (config.StartType == ServiceStartType::Auto && config.DelayedAutoStart);
		});
	start.SetCurSel(it == types.end() ? -1 : (int)(it - types.begin()));

	CComboBox error(GetDlgItem(IDC_SVC_ERRORCONTROL));
	for (auto name : { L"Ignore", L"Normal", L"Severe", L"Critical" })
		error.AddString(name);
	error.SetCurSel((int)config.ErrorControl);

	m_Initialized = true;
	return TRUE;
}

LRESULT CServiceGeneralPage::OnChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_Initialized)
		Changed();
	return 0;
}

int CServiceGeneralPage::OnApply() {
	if (!m_Changed)
		return PSNRET_NOERROR;
	auto& config = m_Data.Config;
	ServiceConfigChange change;
	auto display = GetText(GetDlgItem(IDC_SVC_DISPLAYNAME));
	if (display != config.DisplayName)
		change.DisplayName = display;
	auto path = GetText(GetDlgItem(IDC_SVC_PATH));
	if (path != config.BinaryPathName)
		change.BinaryPath = path;
	int start = CComboBox(GetDlgItem(IDC_SVC_STARTTYPE)).GetCurSel();
	bool delayedChanged = false;
	if (start >= 0) {
		auto& t = StartTypes(m_Data.Driver)[start];
		if (t.Type != config.StartType)
			change.StartType = t.Type;
		delayedChanged = t.Type == ServiceStartType::Auto && t.Delayed != config.DelayedAutoStart;
	}
	int error = CComboBox(GetDlgItem(IDC_SVC_ERRORCONTROL)).GetCurSel();
	if (error >= 0 && (ServiceErrorControl)error != config.ErrorControl)
		change.ErrorControl = (ServiceErrorControl)error;
	auto description = GetText(GetDlgItem(IDC_SVC_DESCRIPTION));

	auto service = OpenForChange();
	if (!service)
		return PSNRET_INVALID;
	if ((change.DisplayName || change.BinaryPath || change.StartType || change.ErrorControl) && !service->ChangeConfig(change)) {
		ShowError(L"Changing the configuration", ::GetLastError());
		return PSNRET_INVALID;
	}
	if (change.DisplayName)
		config.DisplayName = *change.DisplayName;
	if (change.BinaryPath)
		config.BinaryPathName = *change.BinaryPath;
	if (change.StartType)
		config.StartType = *change.StartType;
	if (change.ErrorControl)
		config.ErrorControl = *change.ErrorControl;
	if (delayedChanged) {
		if (!service->SetDelayedAutoStart(StartTypes(m_Data.Driver)[start].Delayed)) {
			ShowError(L"Changing the delayed start", ::GetLastError());
			return PSNRET_INVALID;
		}
		config.DelayedAutoStart = StartTypes(m_Data.Driver)[start].Delayed;
	}
	if (description != m_Data.Description) {
		if (!service->SetDescription(description)) {
			ShowError(L"Changing the description", ::GetLastError());
			return PSNRET_INVALID;
		}
		m_Data.Description = description;
	}
	m_Data.Applied = true;
	m_Changed = false;
	return PSNRET_NOERROR;
}

LRESULT CServiceLogOnPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& account = m_Data.Config.AccountName;
	// a per-user service runs as its user: there's nothing to choose
	m_ReadOnly = (m_Data.Config.Type & (ServiceType::UserService | ServiceType::UserServiceInstance)) != ServiceType(0);
	int kind = 3;
	for (int i = 0; i < _countof(s_Accounts); i++)
		if (_wcsicmp(account.c_str(), s_Accounts[i]) == 0 || (i == 0 && account.empty()))
			kind = i;
	CheckRadioButton(IDC_SVC_LOCALSYSTEM, IDC_SVC_THISACCOUNT, IDC_SVC_LOCALSYSTEM + kind);
	if (kind == 3)
		SetDlgItemText(IDC_SVC_ACCOUNT, account.c_str());
	SetDlgItemText(IDC_SVC_LOGON_NOTE, m_ReadOnly ? L"A per-user service runs as the user it's for."
		: L"The passwords are left as they are unless they're typed again (a built-in account has none).");
	UpdateControls();
	m_Initialized = true;
	return TRUE;
}

void CServiceLogOnPage::UpdateControls() {
	bool account = IsDlgButtonChecked(IDC_SVC_THISACCOUNT) == BST_CHECKED;
	for (UINT id : { IDC_SVC_ACCOUNT, IDC_SVC_PASSWORD, IDC_SVC_CONFIRM })
		GetDlgItem(id).EnableWindow(account && !m_ReadOnly);
	for (UINT id = IDC_SVC_LOCALSYSTEM; id <= IDC_SVC_THISACCOUNT; id++)
		GetDlgItem(id).EnableWindow(!m_ReadOnly);
}

LRESULT CServiceLogOnPage::OnAccountKind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	UpdateControls();
	if (m_Initialized)
		Changed();
	return 0;
}

LRESULT CServiceLogOnPage::OnChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_Initialized)
		Changed();
	return 0;
}

int CServiceLogOnPage::OnApply() {
	if (!m_Changed || m_ReadOnly)
		return PSNRET_NOERROR;
	ServiceConfigChange change;
	int kind = 0;
	while (kind < 3 && IsDlgButtonChecked(IDC_SVC_LOCALSYSTEM + kind) != BST_CHECKED)
		kind++;
	if (kind < 3) {
		change.Account = s_Accounts[kind];
		change.Password = L"";
	}
	else {
		auto account = GetText(GetDlgItem(IDC_SVC_ACCOUNT));
		auto password = GetText(GetDlgItem(IDC_SVC_PASSWORD));
		if (account.empty()) {
			AtlMessageBox(m_hWnd, L"Type the account's name (\"DOMAIN\\user\", or \".\\user\" for a local account).", IDR_MAINFRAME, MB_ICONWARNING);
			GotoDlgCtrl(GetDlgItem(IDC_SVC_ACCOUNT));
			return PSNRET_INVALID_NOCHANGEPAGE;
		}
		if (password != GetText(GetDlgItem(IDC_SVC_CONFIRM))) {
			AtlMessageBox(m_hWnd, L"The passwords don't match.", IDR_MAINFRAME, MB_ICONWARNING);
			GotoDlgCtrl(GetDlgItem(IDC_SVC_PASSWORD));
			return PSNRET_INVALID_NOCHANGEPAGE;
		}
		change.Account = account;
		// typed (or the account is another): the password goes with it; else it stays
		if (!password.empty() || _wcsicmp(account.c_str(), m_Data.Config.AccountName.c_str()) != 0)
			change.Password = password;
	}
	if (_wcsicmp(change.Account->c_str(), m_Data.Config.AccountName.c_str()) == 0 && !change.Password) {
		m_Changed = false;
		return PSNRET_NOERROR;
	}

	auto service = OpenForChange();
	if (!service)
		return PSNRET_INVALID;
	if (!service->ChangeConfig(change)) {
		ShowError(L"Changing the account", ::GetLastError());
		return PSNRET_INVALID;
	}
	m_Data.Config.AccountName = *change.Account;
	SetDlgItemText(IDC_SVC_PASSWORD, L"");
	SetDlgItemText(IDC_SVC_CONFIRM, L"");
	m_Data.Applied = true;
	m_Changed = false;
	return PSNRET_NOERROR;
}

LRESULT CServiceRecoveryPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	ServiceFailureActions failure;
	if (m_Data.Failure)
		failure = *m_Data.Failure;
	else
		SetDlgItemText(IDC_SVC_RECOVERY_NOTE, L"The failure actions can't be read.");

	uint32_t restartDelay = 60000, rebootDelay = 60000;
	for (int i = 0; i < _countof(s_ActionCombos); i++) {
		CComboBox combo(GetDlgItem(s_ActionCombos[i]));
		for (auto& a : s_Actions)
			combo.AddString(a.Name);
		// the last action repeats for the failures after it
		auto type = failure.Actions.empty() ? ServiceFailureActionType::None
			: failure.Actions[(std::min)((size_t)i, failure.Actions.size() - 1)].Type;
		auto it = std::ranges::find(s_Actions, type, &Action::Type);
		combo.SetCurSel(it == std::end(s_Actions) ? 0 : (int)(it - std::begin(s_Actions)));
	}
	// the delays, from the first action of each kind (one each, as services.msc has them)
	if (auto it = std::ranges::find(failure.Actions, ServiceFailureActionType::Restart, &ServiceFailureAction::Type); it != failure.Actions.end())
		restartDelay = it->Delay;
	if (auto it = std::ranges::find(failure.Actions, ServiceFailureActionType::Reboot, &ServiceFailureAction::Type); it != failure.Actions.end())
		rebootDelay = it->Delay;
	SetDlgItemInt(IDC_SVC_RESTARTMIN, restartDelay / 60000, FALSE);
	SetDlgItemInt(IDC_SVC_REBOOTMIN, rebootDelay / 60000, FALSE);
	// never: empty
	if (failure.ResetPeriod != INFINITE)
		SetDlgItemInt(IDC_SVC_RESETDAYS, failure.ResetPeriod / (24 * 3600), FALSE);
	SetDlgItemText(IDC_SVC_COMMAND, failure.Command.c_str());
	CheckDlgButton(IDC_SVC_NONCRASH, failure.OnNonCrashFailures ? BST_CHECKED : BST_UNCHECKED);
	auto keep = [&](RoundedValue& value, uint32_t exact) {
		value.Value = exact;
		value.Text = GetText(GetDlgItem(value.Id));
	};
	keep(m_Reset, failure.ResetPeriod);
	keep(m_RestartDelay, restartDelay);
	keep(m_RebootDelay, rebootDelay);
	UpdateControls();
	m_Initialized = true;
	return TRUE;
}

ServiceFailureActionType CServiceRecoveryPage::GetAction(int index) const {
	int sel = CComboBox(GetDlgItem(s_ActionCombos[index])).GetCurSel();
	return sel < 0 ? ServiceFailureActionType::None : s_Actions[sel].Type;
}

void CServiceRecoveryPage::UpdateControls() {
	auto uses = [&](ServiceFailureActionType type) {
		return GetAction(0) == type || GetAction(1) == type || GetAction(2) == type;
	};
	GetDlgItem(IDC_SVC_RESTARTMIN).EnableWindow(uses(ServiceFailureActionType::Restart));
	GetDlgItem(IDC_SVC_COMMAND).EnableWindow(uses(ServiceFailureActionType::RunCommand));
	GetDlgItem(IDC_SVC_REBOOTMIN).EnableWindow(uses(ServiceFailureActionType::Reboot));
}

LRESULT CServiceRecoveryPage::OnChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	UpdateControls();
	if (m_Initialized)
		Changed();
	return 0;
}

std::optional<uint32_t> CServiceRecoveryPage::GetValue(RoundedValue const& value, uint32_t unit, PCWSTR what) {
	auto edit = GetDlgItem(value.Id);
	if (GetText(edit) == value.Text)
		return value.Value;
	// below INFINITE (0xFFFFFFFF), which means never for the reset period
	auto max = (INFINITE - 1) / unit;
	auto number = GetNumber(edit);
	if (!number || *number > max) {
		AtlMessageBox(m_hWnd, std::format(L"Type a whole number of {}, up to {}.", what, max).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		GotoDlgCtrl(edit);
		return std::nullopt;
	}
	return *number * unit;
}

int CServiceRecoveryPage::OnApply() {
	if (!m_Changed)
		return PSNRET_NOERROR;

	ServiceFailureActions failure;
	if (m_Data.Failure)
		failure.RebootMessage = m_Data.Failure->RebootMessage;
	auto resetText = GetText(GetDlgItem(IDC_SVC_RESETDAYS));
	if (resetText.empty())
		failure.ResetPeriod = INFINITE;
	else if (auto period = GetValue(m_Reset, 24 * 3600, L"days"))
		failure.ResetPeriod = *period;
	else
		return PSNRET_INVALID_NOCHANGEPAGE;
	uint32_t restart = 0, reboot = 0;
	bool restarts = false;
	for (int i = 0; i < 3; i++) {
		auto type = GetAction(i);
		if (type == ServiceFailureActionType::Restart && !restarts) {
			auto delay = GetValue(m_RestartDelay, 60000, L"minutes");
			if (!delay)
				return PSNRET_INVALID_NOCHANGEPAGE;
			restart = *delay;
			restarts = true;
		}
		if (type == ServiceFailureActionType::Reboot && reboot == 0) {
			auto delay = GetValue(m_RebootDelay, 60000, L"minutes");
			if (!delay)
				return PSNRET_INVALID_NOCHANGEPAGE;
			reboot = (std::max)(*delay, 1u);
		}
	}
	for (int i = 0; i < 3; i++) {
		auto type = GetAction(i);
		failure.Actions.push_back({ type, type == ServiceFailureActionType::Restart ? restart
			: type == ServiceFailureActionType::Reboot ? reboot : 0 });
	}
	// none at all: no actions
	if (std::ranges::all_of(failure.Actions, [](auto& a) { return a.Type == ServiceFailureActionType::None; }))
		failure.Actions.clear();
	failure.Command = GetText(GetDlgItem(IDC_SVC_COMMAND));
	failure.OnNonCrashFailures = IsDlgButtonChecked(IDC_SVC_NONCRASH) == BST_CHECKED;

	// a restart action can be set only through a handle that can start the service
	auto service = OpenForChange(restarts ? ServiceAccessMask::Start : ServiceAccessMask::None);
	if (!service)
		return PSNRET_INVALID;
	if (!service->SetFailureActions(failure)) {
		ShowError(L"Changing the recovery actions", ::GetLastError());
		return PSNRET_INVALID;
	}
	m_Data.Failure = failure;
	// what was written is what the fields stand for now
	auto written = [&](RoundedValue& value, uint32_t exact) {
		value.Value = exact;
		value.Text = GetText(GetDlgItem(value.Id));
	};
	if (failure.ResetPeriod != INFINITE)
		written(m_Reset, failure.ResetPeriod);
	if (restarts)
		written(m_RestartDelay, restart);
	if (reboot)
		written(m_RebootDelay, reboot);
	m_Data.Applied = true;
	m_Changed = false;
	return PSNRET_NOERROR;
}

namespace {
	// the services depending on a service directly (or on its load order group), from its dependents (direct or not)
	std::vector<ServiceDependent> DirectDependents(std::wstring const& name) {
		std::vector<ServiceDependent> direct;
		auto service = WinSys::Service::Open(name, ServiceAccessMask::EnumerateDependents);
		if (!service)
			return direct;
		auto config = ServiceManager::GetServiceConfiguration(name);
		auto group = config && !config->LoadOrderGroup.empty() ? SC_GROUP_IDENTIFIER + config->LoadOrderGroup : std::wstring();
		for (auto& d : service->GetDependents()) {
			auto dconfig = ServiceManager::GetServiceConfiguration(d.Name);
			if (dconfig && std::ranges::any_of(dconfig->Dependencies, [&](auto& dep) {
				return _wcsicmp(dep.c_str(), name.c_str()) == 0 || (!group.empty() && _wcsicmp(dep.c_str(), group.c_str()) == 0);
				}))
				direct.push_back(d);
		}
		return direct;
	}

	bool HasDependents(std::wstring const& name) {
		auto service = WinSys::Service::Open(name, ServiceAccessMask::EnumerateDependents);
		return service && !service->GetDependents().empty();
	}
}

LRESULT CServiceDependenciesPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	for (auto [id, dependents] : { std::pair{ IDC_SVC_DEPENDSON, false }, std::pair{ IDC_SVC_DEPENDENTS, true } }) {
		CTreeViewCtrl tree(GetDlgItem(id));
		if (!AddChildren(tree, TVI_ROOT, m_Data.Name, dependents))
			tree.InsertItem(dependents ? L"<No dependents>" : L"<No dependencies>", TVI_ROOT, TVI_LAST);
	}
	return TRUE;
}

LRESULT CServiceDependenciesPage::OnItemExpanding(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto tv = (NMTREEVIEW*)hdr;
	if (tv->action != TVE_EXPAND)
		return FALSE;
	CTreeViewCtrl tree(hdr->hwndFrom);
	auto index = (size_t)tree.GetItemData(tv->itemNew.hItem);
	if (index >= m_Nodes.size() || m_Nodes[index].Filled)
		return FALSE;
	m_Nodes[index].Filled = true;
	// copied: adding nodes moves them
	auto node = m_Nodes[index];
	if (!AddChildren(tree, tv->itemNew.hItem, node.Name, node.Dependents, node.Group)) {
		TVITEM item{ TVIF_CHILDREN, tv->itemNew.hItem };
		item.cChildren = 0;
		tree.SetItem(&item);
	}
	return FALSE;
}

bool CServiceDependenciesPage::AddChildren(CTreeViewCtrl tree, HTREEITEM parent, std::wstring const& name, bool dependents, bool group) {
	bool any = false;
	if (group) {
		for (auto& member : GroupMembers(name)) {
			AddNode(tree, parent, member, false, false);
			any = true;
		}
		return any;
	}
	if (dependents) {
		auto direct = DirectDependents(name);
		// the SCM's order is the stop order
		std::ranges::sort(direct, [](auto& a, auto& b) { return _wcsicmp(a.DisplayName.c_str(), b.DisplayName.c_str()) < 0; });
		for (auto& d : direct) {
			AddNode(tree, parent, d.Name, false, true);
			any = true;
		}
		return any;
	}
	auto config = ServiceManager::GetServiceConfiguration(name);
	if (!config)
		return false;
	for (auto& dep : config->Dependencies) {
		if (dep.empty())
			continue;
		if (dep[0] == SC_GROUP_IDENTIFIER)
			AddNode(tree, parent, dep.substr(1), true, false);
		else
			AddNode(tree, parent, dep, false, false);
		any = true;
	}
	return any;
}

void CServiceDependenciesPage::AddNode(CTreeViewCtrl tree, HTREEITEM parent, std::wstring const& name, bool group, bool dependents) {
	std::wstring text;
	bool children = false;
	bool installed = false;
	if (group) {
		text = std::format(L"{} (load order group)", name);
		// its services are found when it's expanded
		children = true;
	}
	else if (auto config = ServiceManager::GetServiceConfiguration(name); !config)
		text = std::format(L"{} (not installed)", name);
	else {
		auto& display = config->DisplayName;
		text = display.empty() || _wcsicmp(display.c_str(), name.c_str()) == 0 ? name : std::format(L"{} ({})", display, name);
		text += std::format(L": {}", FormatHelper::ServiceStateToString(ServiceManager::GetServiceState(name)));
		installed = true;
		if (InPath(tree, parent, name))
			text += L" (a cycle)";
		else
			children = dependents ? HasDependents(name) : !config->Dependencies.empty();
	}
	m_Nodes.push_back({ name, group, dependents, installed });
	TVINSERTSTRUCT insert{ parent, TVI_LAST };
	insert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
	insert.item.pszText = text.data();
	insert.item.cChildren = children ? 1 : 0;
	insert.item.lParam = (LPARAM)(m_Nodes.size() - 1);
	tree.InsertItem(&insert);
}

bool CServiceDependenciesPage::InPath(CTreeViewCtrl tree, HTREEITEM item, std::wstring const& name) const {
	if (_wcsicmp(name.c_str(), m_Data.Name.c_str()) == 0)
		return true;
	for (; item && item != TVI_ROOT; item = tree.GetParentItem(item)) {
		auto index = (size_t)tree.GetItemData(item);
		if (index < m_Nodes.size() && !m_Nodes[index].Group && _wcsicmp(m_Nodes[index].Name.c_str(), name.c_str()) == 0)
			return true;
	}
	return false;
}

std::vector<std::wstring> const& CServiceDependenciesPage::GroupMembers(std::wstring const& group) {
	if (!m_Groups) {
		m_Groups.emplace();
		for (auto& svc : ServiceManager::EnumServices(ServiceEnumType::AllServices | ServiceEnumType::AllDrivers)) {
			auto config = ServiceManager::GetServiceConfiguration(svc.GetName());
			if (!config || config->LoadOrderGroup.empty())
				continue;
			auto key = config->LoadOrderGroup;
			std::ranges::transform(key, key.begin(), ::towlower);
			(*m_Groups)[key].push_back(svc.GetName());
		}
		for (auto& [key, members] : *m_Groups)
			std::ranges::sort(members, [](auto& a, auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
	}
	auto key = group;
	std::ranges::transform(key, key.begin(), ::towlower);
	static std::vector<std::wstring> const none;
	auto it = m_Groups->find(key);
	return it == m_Groups->end() ? none : it->second;
}

CServiceDependenciesPage::Node const* CServiceDependenciesPage::ServiceAtCursor(CTreeViewCtrl tree, HTREEITEM* item) const {
	// the cursor's, which is where the click was
	POINT pt;
	::GetCursorPos(&pt);
	tree.ScreenToClient(&pt);
	UINT flags = 0;
	auto hItem = tree.HitTest(pt, &flags);
	if (!hItem || (flags & TVHT_ONITEM) == 0)
		return nullptr;
	if (item)
		*item = hItem;
	auto index = (size_t)tree.GetItemData(hItem);
	return index < m_Nodes.size() && m_Nodes[index].Installed ? &m_Nodes[index] : nullptr;
}

LRESULT CServiceDependenciesPage::OnTreeDoubleClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto node = ServiceAtCursor(hdr->hwndFrom);
	if (!node)
		return FALSE;
	auto name = node->Name;
	CServiceSheet sheet(name);
	sheet.DoModal(GetParent());
	// no expanding or collapsing too
	return TRUE;
}

LRESULT CServiceDependenciesPage::OnTreeRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	CTreeViewCtrl tree(hdr->hwndFrom);
	HTREEITEM item = nullptr;
	auto node = ServiceAtCursor(tree, &item);
	if (!node)
		return TRUE;
	auto name = node->Name;
	tree.SelectItem(item);
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_VIEW_PROPERTIES, L"&Properties");
	POINT pt;
	::GetCursorPos(&pt);
	if (menu.TrackPopupMenuEx(TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd) == ID_VIEW_PROPERTIES) {
		CServiceSheet sheet(name);
		sheet.DoModal(GetParent());
	}
	return TRUE;
}

namespace {
	// the subtypes the trigger types define (winsvc.h's GUIDs; INITGUID is off, so they're not linked from there)
	struct KnownSubtype {
		GUID Id;
		PCWSTR Name;
	};
	const KnownSubtype s_Subtypes[] = {
		{ { 0x4f27f2de, 0x14e2, 0x430b, { 0xa5, 0x49, 0x7c, 0xd4, 0x8c, 0xbc, 0x82, 0x45 } }, L"First IP address arrival" },
		{ { 0xcc4ba62a, 0x162e, 0x4648, { 0x84, 0x7a, 0xb6, 0xbd, 0xf9, 0x93, 0xe3, 0x35 } }, L"Last IP address removal" },
		{ { 0x1ce20aba, 0x9851, 0x4421, { 0x94, 0x30, 0x1d, 0xde, 0xb7, 0x66, 0xe8, 0x09 } }, L"Domain joined" },
		{ { 0xddaf516e, 0x58c2, 0x4866, { 0x95, 0x74, 0xc3, 0xb6, 0x15, 0xd4, 0x2e, 0xa1 } }, L"Domain left" },
		{ { 0xb7569e07, 0x8421, 0x4ee0, { 0xad, 0x10, 0x86, 0x91, 0x5a, 0xfd, 0xad, 0x09 } }, L"Firewall port opened" },
		{ { 0xa144ed38, 0x8e12, 0x4de4, { 0x9d, 0x96, 0xe6, 0x47, 0x40, 0xb1, 0xa5, 0x24 } }, L"Firewall port closed" },
		{ { 0x659fcae6, 0x5bdb, 0x4da9, { 0xb1, 0xff, 0xca, 0x2a, 0x17, 0x8d, 0x46, 0xe0 } }, L"Machine policy present" },
		{ { 0x54fb46c8, 0xf089, 0x464c, { 0xb1, 0xfd, 0x59, 0xd1, 0xb6, 0x2c, 0x3b, 0x50 } }, L"User policy present" },
		{ { 0xbc90d167, 0x9470, 0x4139, { 0xa9, 0xba, 0xbe, 0x0b, 0xbb, 0xf5, 0xb7, 0x4d } }, L"RPC interface" },
		{ { 0x1f81d131, 0x3fac, 0x4537, { 0x9e, 0x0c, 0x7e, 0x7b, 0x0c, 0x2f, 0x4b, 0x55 } }, L"Named pipe" },
	};

	std::wstring GuidText(GUID const& id) {
		WCHAR text[64];
		return ::StringFromGUID2(id, text, _countof(text)) ? text : L"";
	}

	std::wstring SubtypeText(ServiceTriggerDetails const& t) {
		if (auto it = std::ranges::find_if(s_Subtypes, [&](auto& s) { return s.Id == t.Subtype; }); it != std::end(s_Subtypes))
			return it->Name;
		auto guid = GuidText(t.Subtype);
		switch (t.Type) {
			case ServiceTriggerType::Custom:
				// an ETW provider
				if (auto name = WinSys::Service::GetEventProviderName(t.Subtype); !name.empty())
					return std::format(L"{} {}", name, guid);
				break;
			case ServiceTriggerType::DeviceInterfaceArrival:
				return L"Device interface " + guid;
		}
		return guid;
	}

	PCWSTR DataTypeName(ServiceTriggerDataType type) {
		switch (type) {
			case ServiceTriggerDataType::Binary: return L"Binary";
			case ServiceTriggerDataType::String: return L"String";
			case ServiceTriggerDataType::Level: return L"Level";
			case ServiceTriggerDataType::KeywordAny: return L"Any keyword";
			case ServiceTriggerDataType::KeywordAll: return L"All keywords";
		}
		return L"Unknown";
	}

	// a data item's values: a string item's strings one each, the others one
	std::vector<std::wstring> DataValues(ServiceTriggerData const& item) {
		std::vector<std::wstring> values;
		auto& data = item.Data;
		switch (item.Type) {
			case ServiceTriggerDataType::String: {
				std::wstring_view text((PCWSTR)data.data(), data.size() / sizeof(WCHAR));
				for (size_t start = 0; start < text.size(); ) {
					auto end = text.find(L'\0', start);
					if (end == std::wstring_view::npos)
						end = text.size();
					if (end > start)
						values.emplace_back(text.substr(start, end - start));
					start = end + 1;
				}
				break;
			}
			case ServiceTriggerDataType::Level:
				if (!data.empty()) {
					PCWSTR const levels[] = { L"", L"Critical", L"Error", L"Warning", L"Information", L"Verbose" };
					values.push_back(data[0] < _countof(levels) ? std::format(L"{} ({})", data[0], levels[data[0]]) : std::to_wstring(data[0]));
				}
				break;
			case ServiceTriggerDataType::KeywordAny:
			case ServiceTriggerDataType::KeywordAll:
				if (data.size() >= sizeof(ULONGLONG))
					values.push_back(std::format(L"0x{:016X}", *(ULONGLONG const*)data.data()));
				break;
			default: {
				std::wstring hex;
				for (size_t i = 0; i < data.size() && i < 64; i++)
					hex += std::format(L"{}{:02X}", i ? L" " : L"", data[i]);
				if (data.size() > 64)
					hex += std::format(L" ... ({} bytes)", data.size());
				values.push_back(std::move(hex));
				break;
			}
		}
		return values;
	}

	void InitColumns(CListViewCtrl& list, HWND hList, std::initializer_list<std::pair<PCWSTR, int>> columns) {
		list.Attach(hList);
		list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		auto dpi = ::GetDpiForWindow(hList);
		int i = 0;
		for (auto& [name, width] : columns)
			list.InsertColumn(i++, name, LVCFMT_LEFT, ::MulDiv(width, dpi, 96));
	}
}

LRESULT CServiceTriggersPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	InitColumns(m_List, GetDlgItem(IDC_SVC_TRIGGERS), { { L"Action", 50 }, { L"Trigger", 110 }, { L"Subtype", 300 } });
	InitColumns(m_DataList, GetDlgItem(IDC_SVC_TRIGGERDATA), { { L"Type", 80 }, { L"Value", 380 } });
	int row = 0;
	for (auto& t : m_Data.Triggers) {
		// "Start: Device arrival"
		auto text = FormatHelper::ServiceTriggerToString(t.Type, t.Action);
		auto colon = text.find(L": ");
		m_List.InsertItem(row, text.substr(0, colon).c_str());
		m_List.SetItemText(row, 1, colon == std::wstring::npos ? L"" : text.substr(colon + 2).c_str());
		m_List.SetItemText(row, 2, SubtypeText(t).c_str());
		row++;
	}
	if (row)
		m_List.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	return TRUE;
}

LRESULT CServiceTriggersPage::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = (NMLISTVIEW*)hdr;
	if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
		ShowData(m_List.GetNextItem(-1, LVNI_SELECTED));
	return 0;
}

void CServiceTriggersPage::ShowData(int index) {
	m_DataList.DeleteAllItems();
	if (index < 0 || index >= (int)m_Data.Triggers.size())
		return;
	int row = 0;
	for (auto& item : m_Data.Triggers[index].Data)
		for (auto& value : DataValues(item)) {
			m_DataList.InsertItem(row, DataTypeName(item.Type));
			m_DataList.SetItemText(row++, 1, value.c_str());
		}
}
