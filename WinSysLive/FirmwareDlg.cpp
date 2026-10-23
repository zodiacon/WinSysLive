#include "pch.h"
#include "FirmwareDlg.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	std::wstring GuidText(GUID const& guid) {
		WCHAR text[64];
		return ::StringFromGUID2(guid, text, _countof(text)) ? text : L"";
	}

	PCWSTR TpmInterfaceName(uint32_t type) {
		// TPM_IFTYPE_*
		switch (type) {
			case 1: return L"TPM 1.2 ports";
			case 2: return L"TrustZone";
			case 3: return L"Hardware (TIS or CRB)";
			case 4: return L"Emulator";
			case 5: return L"SPB";
		}
		return L"Unknown";
	}
}

LRESULT CFirmwareDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Tabs.Attach(GetDlgItem(IDC_FW_TABS));
	m_Tabs.InsertItem(0, L"Summary");
	m_Tabs.InsertItem(1, L"UEFI Variables");

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_Summary.Attach(GetDlgItem(IDC_FW_SUMMARY));
	m_Summary.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_Summary.InsertColumn(0, L"Property", LVCFMT_LEFT, width(200));
	m_Summary.InsertColumn(1, L"Value", LVCFMT_LEFT, width(500));
	ListViewHelper::EnableColumnDpiScaling(m_Summary);

	m_Variables.Attach(GetDlgItem(IDC_FW_VARS));
	m_Variables.ModifyStyle(0, LVS_NOSORTHEADER);
	m_Variables.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_Variables.InsertColumn(0, L"Name", LVCFMT_LEFT, width(220));
	m_Variables.InsertColumn(1, L"Vendor", LVCFMT_LEFT, width(270));
	m_Variables.InsertColumn(2, L"Attributes", LVCFMT_LEFT, width(120));
	m_Variables.InsertColumn(3, L"Size", LVCFMT_RIGHT, width(70));
	ListViewHelper::EnableColumnDpiScaling(m_Variables);

	// the sections' titles; the dump in a fixed-pitch font
	LOGFONT lf;
	CFontHandle(m_Summary.GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&lf);
	m_Value.Attach(GetDlgItem(IDC_FW_VALUE));
	m_HexFont.CreateFont(-::MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
	m_Value.SetFont(m_HexFont);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	ShowTab(0);
	return TRUE;
}

void CFirmwareDlg::ShowTab(int tab) {
	m_Summary.ShowWindow(tab == 0 ? SW_SHOW : SW_HIDE);
	m_Variables.ShowWindow(tab == 1 ? SW_SHOW : SW_HIDE);
	m_Value.ShowWindow(tab == 1 ? SW_SHOW : SW_HIDE);
}

LRESULT CFirmwareDlg::OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	ShowTab(m_Tabs.GetCurSel());
	return 0;
}

std::wstring CFirmwareDlg::VendorName(GUID const& vendor) {
	static const struct {
		GUID Vendor;
		PCWSTR Name;
	} known[] = {
		{ WinSys::Firmware::GlobalVariableVendor(), L"EFI Global" },
		{ { 0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f } }, L"Image Security Database" },
		{ { 0x77fa9abd, 0x0359, 0x4d32, { 0xbd, 0x60, 0x28, 0xf4, 0xe7, 0x8f, 0x78, 0x4b } }, L"Microsoft" },
	};
	auto text = GuidText(vendor);
	for (auto& k : known)
		if (::IsEqualGUID(k.Vendor, vendor))
			return std::format(L"{} {}", k.Name, text);
	return text;
}

std::wstring CFirmwareDlg::AttributesText(uint32_t attributes) {
	// EFI_VARIABLE_*
	static const struct {
		uint32_t Flag;
		PCWSTR Name;
	} names[] = {
		{ 0x01, L"NV" }, { 0x02, L"BS" }, { 0x04, L"RT" }, { 0x08, L"HR" }, { 0x10, L"AW" }, { 0x20, L"TA" }, { 0x40, L"AP" },
	};
	std::wstring text;
	for (auto& n : names)
		if (attributes & n.Flag)
			text += (text.empty() ? L"" : L" ") + std::wstring(n.Name);
	return text;
}

std::wstring CFirmwareDlg::HexDump(std::vector<uint8_t> const& data) {
	std::wstring text;
	for (size_t offset = 0; offset < data.size(); offset += 16) {
		text += std::format(L"{:04X}  ", offset);
		std::wstring ascii;
		for (size_t i = 0; i < 16; i++) {
			if (offset + i < data.size()) {
				auto b = data[offset + i];
				text += std::format(L"{:02X} ", b);
				ascii += b >= 0x20 && b < 0x7f ? (wchar_t)b : L'.';
			}
			else
				text += L"   ";
		}
		text += L" " + ascii + L"\r\n";
	}
	return text;
}

void CFirmwareDlg::Load() {
	CWaitCursor wait;
	m_Rows.clear();
	auto section = [&](PCWSTR title) { m_Rows.push_back({ title, L"", true }); };
	auto row = [&](std::wstring name, std::wstring value) { m_Rows.push_back({ std::move(name), std::move(value) }); };
	auto yesNo = [](std::optional<bool> value) { return value ? (*value ? L"On" : L"Off") : L"Unknown"; };

	// the firmware
	auto type = WinSys::Firmware::GetType();
	section(L"Firmware");
	row(L"Type", type == WinSys::FirmwareType::Uefi ? L"UEFI" : type == WinSys::FirmwareType::Bios ? L"BIOS" : L"Unknown");
	auto vars = type == WinSys::FirmwareType::Uefi ? WinSys::Firmware::EnumVariables() : std::nullopt;
	DWORD varsError = vars ? ERROR_SUCCESS : ::GetLastError();
	m_Vars = vars ? std::move(*vars) : std::vector<WinSys::UefiVariable>();
	std::ranges::sort(m_Vars, [](auto& a, auto& b) { return ::_wcsicmp(a.Name.c_str(), b.Name.c_str()) < 0; });

	if (vars) {
		auto config = WinSys::Firmware::GetBootConfiguration(m_Vars);
		row(L"Secure Boot", yesNo(config.SecureBoot));
		row(L"Setup Mode", yesNo(config.SetupMode));
		row(L"Boot timeout", config.Timeout ? std::format(L"{} seconds", *config.Timeout) : L"");
		row(L"Current boot entry", config.Current ? std::format(L"Boot{:04X}", *config.Current) : L"");
		row(L"Next boot entry", config.Next ? std::format(L"Boot{:04X}", *config.Next) : L"(none set)");

		section(L"Boot Entries (in boot order)");
		for (auto& e : config.Entries) {
			std::wstring name = std::format(L"Boot{:04X}", e.Number);
			if (config.Current && *config.Current == e.Number)
				name += L" (current)";
			std::wstring value = e.Description;
			std::wstring flags = (e.Attributes & 1) ? L"active" : L"inactive";
			if (e.Attributes & 8)
				flags += L", hidden";
			if (e.Order < 0)
				flags += L", not in the boot order";
			value += L"  [" + flags + L"]";
			if (!e.FilePath.empty())
				value += L"  " + e.FilePath;
			row(name, value);
		}
	}
	else if (type == WinSys::FirmwareType::Uefi)
		row(L"UEFI variables", varsError == ERROR_PRIVILEGE_NOT_HELD
			? L"Need administrator rights (SeSystemEnvironmentPrivilege): run WinSysLive elevated"
			: WinSys::Helpers::GetErrorText(varsError));

	// the TPM
	auto tpm = WinSys::Tpm::GetInfo();
	section(L"TPM");
	if (!tpm.Present)
		row(L"Present", std::format(L"No (TBS error 0x{:08X})", tpm.Error));
	else {
		row(L"Version", tpm.Version == 2 ? L"2.0" : tpm.Version == 1 ? L"1.2" : std::to_wstring(tpm.Version));
		row(L"Interface", TpmInterfaceName(tpm.InterfaceType));
		row(L"Manufacturer", tpm.Manufacturer);
		row(L"Vendor", tpm.VendorString);
		if (tpm.FirmwareVersion1 || tpm.FirmwareVersion2)
			row(L"Firmware version", std::format(L"{}.{}.{}.{}", tpm.FirmwareVersion1 >> 16, tpm.FirmwareVersion1 & 0xffff,
				tpm.FirmwareVersion2 >> 16, tpm.FirmwareVersion2 & 0xffff));
		if (!tpm.Family.empty())
			row(L"Specification", std::format(L"{}, level {}, revision {}.{:02}, {} (day {})", tpm.Family, tpm.Level,
				tpm.Revision / 100, tpm.Revision % 100, tpm.Year, tpm.DayOfYear));
		row(L"Implementation revision", std::to_wstring(tpm.ImplementationRevision));
		if (tpm.Error)
			row(L"Query", std::format(L"Failed (0x{:08X}): the properties are missing", tpm.Error));
	}

	m_Summary.DeleteAllItems();
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		m_Summary.InsertItem(i, m_Rows[i].Name.c_str());
		m_Summary.SetItemText(i, 1, m_Rows[i].Value.c_str());
	}

	m_Variables.DeleteAllItems();
	for (int i = 0; i < (int)m_Vars.size(); i++) {
		auto& v = m_Vars[i];
		m_Variables.InsertItem(i, v.Name.c_str());
		m_Variables.SetItemText(i, 1, VendorName(v.Vendor).c_str());
		m_Variables.SetItemText(i, 2, AttributesText(v.Attributes).c_str());
		m_Variables.SetItemText(i, 3, std::to_wstring(v.Value.size()).c_str());
		m_Variables.SetItemData(i, i);
	}
	ShowValue();
	SetDlgItemText(IDC_FW_STATUS, vars ? std::format(L"{} UEFI variables", m_Vars.size()).c_str()
		: type == WinSys::FirmwareType::Uefi ? L"The UEFI variables need administrator rights" : L"No UEFI variables: not a UEFI system");
}

WinSys::UefiVariable const* CFirmwareDlg::GetSelectedVariable() const {
	int row = m_Variables.GetNextItem(-1, LVNI_SELECTED);
	if (row < 0)
		return nullptr;
	auto index = (size_t)m_Variables.GetItemData(row);
	return index < m_Vars.size() ? &m_Vars[index] : nullptr;
}

void CFirmwareDlg::ShowValue() {
	auto v = GetSelectedVariable();
	m_Value.SetWindowText(v ? HexDump(v->Value).c_str() : L"");
}

LRESULT CFirmwareDlg::OnVariableChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if ((lv->uChanged & LVIF_STATE) && ((lv->uOldState ^ lv->uNewState) & LVIS_SELECTED))
		ShowValue();
	return 0;
}

LRESULT CFirmwareDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// the sections' titles bold and shaded, as the handle sheet's General page has them
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (row < m_Rows.size() && m_Rows[row].Header) {
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

LRESULT CFirmwareDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CFirmwareDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	if (m_Tabs.GetCurSel() == 0) {
		// the sections as [titles], the properties tab separated
		for (auto& r : m_Rows)
			text += r.Header ? L"[" + r.Name + L"]\r\n" : r.Name + L"\t" + r.Value + L"\r\n";
	}
	else if (auto v = GetSelectedVariable())
		text = std::format(L"{} {}\t{}\t{} bytes\r\n", v->Name, VendorName(v->Vendor), AttributesText(v->Attributes), v->Value.size())
			+ HexDump(v->Value);
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CFirmwareDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
