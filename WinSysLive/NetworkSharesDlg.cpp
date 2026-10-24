#include "pch.h"
#include "NetworkSharesDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <lm.h>

namespace {
	std::wstring Uses(std::optional<uint32_t> const& uses) {
		if (!uses)
			return L"";
		return *uses == UINT32_MAX ? std::wstring(L"Unlimited") : FormatHelper::FormatNumber(*uses);
	}

	PCWSTR YesNo(bool value) {
		return value ? L"Yes" : L"No";
	}

	bool IsDisk(WinSys::NetworkShare const& share) {
		return (share.Type & STYPE_MASK) == STYPE_DISKTREE && !share.Path.empty();
	}
}

LRESULT CNetworkSharesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_SHARE, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_SHARE, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_SH_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Name", 120, LVCFMT_LEFT }, { L"Path", 220, LVCFMT_LEFT }, { L"Type", 130, LVCFMT_LEFT },
		{ L"Uses", 60, LVCFMT_RIGHT }, { L"Maximum", 80, LVCFMT_RIGHT }, { L"Comment", 200, LVCFMT_LEFT },
	};
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	m_Details.Attach(GetDlgItem(IDC_SH_DETAILS));
	m_Details.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_Details.InsertColumn(0, L"Property", LVCFMT_LEFT, width(200));
	m_Details.InsertColumn(1, L"Value", LVCFMT_LEFT, width(480));
	ListViewHelper::EnableColumnDpiScaling(m_Details);

	// the sections' titles
	LOGFONT lf;
	CFontHandle(m_Details.GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&lf);

	m_CopyList = m_List;
	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CNetworkSharesDlg::Load() {
	CWaitCursor wait;
	auto selected = GetSelectedShare();
	auto selectedName = selected ? selected->Name : std::wstring();

	uint32_t level = 0;
	auto shares = WinSys::NetworkShares::Enum(level);
	auto error = shares ? ERROR_SUCCESS : ::GetLastError();
	m_Shares = shares ? std::move(*shares) : std::vector<WinSys::NetworkShare>();
	m_Level = level;
	std::ranges::sort(m_Shares, [](auto& a, auto& b) { return ::_wcsicmp(a.Name.c_str(), b.Name.c_str()) < 0; });

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int select = -1;
	for (int i = 0; i < (int)m_Shares.size(); i++) {
		auto& s = m_Shares[i];
		m_List.InsertItem(i, s.Name.c_str());
		m_List.SetItemText(i, 1, s.Path.c_str());
		m_List.SetItemText(i, 2, WinSys::NetworkShares::TypeName(s.Type).c_str());
		m_List.SetItemText(i, 3, Uses(s.CurrentUses).c_str());
		m_List.SetItemText(i, 4, Uses(s.MaxUses).c_str());
		m_List.SetItemText(i, 5, s.Remark.c_str());
		m_List.SetItemData(i, i);
		if (::_wcsicmp(s.Name.c_str(), selectedName.c_str()) == 0)
			select = i;
	}
	if (select < 0 && !m_Shares.empty())
		select = 0;
	if (select >= 0) {
		m_List.SelectItem(select);
		m_List.EnsureVisible(select, FALSE);
	}
	m_List.SetRedraw(TRUE);
	ShowDetails();

	std::wstring status;
	if (!shares)
		status = std::format(L"Reading the shares failed: {}", WinSys::Helpers::GetErrorText(error));
	else {
		status = std::format(L"{} share{}", m_Shares.size(), m_Shares.size() == 1 ? L"" : L"s");
		if (level == 1)
			status += L"; the paths, uses and permissions need administrator rights";
	}
	SetDlgItemText(IDC_SH_STATUS, status.c_str());
}

WinSys::NetworkShare const* CNetworkSharesDlg::GetSelectedShare() const {
	int row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(row);
	return index < m_Shares.size() ? &m_Shares[index] : nullptr;
}

void CNetworkSharesDlg::ShowDetails() {
	m_Rows.clear();
	if (auto s = GetSelectedShare()) {
		auto section = [&](PCWSTR title) { m_Rows.push_back({ title, L"", true }); };
		// rows without a value are left out
		auto row = [&](PCWSTR name, std::wstring value) {
			if (!value.empty())
				m_Rows.push_back({ name, std::move(value) });
		};
		section(L"Share");
		row(L"Name", s->Name);
		row(L"Path", s->Path);
		row(L"Type", WinSys::NetworkShares::TypeName(s->Type));
		row(L"Comment", s->Remark);
		row(L"Current uses", Uses(s->CurrentUses));
		row(L"Maximum uses", Uses(s->MaxUses));

		if (s->Flags) {
			auto flags = *s->Flags;
			section(L"Settings");
			row(L"Offline caching", WinSys::NetworkShares::CachingName(flags));
			row(L"Access-based enumeration", YesNo(flags & SHI1005_FLAGS_ACCESS_BASED_DIRECTORY_ENUM));
			row(L"Encrypted", YesNo(flags & SHI1005_FLAGS_ENCRYPT_DATA));
			if (flags & SHI1005_FLAGS_FORCE_LEVELII_OPLOCK)
				row(L"Oplocks", L"Level II only");
			if (flags & SHI1005_FLAGS_ALLOW_NAMESPACE_CACHING)
				row(L"Namespace caching", L"Allowed");
			if (flags & SHI1005_FLAGS_COMPRESS_DATA)
				row(L"Compressed", L"Yes");
		}

		section(L"Permissions");
		if (m_Level != 502)
			row(L"Share permissions", L"Need administrator rights (File > Run as Administrator)");
		else if (!s->HasPermissions || s->Permissions.empty())
			// no security descriptor (the system's shares) or no DACL
			row(L"Share permissions", s->HasPermissions ? L"Everyone: Full Control (no access list)" : L"Not set (the system's share)");
		else
			for (auto& p : s->Permissions)
				m_Rows.push_back({ p.Trustee, std::format(L"{} {}", p.Allow ? L"Allow" : L"Deny", WinSys::NetworkShares::RightsName(p.Mask)) });
	}

	m_Details.SetRedraw(FALSE);
	m_Details.DeleteAllItems();
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		m_Details.InsertItem(i, m_Rows[i].Name.c_str());
		m_Details.SetItemText(i, 1, m_Rows[i].Value.c_str());
	}
	m_Details.SetRedraw(TRUE);
}

LRESULT CNetworkSharesDlg::OnShareChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
		ShowDetails();
	return 0;
}

LRESULT CNetworkSharesDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	// a disk share's folder, in Explorer
	if (auto s = GetSelectedShare(); s && IsDisk(*s))
		::ShellExecute(m_hWnd, L"open", s->Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CNetworkSharesDlg::OnDetailsCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
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

LRESULT CNetworkSharesDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_List || hdr->hwndFrom == m_Details)
		m_CopyList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CNetworkSharesDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CNetworkSharesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	if (m_CopyList == m_Details) {
		// the sections as [titles], the properties tab separated
		for (auto& r : m_Rows)
			text += r.Header ? L"[" + r.Name + L"]\r\n" : r.Name + L"\t" + r.Value + L"\r\n";
	}
	else {
		// every share, the column names first
		WCHAR buffer[512];
		int columns = m_List.GetHeader().GetItemCount();
		for (int c = 0; c < columns; c++) {
			LVCOLUMN col{ LVCF_TEXT };
			col.pszText = buffer;
			col.cchTextMax = _countof(buffer);
			m_List.GetColumn(c, &col);
			text += (c ? L"\t" : L"") + std::wstring(buffer);
		}
		text += L"\r\n";
		for (int i = 0; i < m_List.GetItemCount(); i++) {
			for (int c = 0; c < columns; c++) {
				m_List.GetItemText(i, c, buffer, _countof(buffer));
				text += (c ? L"\t" : L"") + std::wstring(buffer);
			}
			text += L"\r\n";
		}
	}
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CNetworkSharesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
