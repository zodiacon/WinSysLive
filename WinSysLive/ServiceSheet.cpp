#include "pch.h"
#include "ServiceSheet.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

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
	if (auto service = WinSys::Service::Open(name, ServiceAccessMask::QueryConfig))
		m_Data.Failure = service->GetFailureActions();

	m_Title = std::format(L"{} Properties", m_Data.Config.DisplayName.empty() ? name : m_Data.Config.DisplayName);
	SetTitle(m_Title.c_str());
	m_psh.dwFlags |= PSH_NOCONTEXTHELP;
	AddPage(m_General);
	AddPage(m_LogOn);
	AddPage(m_Recovery);
}

void CServiceSheet::OnSheetInitialized() {
	CenterWindow(GetParent());
}

LRESULT CServiceGeneralPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& config = m_Data.Config;
	SetDlgItemText(IDC_SVC_NAME, m_Data.Name.c_str());
	SetDlgItemText(IDC_SVC_DISPLAYNAME, config.DisplayName.c_str());
	SetDlgItemText(IDC_SVC_DESCRIPTION, m_Data.Description.c_str());
	SetDlgItemText(IDC_SVC_PATH, config.BinaryPathName.c_str());
	SetDlgItemText(IDC_SVC_STATE, FormatHelper::ServiceStateToString(m_Data.Status.CurrentState));

	CComboBox start(GetDlgItem(IDC_SVC_STARTTYPE));
	for (auto& t : s_StartTypes)
		start.AddString(t.Name);
	auto it = std::ranges::find_if(s_StartTypes, [&](auto& t) {
		return t.Type == config.StartType && t.Delayed == (config.StartType == ServiceStartType::Auto && config.DelayedAutoStart);
		});
	start.SetCurSel(it == std::end(s_StartTypes) ? -1 : (int)(it - std::begin(s_StartTypes)));

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
		auto& t = s_StartTypes[start];
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
		if (!service->SetDelayedAutoStart(s_StartTypes[start].Delayed)) {
			ShowError(L"Changing the delayed start", ::GetLastError());
			return PSNRET_INVALID;
		}
		config.DelayedAutoStart = s_StartTypes[start].Delayed;
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
