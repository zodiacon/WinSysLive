#include "pch.h"
#include "LogonSessionsDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/Sid.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	std::wstring FormatLuid(LUID const& luid) {
		return luid.HighPart ? std::format(L"0x{:X}:{:08X}", (ULONG)luid.HighPart, luid.LowPart) : std::format(L"0x{:X}", luid.LowPart);
	}

	int64_t LuidValue(LUID const& luid) {
		return ((int64_t)luid.HighPart << 32) | luid.LowPart;
	}

	// a FILETIME of LSA's: none, never (the largest value), else the local time
	std::wstring FormatTime(LARGE_INTEGER const& time) {
		if (time.QuadPart == 0)
			return L"";
		if (time.QuadPart == MAXLONGLONG)
			return L"Never";
		return FormatHelper::FormatDateTime(time.QuadPart);
	}

	// the account's name, with the SID: or the SID alone
	std::wstring FormatSid(BYTE const* bytes) {
		WinSys::Sid sid((PSID)bytes);
		if (!::IsValidSid((PSID)bytes) || !sid)
			return L"";
		auto name = sid.UserName();
		return name.empty() ? sid.AsString() : std::format(L"{} ({})", name, sid.AsString());
	}

	// LOGON_* (ntsecapi.h), as NETLOGON_VALIDATION_SAM_INFO's UserFlags has them
	std::wstring FormatUserFlags(ULONG flags) {
		static const struct {
			ULONG Flag;
			PCWSTR Name;
		} names[] = {
			{ 0x0001, L"Guest" }, { 0x0002, L"No encryption" }, { 0x0004, L"Cached account" }, { 0x0008, L"LM password" },
			{ 0x0020, L"Extra SIDs" }, { 0x0040, L"Subauth session key" }, { 0x0080, L"Server trust account" },
			{ 0x0100, L"NTLMv2 enabled" }, { 0x0200, L"Resource groups" }, { 0x0400, L"Profile path returned" },
			{ 0x0800, L"NT v2" }, { 0x1000, L"LM v2" }, { 0x2000, L"NTLM v2" }, { 0x4000, L"Optimized" },
			{ 0x8000, L"Winlogon" }, { 0x10000, L"PKINIT" }, { 0x20000, L"Not optimized" }, { 0x40000, L"No elevation" },
			{ 0x80000, L"Managed service" },
		};
		if (flags == 0)
			return L"";
		std::wstring text;
		for (auto& n : names)
			if (flags & n.Flag)
				text += (text.empty() ? L"" : L", ") + std::wstring(n.Name);
		return std::format(L"0x{:X}", flags) + (text.empty() ? L"" : L" (" + text + L")");
	}

	std::wstring FormatUser(WinSys::LogonSessionData const& s) {
		if (s.LogonDomain.empty())
			return s.UserName;
		return s.LogonDomain + L"\\" + s.UserName;
	}
}

LRESULT CLogonSessionsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_LOGON_SESSIONS, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_LOGON_SESSIONS, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_LS_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Logon ID", 90, LVCFMT_RIGHT }, { L"User", 200, LVCFMT_LEFT }, { L"Logon Type", 120, LVCFMT_LEFT },
		{ L"Session", 60, LVCFMT_RIGHT }, { L"Package", 100, LVCFMT_LEFT }, { L"Logon Time", 140, LVCFMT_LEFT },
		{ L"Logon Server", 100, LVCFMT_LEFT }, { L"SID", 280, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	// the list view ignores column 0's alignment on insertion
	LVCOLUMN col{ LVCF_FMT };
	col.fmt = columns[0].Format;
	m_List.SetColumn(0, &col);
	ListViewHelper::EnableColumnDpiScaling(m_List);

	m_Details.Attach(GetDlgItem(IDC_LS_DETAILS));
	m_Details.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_Details.InsertColumn(0, L"Property", LVCFMT_LEFT, width(200));
	m_Details.InsertColumn(1, L"Value", LVCFMT_LEFT, width(560));
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

std::wstring CLogonSessionsDlg::GetText(WinSys::LogonSessionData const& s, Column column) const {
	if (s.Error != ERROR_SUCCESS)
		return column == Column::LogonId ? FormatLuid(s.LogonId) : column == Column::User ? L"(access denied)" : L"";

	switch (column) {
		case Column::LogonId: return FormatLuid(s.LogonId);
		case Column::User: return FormatUser(s);
		case Column::Type: return WinSys::LsaSecurity::LogonTypeName(s.LogonType);
		case Column::Session: return std::to_wstring(s.Session);
		case Column::Package: return s.AuthenticationPackage;
		case Column::LogonTime: return FormatTime(s.LogonTime);
		case Column::LogonServer: return s.LogonServer;
		case Column::Sid: return FormatSid(s.Sid);
	}
	return L"";
}

void CLogonSessionsDlg::Load() {
	CWaitCursor wait;
	auto selected = GetSelectedSession();
	std::optional<int64_t> selectedId = selected ? std::optional(LuidValue(selected->LogonId)) : std::nullopt;

	m_Sessions = WinSys::LsaSecurity::EnumLogonSessions(true);
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int denied = 0, select = -1;
	for (int i = 0; i < (int)m_Sessions.size(); i++) {
		auto& s = m_Sessions[i];
		m_List.InsertItem(i, GetText(s, Column::LogonId).c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, GetText(s, (Column)c).c_str());
		m_List.SetItemData(i, i);
		if (s.Error != ERROR_SUCCESS)
			denied++;
		if (selectedId && LuidValue(s.LogonId) == *selectedId)
			select = i;
	}
	Sort();
	if (select >= 0) {
		LVFINDINFO fi{ LVFI_PARAM };
		fi.lParam = select;
		select = m_List.FindItem(&fi, -1);
	}
	if (select < 0 && m_List.GetItemCount() > 0)
		select = 0;
	if (select >= 0) {
		m_List.SelectItem(select);
		m_List.EnsureVisible(select, FALSE);
	}
	m_List.SetRedraw(TRUE);
	ShowDetails();

	std::wstring status;
	if (m_Sessions.empty())
		status = std::format(L"No logon sessions: {}", WinSys::Helpers::GetErrorText(::GetLastError()));
	else {
		status = std::format(L"{} logon sessions", m_Sessions.size());
		if (denied)
			status += std::format(L"; {} without details: other users' sessions need elevation", denied);
	}
	SetDlgItemText(IDC_LS_STATUS, status.c_str());
}

int CLogonSessionsDlg::Compare(LPARAM index1, LPARAM index2, LPARAM param) {
	auto self = reinterpret_cast<CLogonSessionsDlg*>(param);
	auto& a = self->m_Sessions[index1];
	auto& b = self->m_Sessions[index2];
	auto column = (Column)self->m_SortColumn;
	int result;
	switch (column) {
		case Column::LogonId:
			result = LuidValue(a.LogonId) < LuidValue(b.LogonId) ? -1 : LuidValue(a.LogonId) > LuidValue(b.LogonId) ? 1 : 0;
			break;
		case Column::Session:
			result = a.Session < b.Session ? -1 : a.Session > b.Session ? 1 : 0;
			break;
		case Column::LogonTime:
			result = a.LogonTime.QuadPart < b.LogonTime.QuadPart ? -1 : a.LogonTime.QuadPart > b.LogonTime.QuadPart ? 1 : 0;
			break;
		default:
			result = ::_wcsicmp(self->GetText(a, column).c_str(), self->GetText(b, column).c_str());
			break;
	}
	return self->m_SortAscending ? result : -result;
}

void CLogonSessionsDlg::Sort() {
	// the enumeration's order until a header is clicked
	if (m_SortColumn >= 0)
		m_List.SortItems(Compare, (LPARAM)this);
}

LRESULT CLogonSessionsDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		m_SortAscending = true;
	}
	Sort();
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected >= 0)
		m_List.EnsureVisible(selected, FALSE);
	return 0;
}

WinSys::LogonSessionData const* CLogonSessionsDlg::GetSelectedSession() const {
	int row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(row);
	return index < m_Sessions.size() ? &m_Sessions[index] : nullptr;
}

void CLogonSessionsDlg::ShowDetails() {
	m_Rows.clear();
	if (auto s = GetSelectedSession()) {
		auto section = [&](PCWSTR title) { m_Rows.push_back({ title, L"", true }); };
		// rows without a value are left out
		auto row = [&](PCWSTR name, std::wstring value) {
			if (!value.empty())
				m_Rows.push_back({ name, std::move(value) });
		};
		section(L"Identity");
		row(L"Logon ID", FormatLuid(s->LogonId));
		if (s->Error != ERROR_SUCCESS)
			row(L"Details", s->Error == ERROR_ACCESS_DENIED ? L"Access denied: another user's session needs WinSysLive to run elevated"
				: WinSys::Helpers::GetErrorText(s->Error));
		else {
			row(L"User", FormatUser(*s));
			row(L"UPN", s->Upn);
			row(L"SID", FormatSid(s->Sid));
			row(L"DNS domain", s->DnsDomainName);

			section(L"Logon");
			row(L"Logon type", WinSys::LsaSecurity::LogonTypeName(s->LogonType));
			row(L"Session", std::to_wstring(s->Session));
			row(L"Authentication package", s->AuthenticationPackage);
			row(L"Logon server", s->LogonServer);
			row(L"Logon time", FormatTime(s->LogonTime));
			row(L"User flags", FormatUserFlags(s->UserFlags));

			section(L"Profile");
			row(L"Profile path", s->ProfilePath);
			row(L"Home directory", s->HomeDirectory);
			row(L"Home drive", s->HomeDirectoryDrive);
			row(L"Logon script", s->LogonScript);

			section(L"Account");
			row(L"Last successful logon", FormatTime(s->LastSuccessfulLogon));
			row(L"Last failed logon", FormatTime(s->LastFailedLogon));
			if (s->LastFailedLogon.QuadPart)
				row(L"Failed attempts since", std::to_wstring(s->FailedAttemptCountSinceLastSuccessfulLogon));
			row(L"Password last set", FormatTime(s->PasswordLastSet));
			row(L"Password can change", FormatTime(s->PasswordCanChange));
			row(L"Password must change", FormatTime(s->PasswordMustChange));
			row(L"Logoff time", FormatTime(s->LogoffTime));
			row(L"Kickoff time", FormatTime(s->KickOffTime));
		}
		// sections left empty go
		for (size_t i = 0; i < m_Rows.size(); )
			if (m_Rows[i].Header && (i + 1 == m_Rows.size() || m_Rows[i + 1].Header))
				m_Rows.erase(m_Rows.begin() + i);
			else
				i++;
	}

	m_Details.SetRedraw(FALSE);
	m_Details.DeleteAllItems();
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		m_Details.InsertItem(i, m_Rows[i].Name.c_str());
		m_Details.SetItemText(i, 1, m_Rows[i].Value.c_str());
	}
	m_Details.SetRedraw(TRUE);
}

LRESULT CLogonSessionsDlg::OnSessionChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
		ShowDetails();
	return 0;
}

LRESULT CLogonSessionsDlg::OnListCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// sessions without details grayed
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (size_t)cd->nmcd.lItemlParam;
			if (index < m_Sessions.size() && m_Sessions[index].Error != ERROR_SUCCESS)
				cd->clrText = ::GetSysColor(COLOR_GRAYTEXT);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CLogonSessionsDlg::OnDetailsCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
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

LRESULT CLogonSessionsDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_List || hdr->hwndFrom == m_Details)
		m_CopyList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CLogonSessionsDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CLogonSessionsDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	if (m_CopyList == m_Details) {
		// the sections as [titles], the properties tab separated
		for (auto& r : m_Rows)
			text += r.Header ? L"[" + r.Name + L"]\r\n" : r.Name + L"\t" + r.Value + L"\r\n";
	}
	else {
		// every session, in the list's order, with the column names first
		WCHAR name[64];
		for (int c = 0; c < (int)Column::_Count; c++) {
			LVCOLUMN col{ LVCF_TEXT };
			col.pszText = name;
			col.cchTextMax = _countof(name);
			m_List.GetColumn(c, &col);
			text += (c ? L"\t" : L"") + std::wstring(name);
		}
		text += L"\r\n";
		for (int i = 0; i < m_List.GetItemCount(); i++) {
			auto& s = m_Sessions[m_List.GetItemData(i)];
			for (int c = 0; c < (int)Column::_Count; c++)
				text += (c ? L"\t" : L"") + GetText(s, (Column)c);
			text += L"\r\n";
		}
	}
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CLogonSessionsDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
