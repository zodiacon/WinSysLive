#include "pch.h"
#include "ProcessPages.h"
#include "ObjectSecurity.h"
#include "FormatHelper.h"
#include "Signature.h"
#include "NetworkTracking.h"
#include <WinSys/Helpers.h>
#include <WinSys/Processes.h>
#include <WinSys/Token.h>
#include <WinSys/LsaSecurity.h>
#include <WTLHelper.h>
#include <ClipboardHelper.h>

namespace {
	void SetIconId(PROPSHEETPAGE& psp, UINT icon) {
		psp.dwFlags |= PSP_USEICONID;
		psp.pszIcon = MAKEINTRESOURCE(icon);
	}

	// the image's DLL characteristics (IMAGE_DLLCHARACTERISTICS_*), from its PE header
	std::optional<WORD> GetDllCharacteristics(std::wstring const& path) {
		wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr));
		BYTE buffer[4096];
		DWORD read;
		if (!file || !::ReadFile(file.get(), buffer, sizeof(buffer), &read, nullptr) || read < sizeof(IMAGE_DOS_HEADER))
			return std::nullopt;
		auto dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(buffer);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > read)
			return std::nullopt;
		auto nt = reinterpret_cast<IMAGE_NT_HEADERS32 const*>(buffer + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
			return std::nullopt;
		if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			return reinterpret_cast<IMAGE_NT_HEADERS64 const*>(nt)->OptionalHeader.DllCharacteristics;
		return nt->OptionalHeader.DllCharacteristics;
	}

	// "Enabled (a, b)" or "Enabled"
	std::wstring EnabledText(std::initializer_list<std::pair<DWORD, PCWSTR>> details) {
		std::wstring text;
		for (auto& [on, name] : details)
			if (on)
				text += (text.empty() ? L" (" : L", ") + std::wstring(name);
		return L"Enabled" + text + (text.empty() ? L"" : L")");
	}
}

wil::unique_handle OpenSheetProcess(ProcessProperties const& props, DWORD access) {
	wil::unique_handle hProcess(::OpenProcess(access, FALSE, props.Id));
	FILETIME created, exited, kernel, user;
	if (hProcess && ::GetProcessTimes(hProcess.get(), &created, &exited, &kernel, &user)
		&& (int64_t(created.dwHighDateTime) << 32 | created.dwLowDateTime) != props.CreateTime) {
		// another process with the same ID
		hProcess.reset();
		::SetLastError(ERROR_NOT_FOUND);
	}
	return hProcess;
}

CProcGeneralPage::CProcGeneralPage(ProcessProperties const& props, HICON hIcon) : m_Props(props), m_hIcon(hIcon) {
	SetIconId(m_psp, IDI_PROCESS);
}

LRESULT CProcGeneralPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& p = m_Props;
	if (m_hIcon)
		CStatic(GetDlgItem(IDC_PROC_ICON)).SetIcon(m_hIcon);
	SetDlgItemText(IDC_PROC_NAME, p.ImageName.c_str());
	SetDlgItemText(IDC_PROC_DESCRIPTION, p.Description.c_str());
	SetDlgItemText(IDC_PROC_PATH, p.ImagePath.c_str());
	SetDlgItemText(IDC_PROC_COMMANDLINE, p.CommandLine.c_str());
	SetDlgItemText(IDC_PROC_PID, std::format(L"{} (0x{:X})", p.Id, p.Id).c_str());
	SetDlgItemText(IDC_PROC_PARENT, (p.ParentName.empty()
		? std::format(L"{} (not running)", p.ParentId) : std::format(L"{} ({})", p.ParentName, p.ParentId)).c_str());
	SetDlgItemText(IDC_PROC_USER, p.User.c_str());
	SetDlgItemText(IDC_PROC_SESSION, std::to_wstring(p.SessionId).c_str());
	SetDlgItemText(IDC_PROC_STARTED, p.CreateTime ? FormatHelper::FormatDateTime(p.CreateTime).c_str() : L"");
	ShowMitigations();
	GetDlgItem(IDC_PROC_VERIFY).EnableWindow(!p.ImagePath.empty());
	// not Idle and System
	GetDlgItem(IDC_PROC_KILL).EnableWindow(p.Id > 4);

	// no gripper (the sheet has one), no minimum (the sheet keeps its own)
	InitDynamicLayout(false, false);
	// painted off screen, bottom to top: resizing moves most controls, which otherwise flicker as each erases and
	// repaints in turn (not for a page with a list view, which composited painting slows down)
	ModifyStyleEx(0, WS_EX_COMPOSITED);
	return TRUE;
}

void CProcGeneralPage::ShowMitigations() {
	auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_INFORMATION);
	if (!hProcess)
		hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION);
	if (!hProcess) {
		auto text = WinSys::Helpers::GetErrorText(::GetLastError());
		for (UINT id : { IDC_PROC_ASLR, IDC_PROC_CFG, IDC_PROC_CET, IDC_PROC_DEP })
			SetDlgItemText(id, text.c_str());
		return;
	}
	auto h = hProcess.get();
	auto unknown = L"Unknown";

	// ASLR: the image asks for it (or the process forces it); the policy adds the rest
	PROCESS_MITIGATION_ASLR_POLICY aslr{};
	bool hasAslr = ::GetProcessMitigationPolicy(h, ProcessASLRPolicy, &aslr, sizeof(aslr));
	auto characteristics = GetDllCharacteristics(m_Props.ImagePath);
	std::wstring text = unknown;
	if (characteristics || hasAslr) {
		// high entropy: the policy, or the image's header asking for it
		WORD flags = characteristics.value_or(0);
		bool dynamicBase = flags & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
		text = dynamicBase || aslr.EnableForceRelocateImages
			? EnabledText({ { aslr.EnableHighEntropy || (flags & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA), L"high entropy" },
				{ aslr.EnableBottomUpRandomization, L"bottom-up" },
				{ aslr.EnableForceRelocateImages, L"force relocate" }, { aslr.DisallowStrippedImages, L"no stripped images" } })
			: L"Disabled";
	}
	SetDlgItemText(IDC_PROC_ASLR, text.c_str());

	PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
	if (::GetProcessMitigationPolicy(h, ProcessControlFlowGuardPolicy, &cfg, sizeof(cfg)))
		text = cfg.EnableControlFlowGuard
			? EnabledText({ { cfg.StrictMode, L"strict" }, { cfg.EnableExportSuppression, L"export suppression" } }) : L"Disabled";
	else
		text = unknown;
	SetDlgItemText(IDC_PROC_CFG, text.c_str());

	// CET: hardware-enforced stack protection (user-mode shadow stacks)
	PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY cet{};
	if (::GetProcessMitigationPolicy(h, ProcessUserShadowStackPolicy, &cet, sizeof(cet)))
		text = cet.EnableUserShadowStack
			? EnabledText({ { cet.EnableUserShadowStackStrictMode, L"strict" } }) : cet.AuditUserShadowStack ? L"Audit" : L"Disabled";
	else
		text = unknown;
	SetDlgItemText(IDC_PROC_CET, text.c_str());

	// DEP is always on for 64-bit processes; the policy describes 32-bit ones
	BOOL wow64 = FALSE;
	::IsWow64Process(h, &wow64);
	PROCESS_MITIGATION_DEP_POLICY dep{};
	if (!wow64)
		text = L"Enabled (permanent, 64-bit)";
	else if (::GetProcessMitigationPolicy(h, ProcessDEPPolicy, &dep, sizeof(dep)))
		text = dep.Enable ? EnabledText({ { dep.Permanent, L"permanent" },
			{ dep.DisableAtlThunkEmulation, L"no ATL thunk emulation" } }) : L"Disabled";
	else
		text = unknown;
	SetDlgItemText(IDC_PROC_DEP, text.c_str());
}

LRESULT CProcGeneralPage::OnVerify(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CWaitCursor wait;
	SetDlgItemText(IDC_PROC_SIGNATURE, L"Verifying...");
	auto info = WinSys::FileSignature::Verify(m_Props.ImagePath.c_str());
	SetDlgItemText(IDC_PROC_SIGNATURE, FormatSignature(info).c_str());
	return 0;
}

LRESULT CProcGeneralPage::OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto text = std::format(L"Terminate process {} ({})?", m_Props.ImageName, m_Props.Id);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;

	auto hProcess = OpenSheetProcess(m_Props, PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION);
	if (!hProcess || !::TerminateProcess(hProcess.get(), 1)) {
		AtlMessageBox(m_hWnd, std::format(L"Failed to terminate the process: {}", WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	CWindow button(GetDlgItem(IDC_PROC_KILL));
	button.SetWindowText(L"Terminated");
	button.EnableWindow(FALSE);
	return 0;
}

CProcEnvironmentPage::CProcEnvironmentPage(ProcessProperties const& props) : m_Props(props) {
	SetIconId(m_psp, IDI_ENVIRONMENT);
}

LRESULT CProcEnvironmentPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_ENV_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
	auto cm = GetColumnManager(m_List);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	cm->AddColumn(L"Name", LVCFMT_LEFT, ::MulDiv(180, dpi, 96));
	cm->AddColumn(L"Value", LVCFMT_LEFT, ::MulDiv(560, dpi, 96));

	if (Load()) {
		m_List.SetItemCount((int)m_Vars.size());
		SetDlgItemText(IDC_ENV_STATUS, std::format(L"{} variables", m_Vars.size()).c_str());
	}
	GetDlgItem(IDC_ENV_COPYALL).EnableWindow(!m_Vars.empty());
	UpdateButtons();

	InitDynamicLayout(false, false);
	return TRUE;
}

bool CProcEnvironmentPage::Load() {
	auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ);
	if (!hProcess) {
		SetDlgItemText(IDC_ENV_STATUS, std::format(L"Can't read the environment: {}",
			WinSys::Helpers::GetErrorText(::GetLastError())).c_str());
		return false;
	}
	m_Vars = WinSys::Process::GetEnvironment(hProcess.get());
	if (m_Vars.empty()) {
		// a process with no environment at all is unlikely: rather, its memory couldn't be read
		SetDlgItemText(IDC_ENV_STATUS, L"Can't read the environment");
		return false;
	}
	// by name, as the list is shown before any sorting
	std::ranges::stable_sort(m_Vars, [](auto const& v1, auto const& v2) { return ::_wcsicmp(v1.first.c_str(), v2.first.c_str()) < 0; });
	return true;
}

CString CProcEnvironmentPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Vars.size())
		return L"";
	auto& [name, value] = m_Vars[row];
	return col == 0 ? name.c_str() : value.c_str();
}

void CProcEnvironmentPage::DoSort(SortInfo const* si) {
	auto col = si->SortColumn;
	std::ranges::stable_sort(m_Vars, [&](auto const& v1, auto const& v2) {
		auto result = col == 0 ? ::_wcsicmp(v1.first.c_str(), v2.first.c_str()) : ::_wcsicmp(v1.second.c_str(), v2.second.c_str());
		return si->SortAscending ? result < 0 : result > 0;
		});
	// the selection is by row: it would now be other variables
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
}

void CProcEnvironmentPage::OnStateChanged(HWND, int, int, UINT, UINT) {
	// a range of rows selected with Shift
	UpdateButtons();
}

LRESULT CProcEnvironmentPage::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	UpdateButtons();
	bHandled = FALSE;
	return 0;
}

void CProcEnvironmentPage::UpdateButtons() {
	GetDlgItem(IDC_ENV_COPY).EnableWindow(m_List.GetSelectedCount() > 0);
}

LRESULT CProcEnvironmentPage::OnCopy(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// NAME=value lines, as in the environment block itself
	std::wstring text;
	for (int i = id == IDC_ENV_COPYALL ? 0 : m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Vars.size();
		i = id == IDC_ENV_COPYALL ? i + 1 : m_List.GetNextItem(i, LVNI_SELECTED))
		text += std::format(L"{}={}\r\n", m_Vars[i].first, m_Vars[i].second);
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

namespace {
	std::wstring FormatLuid(LUID const& luid) {
		return luid.HighPart ? std::format(L"0x{:X}:{:08X}", (ULONG)luid.HighPart, luid.LowPart) : std::format(L"0x{:X}", luid.LowPart);
	}

	// the account's name, with the SID: or the SID alone
	std::wstring FormatSid(WinSys::Sid const& sid) {
		if (!sid)
			return L"";
		auto name = sid.UserName();
		return name.empty() ? sid.AsString() : std::format(L"{} ({})", name, sid.AsString());
	}

	// a protected process's trust level SID (S-1-19-type-signer): its protection type and signer
	std::wstring FormatTrustLevel(WinSys::Sid const& sid) {
		auto psid = (PSID)sid;
		if (*::GetSidSubAuthorityCount(psid) != 2)
			return FormatSid(sid);
		auto type = *::GetSidSubAuthority(psid, 0), signer = *::GetSidSubAuthority(psid, 1);
		PCWSTR typeName = type == SECURITY_PROCESS_PROTECTION_TYPE_FULL_RID ? L"Protected"
			: type == SECURITY_PROCESS_PROTECTION_TYPE_LITE_RID ? L"Protected Light" : L"None";
		PCWSTR signerName = L"";
		switch (signer) {
			case SECURITY_PROCESS_PROTECTION_LEVEL_AUTHENTICODE_RID: signerName = L"Authenticode"; break;
			case SECURITY_PROCESS_PROTECTION_LEVEL_ANTIMALWARE_RID: signerName = L"Antimalware"; break;
			case SECURITY_PROCESS_PROTECTION_LEVEL_APP_RID: signerName = L"App"; break;
			case SECURITY_PROCESS_PROTECTION_LEVEL_WINDOWS_RID: signerName = L"Windows"; break;
			case SECURITY_PROCESS_PROTECTION_LEVEL_WINTCB_RID: signerName = L"WinTcb"; break;
		}
		return std::format(L"{}{}{} ({})", typeName, *signerName ? L", " : L"", signerName, sid.AsString());
	}

	// an AppContainer's package moniker (WinSys Sid::AppContainerMoniker) and SID
	std::wstring FormatAppContainer(WinSys::Sid const& sid) {
		auto moniker = sid ? sid.AppContainerMoniker() : L"";
		return moniker.empty() ? FormatSid(sid) : std::format(L"{} ({})", moniker, sid.AsString());
	}

	PCWSTR YesNo(bool value) {
		return value ? L"Yes" : L"No";
	}

	PCWSTR LogonTypeToString(WinSys::SecurityLogonType type) {
		using enum WinSys::SecurityLogonType;
		switch (type) {
			case Interactive: return L"Interactive";
			case Network: return L"Network";
			case Batch: return L"Batch";
			case Service: return L"Service";
			case Proxy: return L"Proxy";
			case Unlock: return L"Unlock";
			case NetworkCleartext: return L"Network (cleartext)";
			case NewCredentials: return L"New credentials";
			case RemoteInteractive: return L"Remote interactive";
			case CachedInteractive: return L"Cached interactive";
			case CachedRemoteInteractive: return L"Cached remote interactive";
			case CachedUnlock: return L"Cached unlock";
		}
		return L"System";
	}

	PCWSTR SidUseToString(SID_NAME_USE use) {
		switch (use) {
			case SidTypeUser: return L"User";
			case SidTypeGroup: return L"Group";
			case SidTypeDomain: return L"Domain";
			case SidTypeAlias: return L"Alias";
			case SidTypeWellKnownGroup: return L"Well-known group";
			case SidTypeDeletedAccount: return L"Deleted account";
			case SidTypeComputer: return L"Computer";
			case SidTypeLabel: return L"Label";
			case SidTypeLogonSession: return L"Logon session";
		}
		return L"";
	}

	std::wstring GroupAttributesToString(WinSys::SidGroupAttributes attributes) {
		static const struct {
			DWORD Flag;
			PCWSTR Name;
		} flags[] = {
			{ SE_GROUP_MANDATORY, L"Mandatory" },
			{ SE_GROUP_ENABLED, L"Enabled" },
			{ SE_GROUP_ENABLED_BY_DEFAULT, L"Default" },
			{ SE_GROUP_OWNER, L"Owner" },
			{ SE_GROUP_USE_FOR_DENY_ONLY, L"Deny only" },
			{ SE_GROUP_INTEGRITY, L"Integrity" },
			{ SE_GROUP_INTEGRITY_ENABLED, L"Integrity enabled" },
			{ SE_GROUP_LOGON_ID, L"Logon ID" },
			{ SE_GROUP_RESOURCE, L"Resource" },
		};
		std::wstring text;
		for (auto& f : flags)
			if ((DWORD)attributes & f.Flag)
				text += (text.empty() ? L"" : L", ") + std::wstring(f.Name);
		return text;
	}

	void InitList(CListViewCtrl& list, HWND hList, std::initializer_list<std::pair<PCWSTR, int>> columns) {
		list.Attach(hList);
		list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		auto dpi = ::GetDpiForWindow(hList);
		int i = 0;
		for (auto& [name, width] : columns)
			list.InsertColumn(i++, name, LVCFMT_LEFT, ::MulDiv(width, dpi, 96));
	}

	void AddRow(CListViewCtrl& list, std::initializer_list<std::wstring> texts) {
		int row = list.GetItemCount();
		int col = 0;
		for (auto& text : texts) {
			if (col == 0)
				list.InsertItem(row, text.c_str());
			else
				list.SetItemText(row, col, text.c_str());
			col++;
		}
	}
}

CProcTokenPage::CProcTokenPage(ProcessProperties const& props) : m_Props(props) {
	SetIconId(m_psp, IDI_TOKEN);
}

LRESULT CProcTokenPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	InitList(m_Privileges, GetDlgItem(IDC_TOKEN_PRIVS), { { L"Privilege", 190 }, { L"State", 110 }, { L"Description", 300 } });
	InitList(m_Groups, GetDlgItem(IDC_TOKEN_GROUPS), { { L"Name", 260 }, { L"Attributes", 220 }, { L"Type", 110 }, { L"SID", 300 } });

	// the token's security decides; the process needs only limited access. Its source needs more access to the token
	auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION);
	std::unique_ptr<WinSys::Token> token;
	if (hProcess) {
		token = std::make_unique<WinSys::Token>(hProcess.get(), WinSys::TokenAccessMask(TOKEN_QUERY | TOKEN_QUERY_SOURCE));
		if (!*token)
			token = std::make_unique<WinSys::Token>(hProcess.get(), WinSys::TokenAccessMask::Query);
	}
	if (!token || !*token) {
		SetDlgItemText(IDC_TOKEN_USER, std::format(L"Can't open the token: {}", WinSys::Helpers::GetErrorText(::GetLastError())).c_str());
	}
	else {
		ShowProperties(*token);
		ShowPrivileges(*token);
		ShowGroups(*token);
	}

	InitDynamicLayout(false, false);
	return TRUE;
}

LRESULT CProcTokenPage::OnSecurity(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto info = CObjectSecurityInfo::Open([&](DWORD access) -> HANDLE {
		HANDLE hToken = nullptr;
		if (auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION); hProcess && ::OpenProcessToken(hProcess.get(), access, &hToken))
			return hToken;
		return nullptr;
		}, L"Token", std::format(L"Token of {} ({})", m_Props.ImageName, m_Props.Id));
	// its dialogs are owned by the sheet, which themes them (CAclDialogTheming)
	::EditSecurity(m_hWnd, info);
	info->Release();
	return 0;
}

void CProcTokenPage::ShowProperties(WinSys::Token const& token) {
	auto set = [&](UINT id, std::wstring const& value) {
		SetDlgItemText(id, value.c_str());
	};
	auto unknown = [](auto const& value, auto format) -> std::wstring {
		return value ? format(*value) : std::wstring(L"Unknown");
	};
	auto stats = token.GetStats();

	auto [userName, userSid] = token.GetUserNameAndSid();
	set(IDC_TOKEN_USER, userName.empty() ? userSid.AsString() : std::format(L"{} ({})", userName, userSid.AsString()));
	set(IDC_TOKEN_OWNER, FormatSid(token.GetOwner()));
	set(IDC_TOKEN_PGROUP, FormatSid(token.GetPrimaryGroup()));
	set(IDC_TOKEN_SESSION, std::to_wstring(token.GetSessionId()));

	set(IDC_TOKEN_LOGONID, FormatLuid(stats.AuthenticationId));
	// details only for the caller's own logon sessions, or when elevated
	if (auto logon = WinSys::LsaSecurity::GetLogonSessionData(stats.AuthenticationId)) {
		set(IDC_TOKEN_LOGONTYPE, LogonTypeToString(logon->LogonType));
		set(IDC_TOKEN_PACKAGE, logon->AuthenticationPackage);
		if (logon->LogonTime.QuadPart)
			set(IDC_TOKEN_LOGONTIME, FormatHelper::FormatDateTime(logon->LogonTime.QuadPart));
		if (!logon->LogonServer.empty())
			set(IDC_TOKEN_LOGONSERVER, logon->LogonServer);
	}
	if (auto origin = token.GetOrigin(); origin && (origin->LowPart || origin->HighPart))
		set(IDC_TOKEN_ORIGIN, FormatLuid(*origin));

	set(IDC_TOKEN_TOKENID, FormatLuid(stats.TokenId));
	set(IDC_TOKEN_MODIFIEDID, FormatLuid(stats.ModifiedId));
	set(IDC_TOKEN_TYPE, stats.TokenType == TokenPrimary ? std::wstring(L"Primary") : std::format(L"Impersonation ({})",
		stats.ImpersonationLevel == SecurityAnonymous ? L"anonymous" : stats.ImpersonationLevel == SecurityIdentification ? L"identification"
		: stats.ImpersonationLevel == SecurityImpersonation ? L"impersonation" : L"delegation"));
	if (auto source = token.GetSource()) {
		// padded with spaces to its 8 characters
		std::string name(source->SourceName, strnlen(source->SourceName, TOKEN_SOURCE_LENGTH));
		while (!name.empty() && name.back() == ' ')
			name.pop_back();
		set(IDC_TOKEN_SOURCE, std::format(L"{} ({})", std::wstring(name.begin(), name.end()), FormatLuid(source->SourceIdentifier)));
	}

	set(IDC_TOKEN_ELEVATED, YesNo(token.IsElevated()));
	set(IDC_TOKEN_ELEVATIONTYPE, unknown(token.GetElevationType(), [](auto type) -> std::wstring {
		using enum WinSys::ElevationType;
		return type == Full ? L"Full (elevated half of a split token)" : type == Limited ? L"Limited (filtered half of a split token)" : L"Default";
		}));
	set(IDC_TOKEN_INTEGRITY, FormatHelper::IntegrityLevelToString(token.GetIntegrityLevel()));
	set(IDC_TOKEN_POLICY, unknown(token.GetMandatoryPolicy(), [](uint32_t policy) -> std::wstring {
		std::wstring text;
		if (policy & TOKEN_MANDATORY_POLICY_NO_WRITE_UP)
			text = L"No write up";
		if (policy & TOKEN_MANDATORY_POLICY_NEW_PROCESS_MIN)
			text += text.empty() ? L"New process min" : L", new process min";
		return text.empty() ? L"None" : text;
		}));
	using enum WinSys::VirtualizationState;
	auto virt = token.GetVirtualizationState();
	set(IDC_TOKEN_VIRTUALIZATION, virt == Enabled ? L"Enabled" : virt == Disabled ? L"Disabled" : virt == NotAllowed ? L"Not allowed" : L"Unknown");
	set(IDC_TOKEN_UIACCESS, unknown(token.HasUIAccess(), [](bool v) -> std::wstring { return YesNo(v); }));
	if (token.IsAppContainer()) {
		auto number = token.GetAppContainerNumber();
		set(IDC_TOKEN_APPCONTAINER, std::format(L"{}{}", FormatAppContainer(token.GetAppContainerSid()), number ? std::format(L", number {}", *number) : L""));
	}
	else {
		set(IDC_TOKEN_APPCONTAINER, L"No");
	}
	set(IDC_TOKEN_RESTRICTED, YesNo(token.IsRestricted()));
	set(IDC_TOKEN_FILTERED, unknown(token.HasRestrictions(), [](bool v) -> std::wstring { return YesNo(v); }));
	set(IDC_TOKEN_SANDBOX, unknown(token.IsSandboxInert(), [](bool v) -> std::wstring { return YesNo(v); }));
	auto trust = token.GetTrustLevel();
	set(IDC_TOKEN_TRUSTLEVEL, trust ? FormatTrustLevel(trust) : L"None");
	set(IDC_TOKEN_CHARGED, FormatHelper::FormatBytes(stats.DynamicCharged));
	set(IDC_TOKEN_AVAILABLE, FormatHelper::FormatBytes(stats.DynamicAvailable));
	// tokens that don't expire have the largest time, or (SYSTEM's) the end of 2999
	constexpr int64_t Year2900 = 410'000'000'000'000'000;
	auto expires = stats.ExpirationTime.QuadPart;
	set(IDC_TOKEN_EXPIRES, expires <= 0 || expires >= Year2900 ? L"Never" : FormatHelper::FormatDateTime(expires));
}

void CProcTokenPage::ShowPrivileges(WinSys::Token const& token) {
	auto privileges = token.EnumPrivileges();
	for (auto& p : privileges) {
		auto attributes = p.Attributes;
		std::wstring state = (attributes & SE_PRIVILEGE_ENABLED) ? L"Enabled" : L"Disabled";
		if (attributes & SE_PRIVILEGE_ENABLED_BY_DEFAULT)
			state += L" (default)";
		WCHAR description[256];
		DWORD size = _countof(description), language;
		if (p.Name.empty() || !::LookupPrivilegeDisplayName(nullptr, p.Name.c_str(), description, &size, &language))
			description[0] = 0;
		AddRow(m_Privileges, { p.Name.empty() ? FormatLuid(p.Privilege) : p.Name, state, description });
	}
	SetDlgItemText(IDC_TOKEN_PRIVS_LABEL, std::format(L"Privileges ({}):", privileges.size()).c_str());
}

void CProcTokenPage::ShowGroups(WinSys::Token const& token) {
	auto groups = token.EnumGroups();
	for (auto& g : groups)
		AddRow(m_Groups, { g.Name, GroupAttributesToString(g.Attributes), SidUseToString(g.Use), g.Sid });
	// an AppContainer's capabilities are SIDs as well
	auto capabilities = token.IsAppContainer() ? token.EnumGroups(true) : std::vector<WinSys::TokenGroup>();
	for (auto& c : capabilities)
		AddRow(m_Groups, { c.Name, GroupAttributesToString(c.Attributes), L"Capability", c.Sid });
	SetDlgItemText(IDC_TOKEN_GROUPS_LABEL, (capabilities.empty() ? std::format(L"Groups ({}):", groups.size())
		: std::format(L"Groups ({}) and capabilities ({}):", groups.size(), capabilities.size())).c_str());
}

ProcessSnapshot::ProcessSnapshot(ProcessProperties const& props) : m_Props(props) {
}

void ProcessSnapshot::Take() {
	m_Process = nullptr;
	if (m_Exited)
		return;
	// the size needed grows between calls
	NTSTATUS status = STATUS_INFO_LENGTH_MISMATCH;
	for (int i = 0; i < 4 && status == STATUS_INFO_LENGTH_MISMATCH; i++) {
		if (!m_Buffer)
			m_Buffer = std::make_unique<BYTE[]>(m_BufferSize);
		LARGE_INTEGER now;
		::QueryPerformanceCounter(&now);
		m_Time = now.QuadPart;
		ULONG len = 0;
		status = ::NtQuerySystemInformation(SystemExtendedProcessInformation, m_Buffer.get(), m_BufferSize, &len);
		if (status == STATUS_INFO_LENGTH_MISMATCH) {
			m_BufferSize = (std::max)(m_BufferSize * 2, len + (1 << 16));
			m_Buffer.reset();
		}
	}
	if (!NT_SUCCESS(status))
		return;

	// the process, unless its ID now belongs to another one
	auto p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION const*>(m_Buffer.get());
	for (;;) {
		if (HandleToULong(p->UniqueProcessId) == m_Props.Id && p->CreateTime.QuadPart == m_Props.CreateTime) {
			m_Process = p;
			return;
		}
		if (p->NextEntryOffset == 0)
			break;
		p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION const*>(reinterpret_cast<BYTE const*>(p) + p->NextEntryOffset);
	}
	m_Exited = true;
}

SYSTEM_PROCESS_INFORMATION const* ProcessSnapshot::Process() const {
	return m_Process;
}

SYSTEM_PROCESS_INFORMATION const* ProcessSnapshot::Find(uint32_t pid) const {
	if (!m_Process)
		return nullptr;
	auto p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION const*>(m_Buffer.get());
	for (;;) {
		if (HandleToULong(p->UniqueProcessId) == pid)
			return p;
		if (p->NextEntryOffset == 0)
			return nullptr;
		p = reinterpret_cast<SYSTEM_PROCESS_INFORMATION const*>(reinterpret_cast<BYTE const*>(p) + p->NextEntryOffset);
	}
}

bool ProcessSnapshot::Exited() const {
	return m_Exited;
}

int64_t ProcessSnapshot::Time() const {
	return m_Time;
}

namespace {
	std::wstring FormatRate(double bytesPerSecond) {
		wchar_t text[32];
		GraphCtrl::Format::BytesPerSec((float)bytesPerSecond, text, _countof(text));
		return text;
	}

	std::wstring FormatBitRate(double bytesPerSecond) {
		wchar_t text[32];
		GraphCtrl::Format::BitsPerSec((float)(bytesPerSecond * 8), text, _countof(text));
		return text;
	}

	// the graphs' time span, as the Performance tab's: they show PerfHistoryLength samples
	std::wstring TimeSpanText(int interval) {
		auto seconds = PerfHistoryLength * interval / 1000;
		return seconds % 60 == 0 ? std::format(L"{} minutes", seconds / 60) : std::format(L"{} seconds", seconds);
	}
}

CProcPerformancePage::CProcPerformancePage(ProcessProperties const& props) : m_Props(props) {
	SetIconId(m_psp, IDI_PERFORMANCE);
	// created with the sheet, so the graphs have history when the page is first shown
	m_psp.dwFlags |= PSP_PREMATURE;
}

LRESULT CProcPerformancePage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	using namespace GraphCtrl;

	m_Cpu.Create(m_hWnd);
	m_Memory.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE, GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
	m_Memory.AddSeries(L"Private", SeriesStyle{ PerfColors::Memory, PerfColors::Memory });
	m_Memory.AddSeries(L"Working set", SeriesStyle{ PerfColors::User, PerfColors::User });
	m_Memory.SetTitle(L"Memory");
	m_Memory.SetRange(0, 1 << 20);
	m_Memory.SetAutoScale(true);
	m_Memory.SetValueFormatter(Format::Bytes);
	m_Memory.SetHistoryLength(PerfHistoryLength);

	m_Io.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE, GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
	m_Io.AddSeries(L"Read", SeriesStyle{ PerfColors::DiskRead, PerfColors::DiskRead });
	m_Io.AddSeries(L"Write", SeriesStyle{ PerfColors::DiskWrite, PerfColors::DiskWrite });
	m_Io.AddSeries(L"Other", SeriesStyle{ PerfColors::IoOther, PerfColors::IoOther });
	m_Io.SetTitle(L"I/O Transfer Rate");
	m_Io.SetRange(0, 1 << 20);
	m_Io.SetAutoScale(true);
	m_Io.SetValueFormatter(Format::BytesPerSec);
	m_Io.SetHistoryLength(PerfHistoryLength);

	// the counters start with the tracker: a process's traffic before then isn't known
	m_NetworkTracker = NetworkTracking::Acquire();
	if (!m_NetworkTracker)
		m_NetworkError = ::GetLastError();
	m_Network.Create(m_hWnd, !m_NetworkTracker ? L"Network Throughput (not available)" : L"Network Throughput");

	auto theme = WTLHelper::IsDarkMode() ? GraphTheme::Dark() : GraphTheme::Light();
	auto timeSpan = TimeSpanText(m_Props.UpdateInterval);
	for (CGraphControl* graph : { (CGraphControl*)&m_Cpu, &m_Memory, &m_Io, (CGraphControl*)&m_Network }) {
		graph->SetTheme(theme);
		graph->SetTimeSpanText(timeSpan);
		graph->SetExpectedInterval(m_Props.UpdateInterval);
	}

	CRect rc;
	GetDlgItem(IDC_PERF_VERTICAL).GetWindowRect(&rc);
	m_ButtonSize = rc.Size();
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	return TRUE;
}

LRESULT CProcPerformancePage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	Layout(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

LRESULT CProcPerformancePage::OnVertical(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Vertical = IsDlgButtonChecked(IDC_PERF_VERTICAL) == BST_CHECKED;
	CRect rc;
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	// now: a resized graph otherwise paints only with the next sample, and the page where the graphs were before
	Invalidate();
	UpdateWindow();
	for (GraphCtrl::CGraphControl* graph : { (GraphCtrl::CGraphControl*)&m_Cpu, &m_Memory, &m_Io, (GraphCtrl::CGraphControl*)&m_Network })
		graph->Refresh();
	return 0;
}

void CProcPerformancePage::Layout(int cx, int cy) {
	if (!m_Cpu.IsWindow())
		return;
	// the other pages' margins; the text lines at the bottom, the check box at their right, the graphs above them
	CRect units(0, 0, 7, 11);
	MapDialogRect(&units);
	int margin = units.right, lineHeight = units.bottom, gap = units.right / 2;
	// centered on the last line
	GetDlgItem(IDC_PERF_VERTICAL).SetWindowPos(nullptr, cx - margin - m_ButtonSize.cx, cy - margin - (lineHeight + m_ButtonSize.cy) / 2, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	UINT const lines[] = { IDC_PERF_CPU, IDC_PERF_MEMORY, IDC_PERF_IO };
	int y = cy - margin - _countof(lines) * lineHeight;
	for (auto id : lines) {
		GetDlgItem(id).SetWindowPos(nullptr, margin, y, (std::max)(0, int(cx - 2 * margin - m_ButtonSize.cx - gap)), lineHeight,
			SWP_NOZORDER | SWP_NOACTIVATE);
		y += lineHeight;
	}

	CRect area(margin, margin, cx - margin, cy - margin - _countof(lines) * lineHeight - gap);
	GraphCtrl::CGraphControl* graphs[] = { &m_Cpu, &m_Memory, &m_Io, &m_Network };
	// two columns when there is room for them, so the graphs don't get too flat, unless asked for one
	int columns = !m_Vertical && area.Width() >= ::MulDiv(560, ::GetDpiForWindow(m_hWnd), 96) ? 2 : 1;
	int rows = _countof(graphs) / columns;
	int width = (std::max)(0, area.Width() - (columns - 1) * gap), height = (std::max)(0, area.Height() - (rows - 1) * gap);
	for (int i = 0; i < _countof(graphs); i++) {
		int row = i / columns, column = i % columns;
		// the last row and column take the rounding remainder
		int x = area.left + column * (width / columns + gap), top = area.top + row * (height / rows + gap);
		int w = column == columns - 1 ? area.right - x : width / columns;
		int h = row == rows - 1 ? area.bottom - top : height / rows;
		graphs[i]->MoveWindow(x, top, (std::max)(0, w), (std::max)(0, h));
	}
}

void CProcPerformancePage::OnSample(ProcessSnapshot const& snapshot) {
	if (!m_Cpu.IsWindow() || m_Exited)
		return;
	auto p = snapshot.Process();
	if (!p) {
		if (snapshot.Exited()) {
			m_Exited = true;
			SetDlgItemText(IDC_PERF_CPU, L"The process has exited.");
		}
		return;
	}

	Counters now{ snapshot.Time(), p->KernelTime.QuadPart, p->UserTime.QuadPart,
		p->ReadTransferCount.QuadPart, p->WriteTransferCount.QuadPart, p->OtherTransferCount.QuadPart };
	if (m_NetworkTracker) {
		auto net = m_NetworkTracker->GetCounters(m_Props.Id);
		now.Received = net.Received;
		now.Sent = net.Sent;
	}

	// the rates, from the previous sample
	PerfSample cpu;	// what CCpuGraph takes
	double read = 0, write = 0, other = 0, received = 0, sent = 0;
	bool rates = m_Prev && now.Time > m_Prev->Time;
	if (rates) {
		static const int64_t frequency = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f.QuadPart; }();
		static const int processors = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		auto& prev = *m_Prev;
		double seconds = double(now.Time - prev.Time) / frequency;
		// the CPU times are in 100 ns units; percent of all processors
		double total = seconds * 10'000'000 * processors / 100;
		cpu.HasCpu = true;
		cpu.KernelCpu = float((now.KernelTime - prev.KernelTime) / total);
		cpu.UserCpu = float((now.UserTime - prev.UserTime) / total);
		read = (now.Read - prev.Read) / seconds;
		write = (now.Write - prev.Write) / seconds;
		other = (now.Other - prev.Other) / seconds;
		received = now.Received >= prev.Received ? (now.Received - prev.Received) / seconds : 0;
		sent = now.Sent >= prev.Sent ? (now.Sent - prev.Sent) / seconds : 0;
	}
	m_Prev = now;

	m_Cpu.Push(cpu);
	float memory[] = { (float)p->PrivatePageCount, (float)p->WorkingSetSize };
	m_Memory.PushSamples(memory, _countof(memory));
	float io[] = { rates ? (float)read : GraphCtrl::MissingSample, rates ? (float)write : GraphCtrl::MissingSample,
		rates ? (float)other : GraphCtrl::MissingSample };
	m_Io.PushSamples(io, _countof(io));
	m_Network.Push(received, sent, rates && m_NetworkTracker);

	auto cpuTime = p->KernelTime.QuadPart + p->UserTime.QuadPart;
	SetDlgItemText(IDC_PERF_CPU, std::format(L"CPU: {}    CPU time: {} (kernel {})    Threads: {}    Handles: {}",
		cpu.HasCpu ? std::format(L"{:.2f}% (kernel {:.2f}%)", cpu.CpuPercent(), cpu.KernelCpu) : std::wstring(L"-"),
		FormatHelper::FormatTimeSpan(cpuTime), FormatHelper::FormatTimeSpan(p->KernelTime.QuadPart),
		FormatHelper::FormatNumber(p->NumberOfThreads), FormatHelper::FormatNumber(p->HandleCount)).c_str());
	SetDlgItemText(IDC_PERF_MEMORY, std::format(L"Private: {} (peak {})    Working set: {} (peak {})    Virtual size: {}",
		FormatHelper::FormatBytes(p->PrivatePageCount), FormatHelper::FormatBytes(p->PeakPagefileUsage),
		FormatHelper::FormatBytes(p->WorkingSetSize), FormatHelper::FormatBytes(p->PeakWorkingSetSize),
		FormatHelper::FormatBytes(p->VirtualSize)).c_str());
	auto network = m_NetworkTracker ? std::format(L"Network: receive {}, send {}", FormatBitRate(received), FormatBitRate(sent))
		: m_NetworkError == ERROR_ACCESS_DENIED ? std::wstring(L"Network: needs administrator rights") : std::wstring(L"Network: not available");
	SetDlgItemText(IDC_PERF_IO, std::format(L"I/O: read {}, write {}, other {}    {}",
		FormatRate(read), FormatRate(write), FormatRate(other), network).c_str());
}

CProcThreadsPage::CProcThreadsPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Snapshot(snapshot) {
	SetIconId(m_psp, IDI_THREADS);
	m_Threads.UseColumns(props.ThreadColumns);
}

LRESULT CProcThreadsPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// a control parent, so the dialog manager tabs into its list
	m_Threads.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT);
	return TRUE;
}

LRESULT CProcThreadsPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_Threads) {
		// the other pages' margin
		CRect margin(0, 0, 7, 7);
		MapDialogRect(&margin);
		m_Threads.MoveWindow(margin.right, margin.bottom, (std::max)(0, int(GET_X_LPARAM(lParam) - 2 * margin.right)),
			(std::max)(0, int(GET_Y_LPARAM(lParam) - 2 * margin.bottom)));
	}
	return 0;
}

int CProcThreadsPage::OnSetActive() {
	// the sheet's last snapshot; the sheet feeds the page only while it's active
	OnSample(m_Snapshot);
	return 0;
}

void CProcThreadsPage::OnSample(ProcessSnapshot const& snapshot) {
	// once the process is gone, its last threads stay
	if (auto p = snapshot.Process(); p && m_Threads)
		m_Threads.Update(p, snapshot.Time());
}
