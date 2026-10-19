#include "pch.h"
#include "ProcessPages.h"
#include "FormatHelper.h"
#include "Signature.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>

namespace {
	// the pages run on the sheet's thread, where WTLHelper's hook doesn't theme new dialogs
	void ApplyTheme(HWND hDlg) {
		DarkMode::setDarkWndNotifySafe(hDlg);
	}

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
	ApplyTheme(m_hWnd);
	return TRUE;
}

wil::unique_handle CProcGeneralPage::OpenProcess(DWORD access) const {
	wil::unique_handle hProcess(::OpenProcess(access, FALSE, m_Props.Id));
	FILETIME created, exited, kernel, user;
	if (hProcess && ::GetProcessTimes(hProcess.get(), &created, &exited, &kernel, &user)
		&& (int64_t(created.dwHighDateTime) << 32 | created.dwLowDateTime) != m_Props.CreateTime) {
		// another process with the same ID
		hProcess.reset();
		::SetLastError(ERROR_NOT_FOUND);
	}
	return hProcess;
}

void CProcGeneralPage::ShowMitigations() {
	auto hProcess = OpenProcess(PROCESS_QUERY_INFORMATION);
	if (!hProcess)
		hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION);
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
	auto info = VerifySignature(m_Props.ImagePath.c_str());
	SetDlgItemText(IDC_PROC_SIGNATURE, info.ToString().c_str());
	return 0;
}

LRESULT CProcGeneralPage::OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto text = std::format(L"Terminate process {} ({})?", m_Props.ImageName, m_Props.Id);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;

	auto hProcess = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION);
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

CProcPlaceholderPage::CProcPlaceholderPage(PCWSTR title, UINT icon, std::wstring text) :
	CPropertyPageImpl(title), m_Text(std::move(text)) {
	SetIconId(m_psp, icon);
}

LRESULT CProcPlaceholderPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetDlgItemText(IDC_PROC_PLACEHOLDER, m_Text.c_str());
	ApplyTheme(m_hWnd);
	return TRUE;
}

LRESULT CProcPlaceholderPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	// the text stays in the middle
	CWindow text(GetDlgItem(IDC_PROC_PLACEHOLDER));
	if (text) {
		CRect rc;
		text.GetWindowRect(&rc);
		int cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
		int margin = ::MulDiv(8, ::GetDpiForWindow(m_hWnd), 96);
		text.SetWindowPos(nullptr, margin, (cy - rc.Height()) / 2, (std::max)(0, cx - 2 * margin), rc.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	return 0;
}
