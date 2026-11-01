#include "pch.h"
#include "SystemInfoDlg.h"
#include "FormatHelper.h"
#include <WinSys/SystemDetails.h>
#include <WinSys/Firmware.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	PCWSTR OnOff(bool value) {
		return value ? L"On" : L"Off";
	}

	std::wstring Size(uint64_t bytes) {
		return bytes ? FormatHelper::FormatBytes(bytes) : L"";
	}

	PCWSTR ArchitectureName(uint16_t arch) {
		switch (arch) {
			case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
			case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
			case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
			case PROCESSOR_ARCHITECTURE_ARM: return L"ARM";
		}
		return L"Unknown";
	}

	// days, hours, minutes, seconds
	std::wstring Uptime(int64_t bootTime) {
		FILETIME now;
		::GetSystemTimeAsFileTime(&now);
		auto seconds = ((int64_t)(((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime) - bootTime) / 10000000;
		if (seconds < 0)
			return L"";
		auto days = seconds / 86400;
		auto time = std::format(L"{}:{:02}:{:02}", seconds / 3600 % 24, seconds / 60 % 60, seconds % 60);
		return days ? std::format(L"{} day{}, {}", days, days == 1 ? L"" : L"s", time) : time;
	}

	PCWSTR UacPromptName(uint32_t value) {
		switch (value) {
			case 0: return L"Elevate without prompting";
			case 1: return L"Prompt for credentials on the secure desktop";
			case 2: return L"Prompt for consent on the secure desktop";
			case 3: return L"Prompt for credentials";
			case 4: return L"Prompt for consent";
			case 5: return L"Prompt for consent for non-Windows binaries";
		}
		return L"Unknown";
	}

	std::wstring Join(std::vector<std::wstring> const& items, PCWSTR separator = L", ") {
		std::wstring text;
		for (auto& item : items)
			if (!item.empty())
				text += (text.empty() ? L"" : separator) + item;
		return text;
	}
}

LRESULT CSystemInfoDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_SYSINFO, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_SYSINFO, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_Tabs.Attach(GetDlgItem(IDC_SI_TABS));
	static const PCWSTR tabs[] = { L"Operating System", L"Computer", L"Processor", L"Memory", L"Security" };
	static_assert(_countof(tabs) == (int)Tab::_Count);
	for (int i = 0; i < _countof(tabs); i++)
		m_Tabs.InsertItem(i, tabs[i]);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_List.Attach(GetDlgItem(IDC_SI_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_List.InsertColumn(0, L"Property", LVCFMT_LEFT, ::MulDiv(220, dpi, 96));
	m_List.InsertColumn(1, L"Value", LVCFMT_LEFT, ::MulDiv(500, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	// above the tab control, which would otherwise paint over it when the selection changes
	m_List.SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	// the groups' titles
	LOGFONT lf;
	CFontHandle(m_List.GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&lf);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CSystemInfoDlg::Group(PCWSTR title) {
	m_Rows[(int)m_Building].push_back({ title, L"", true });
}

void CSystemInfoDlg::Add(std::wstring name, std::wstring value) {
	if (!value.empty())
		m_Rows[(int)m_Building].push_back({ std::move(name), std::move(value) });
}

void CSystemInfoDlg::Load() {
	CWaitCursor wait;
	std::wstring selected;
	if (int sel = m_List.GetNextItem(-1, LVNI_SELECTED); sel >= 0 && sel < (int)Rows().size())
		selected = Rows()[sel].Name;

	for (auto& rows : m_Rows)
		rows.clear();
	m_Building = Tab::Os;
	AddOs();
	m_Building = Tab::Computer;
	AddComputer();
	m_Building = Tab::Processor;
	AddProcessor();
	m_Building = Tab::Memory;
	AddMemory();
	m_Building = Tab::Security;
	AddSecurity();
	// a group whose queries all failed has no rows
	for (auto& rows : m_Rows)
		for (size_t i = rows.size(); i-- > 0; )
			if (rows[i].Header && (i + 1 == rows.size() || rows[i + 1].Header))
				rows.erase(rows.begin() + i);
	ShowTab(selected);

	SYSTEMTIME now;
	::GetLocalTime(&now);
	WCHAR time[64];
	::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, nullptr, time, _countof(time));
	SetDlgItemText(IDC_SI_STATUS, std::format(L"Read at {}", time).c_str());
}

void CSystemInfoDlg::ShowTab(std::wstring const& select) {
	auto& rows = Rows();
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int selected = -1;
	for (int i = 0; i < (int)rows.size(); i++) {
		m_List.InsertItem(i, rows[i].Name.c_str());
		m_List.SetItemText(i, 1, rows[i].Value.c_str());
		if (selected < 0 && !select.empty() && rows[i].Name == select)
			selected = i;
	}
	if (selected >= 0) {
		m_List.SetItemState(selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.EnsureVisible(selected, FALSE);
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate();
}

LRESULT CSystemInfoDlg::OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	ShowTab();
	return 0;
}

void CSystemInfoDlg::AddOs() {
	auto os = WinSys::SystemDetails::GetOs();
	Add(L"Product", Join({ os.ProductName, os.DisplayVersion }, L" "));
	Add(L"Edition", os.EditionId);
	Add(L"Version", std::format(L"{}.{}.{}.{}", os.Major, os.Minor, os.Build, os.Revision));
	Add(L"Build lab", os.BuildLab);
	Add(L"Installation type", os.InstallationType);
	Add(L"Architecture", ArchitectureName(os.Architecture));
	Add(L"Installed (last feature update)", FormatHelper::FormatDateTime(os.InstallTime));
	if (os.BootTime) {
		auto boot = FormatHelper::FormatDateTime(os.BootTime);
		Add(L"Last boot", os.FastStartup ? boot + L" (fast startup)" : boot);
		Add(L"Uptime", Uptime(os.BootTime));
	}
	Add(L"Computer name", os.ComputerName);
	if (_wcsicmp(os.NetBiosName.c_str(), os.ComputerName.c_str()))
		Add(L"NetBIOS name", os.NetBiosName);
	switch (os.Join) {
		case WinSys::DomainJoin::Domain: Add(L"Domain", os.JoinName); break;
		case WinSys::DomainJoin::Workgroup: Add(L"Workgroup", os.JoinName); break;
		case WinSys::DomainJoin::Unjoined: Add(L"Domain", L"(not joined)"); break;
	}
	Add(L"Registered owner", os.RegisteredOwner);
	Add(L"Registered organization", os.RegisteredOrganization);
	Add(L"Windows directory", os.WindowsDirectory);
	Add(L"Time zone", os.TimeZone);
	Add(L"System locale", os.SystemLocale);
	Add(L"User locale", os.UserLocale);
	Add(L"Display language", os.UiLanguage);
}

void CSystemInfoDlg::AddComputer() {
	auto hw = WinSys::SystemDetails::GetHardware();
	Add(L"Manufacturer", hw.SystemManufacturer);
	Add(L"Model", hw.SystemProduct);
	Add(L"Version", hw.SystemVersion);
	Add(L"Family", hw.SystemFamily);
	Add(L"SKU", hw.SystemSku);
	Add(L"Baseboard", Join({ hw.BoardManufacturer, hw.BoardProduct, hw.BoardVersion }, L" "));
	Add(L"BIOS", Join({ hw.BiosVendor, hw.BiosVersion, hw.BiosDate }, L", "));
	PCWSTR firmware = L"";
	switch (WinSys::Firmware::GetType()) {
		case WinSys::FirmwareType::Uefi: firmware = L"UEFI"; break;
		case WinSys::FirmwareType::Bios: firmware = L"BIOS (legacy)"; break;
	}
	Add(L"Firmware type", firmware);
	if (hw.SmbiosMajor)
		Add(L"SMBIOS version", std::format(L"{}.{}", hw.SmbiosMajor, hw.SmbiosMinor));
	else
		Add(L"SMBIOS", L"(no tables)");
}

void CSystemInfoDlg::AddProcessor() {
	auto cpu = WinSys::SystemDetails::GetProcessor();
	Add(L"Name", cpu.Brand);
	Add(L"Vendor", cpu.Vendor);
	if (!cpu.Vendor.empty())
		Add(L"Family, model, stepping", std::format(L"{} ({:X}h), {} ({:X}h), {}", cpu.Family, cpu.Family, cpu.Model, cpu.Model, cpu.Stepping));
	if (cpu.Microcode)
		Add(L"Microcode revision", std::format(L"0x{:X}", *cpu.Microcode));
	if (cpu.RatedMhz)
		Add(L"Base speed", std::format(L"{} MHz", FormatHelper::FormatNumber(cpu.RatedMhz)));

	if (cpu.Packages > 1)
		Add(L"Sockets", std::to_wstring(cpu.Packages));
	auto cores = std::to_wstring(cpu.Cores);
	if (cpu.PerformanceCores)
		cores += std::format(L" ({} performance, {} efficiency)", cpu.PerformanceCores, cpu.EfficiencyCores);
	Add(L"Cores", cores);
	Add(L"Logical processors", std::to_wstring(cpu.LogicalProcessors));
	if (cpu.Groups > 1)
		Add(L"Processor groups", std::to_wstring(cpu.Groups));
	if (cpu.NumaNodes > 1)
		Add(L"NUMA nodes", std::to_wstring(cpu.NumaNodes));
	Add(L"L1 cache", Join({ cpu.L1Data ? Size(cpu.L1Data) + L" data" : L"", cpu.L1Instruction ? Size(cpu.L1Instruction) + L" instruction" : L"" }));
	Add(L"L2 cache", Size(cpu.L2));
	Add(L"L3 cache", Size(cpu.L3));

	Add(L"Hypervisor", cpu.HypervisorPresent ? (cpu.Hypervisor.empty() ? L"Present" : cpu.Hypervisor) : L"None");
	// with a hypervisor running, Windows is its guest (the root partition's too), and the processor's virtualization
	// belongs to the hypervisor: Windows then reports no SLAT
	if (!cpu.HypervisorPresent) {
		Add(L"Virtualization", cpu.VirtualizationEnabled ? L"Enabled in firmware" : L"Disabled in firmware");
		Add(L"Second-level address translation", cpu.Slat ? L"Supported" : L"Not supported");
	}
	Add(L"Features", Join(cpu.Features));
}

void CSystemInfoDlg::AddMemory() {
	auto hw = WinSys::SystemDetails::GetHardware();
	Add(L"Installed", Size(hw.InstalledMemory));
	Add(L"Usable by Windows", Size(hw.UsableMemory));
	if (hw.MemorySlots)
		Add(L"Slots used", std::format(L"{} of {}", hw.Modules.size(), hw.MemorySlots));
	if (!hw.Modules.empty())
		Group(L"Modules");
	for (auto& m : hw.Modules) {
		std::wstring text = Join({ Size(m.Size), m.Type, m.FormFactor }, L" ");
		if (m.Speed) {
			text += std::format(L", {} MT/s", FormatHelper::FormatNumber(m.Speed));
			if (m.ConfiguredSpeed && m.ConfiguredSpeed != m.Speed)
				text += std::format(L" (running at {})", FormatHelper::FormatNumber(m.ConfiguredSpeed));
		}
		auto maker = Join({ m.Manufacturer, m.PartNumber }, L" ");
		if (!maker.empty())
			text += L", " + maker;
		auto name = m.Locator.empty() ? std::wstring(L"Module") : m.Locator;
		if (!m.BankLocator.empty() && m.BankLocator != m.Locator)
			name += L" (" + m.BankLocator + L")";
		Add(name, text);
	}
}

void CSystemInfoDlg::AddSecurity() {
	auto sec = WinSys::SystemDetails::GetSecurity();
	// AMD's processors aren't affected by MDS and branch history injection, for which Windows then reports nothing
	bool amd = WinSys::SystemDetails::GetProcessor().Vendor == L"AuthenticAMD";
	Group(L"Boot");
	if (sec.SecureBootEnabled)
		Add(L"Secure Boot", *sec.SecureBootEnabled ? L"On" : sec.SecureBootCapable.value_or(false) ? L"Off (supported)" : L"Off (not supported)");

	Group(L"Virtualization-Based Security");
	if (sec.Vbs) {
		auto& v = *sec.Vbs;
		Add(L"Virtualization-based security", v.SecureKernelRunning
			? (v.HardwareEnforced ? L"Running (hardware enforced)" : L"Running") : L"Off");
		if (v.SecureKernelRunning)
			Add(L"Firmware page protection", OnOff(v.FirmwarePageProtection));
	}
	Group(L"Code Integrity");
	if (sec.CodeIntegrity) {
		auto& ci = *sec.CodeIntegrity;
		bool hvci = ci.HvciEnabled || (sec.Vbs && sec.Vbs->HvciEnabled);
		bool strict = ci.HvciStrict || (sec.Vbs && sec.Vbs->HvciStrict);
		Add(L"Memory integrity (HVCI)", hvci ? (strict ? L"On (strict mode)" : L"On") : ci.HvciAudit ? L"Audit mode" : L"Off");
		Add(L"Kernel code integrity", OnOff(ci.Enabled));
		Add(L"Test signing", OnOff(ci.TestSigning));
		Add(L"Code integrity debug mode", OnOff(ci.DebugMode));
		Add(L"Flight signing", OnOff(ci.FlightSigning));
		Add(L"WHQL enforcement", OnOff(ci.WhqlEnforced));
		Add(L"App Control (user-mode code integrity)", ci.UmciEnforced ? L"Enforced" : ci.UmciAudit ? L"Audit mode" : L"Off");
		Add(L"Code integrity options", std::format(L"0x{:X}", ci.Options));
	}
	if (sec.SmartAppControl) {
		PCWSTR state = L"Unknown";
		switch (*sec.SmartAppControl) {
			case 0: state = L"Off"; break;
			case 1: state = L"On"; break;
			case 2: state = L"Evaluation"; break;
		}
		Add(L"Smart App Control", state);
	}
	Group(L"Credentials");
	Add(L"Credential Guard", sec.CredentialGuardRunning ? L"Running" : L"Not running");
	{
		auto ppl = sec.LsaRunAsPpl.value_or(0);
		Add(L"LSA protection", ppl == 1 ? L"On (UEFI lock)" : ppl == 2 ? L"On" : L"Off");
	}
	Group(L"Kernel");
	if (sec.KernelDebuggerEnabled)
		Add(L"Kernel debugger", !*sec.KernelDebuggerEnabled ? L"Not enabled" : sec.KernelDebuggerAttached.value_or(false) ? L"Enabled, attached" : L"Enabled");
	if (sec.ShadowStacks) {
		auto& s = *sec.ShadowStacks;
		Add(L"Kernel-mode stack protection (CET)", s.KernelEnabled ? (s.KernelAudit ? L"Audit mode" : L"On") : s.Capable ? L"Off (supported)" : L"Off (not supported)");
		if (s.Capable)
			Add(L"User-mode shadow stacks", s.UserAllowed ? L"Allowed" : L"Not allowed");
	}
	if (sec.KernelDmaProtection)
		Add(L"Kernel DMA Protection", OnOff(*sec.KernelDmaProtection));
	if (sec.MeasuredLaunch)
		Add(L"System Guard Secure Launch", OnOff(*sec.MeasuredLaunch));
	Group(L"Speculative Execution");
	if (sec.KvaShadowEnabled) {
		std::wstring text = *sec.KvaShadowEnabled ? L"On" : L"Off";
		if (sec.KvaShadowRequired)
			text += *sec.KvaShadowRequired ? L" (required by the processor)" : L" (not required by the processor)";
		Add(L"Kernel VA shadow (Meltdown)", text);
	}
	if (sec.Speculation) {
		auto& s = *sec.Speculation;
		Add(L"Branch target injection (Spectre v2)", s.BranchTargetMitigation
			? (s.EnhancedIbrs ? L"Mitigated (enhanced IBRS)" : s.Retpoline ? L"Mitigated (retpoline)" : L"Mitigated") : L"Not mitigated");
		Add(L"Speculative store bypass", s.SsbdSystemWide ? L"Disabled system-wide" : s.SsbdRequired ? L"Not disabled (required)" : L"Not disabled (not required)");
		Add(L"Microarchitectural data sampling", s.MdsHardwareProtected ? L"Not affected (hardware)" : s.MdsClearEnabled ? L"Mitigated (buffer clearing)"
			: amd ? L"Not affected (AMD)" : L"Not mitigated");
		Add(L"Branch history injection", s.BhbEnabled ? L"Mitigated" : amd ? L"Not affected (AMD)" : L"Not mitigated");
	}
	Group(L"User Account Control");
	if (sec.UacEnabled) {
		Add(L"Enabled", *sec.UacEnabled ? L"Yes" : L"No");
		if (*sec.UacEnabled && sec.UacAdminPrompt)
			Add(L"Administrators' prompt", UacPromptName(*sec.UacAdminPrompt));
	}
}

LRESULT CSystemInfoDlg::OnListCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// the groups' titles bold and shaded, as the page files dialog has them
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (auto& rows = Rows(); row < rows.size() && rows[row].Header) {
				bool dark = WTLHelper::IsDarkMode();
				cd->clrTextBk = dark ? RGB(60, 64, 72) : RGB(225, 232, 242);
				::SelectObject(cd->nmcd.hdc, m_BoldFont);
				return CDRF_NEWFONT;
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CSystemInfoDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CSystemInfoDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto text = [](Row const& r) {
		return r.Header ? L"[" + r.Name + L"]\r\n" : r.Name + L"\t" + r.Value + L"\r\n";
	};
	std::wstring all;
	auto& rows = Rows();
	if (int sel = m_List.GetNextItem(-1, LVNI_SELECTED); sel >= 0 && sel < (int)rows.size())
		all = text(rows[sel]);
	else
		for (auto& r : rows)
			all += text(r);
	if (!all.empty())
		ClipboardHelper::CopyText(m_hWnd, all.c_str());
	return 0;
}

LRESULT CSystemInfoDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
