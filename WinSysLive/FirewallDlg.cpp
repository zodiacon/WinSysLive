#include "pch.h"
#include "FirewallDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <netfw.h>

using namespace WinSys;

LRESULT CFirewallDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_FIREWALL, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_FIREWALL, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_FWR_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Name", 260, LVCFMT_LEFT }, { L"Group", 180, LVCFMT_LEFT }, { L"Direction", 75, LVCFMT_LEFT },
		{ L"Action", 55, LVCFMT_LEFT }, { L"Enabled", 55, LVCFMT_LEFT }, { L"Profiles", 110, LVCFMT_LEFT },
		{ L"Protocol", 60, LVCFMT_LEFT }, { L"Local Ports", 90, LVCFMT_LEFT }, { L"Remote Ports", 90, LVCFMT_LEFT },
		{ L"Local Addresses", 110, LVCFMT_LEFT }, { L"Remote Addresses", 110, LVCFMT_LEFT }, { L"Program", 280, LVCFMT_LEFT },
		{ L"Service", 90, LVCFMT_LEFT }, { L"Package", 120, LVCFMT_LEFT }, { L"Interfaces", 70, LVCFMT_LEFT },
		{ L"Edge Traversal", 85, LVCFMT_LEFT }, { L"Description", 300, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	::SendDlgItemMessage(m_hWnd, IDC_FWR_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name, group, program, service, port or description");
	CheckDlgButton(IDC_FWR_INBOUND, BST_CHECKED);
	CheckDlgButton(IDC_FWR_OUTBOUND, BST_CHECKED);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CFirewallDlg::Load() {
	if (m_Load)
		return;
	GetDlgItem(IDC_FWR_REFRESH).EnableWindow(FALSE);
	SetDlgItemText(IDC_FWR_STATUS, L"Reading the firewall rules...");
	// the thread holds the state, not the dialog, so closing needn't wait for it
	m_Load = std::make_shared<LoadState>();
	std::thread([state = m_Load, hWnd = m_hWnd] {
		auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		auto rules = Firewall::EnumRules();
		auto error = rules ? 0 : ::GetLastError();
		auto policy = Firewall::GetPolicy();
		if (SUCCEEDED(com))
			::CoUninitialize();
		{
			std::lock_guard lock(state->Lock);
			state->Rules = std::move(rules);
			state->Policy = std::move(policy);
			state->Error = error;
		}
		if (::IsWindow(hWnd))
			::PostMessage(hWnd, WM_FIREWALL_READ, 0, 0);
		}).detach();
}

LRESULT CFirewallDlg::OnRulesRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (!m_Load)
		return 0;
	{
		std::lock_guard lock(m_Load->Lock);
		m_Error = m_Load->Rules ? 0 : m_Load->Error;
		m_Rules = m_Load->Rules ? std::move(*m_Load->Rules) : std::vector<FirewallRule>{};
		m_Policy = std::move(m_Load->Policy);
	}
	m_Load.reset();
	m_Processes = m_Running ? m_Running() : decltype(m_Processes){};
	GetDlgItem(IDC_FWR_REFRESH).EnableWindow(TRUE);
	ShowPolicy();
	ShowRows();
	return 0;
}

void CFirewallDlg::ShowPolicy() {
	if (!m_Policy) {
		SetDlgItemText(IDC_FWR_PROFILES, L"The firewall's profiles can't be read");
		return;
	}
	std::wstring text;
	for (auto& p : m_Policy->Profiles) {
		if (!text.empty())
			text += L"  |  ";
		text += Firewall::ProfilesText(p.Type);
		if (m_Policy->CurrentProfiles & p.Type)
			text += L" (active)";
		text += L": ";
		if (!p.Enabled)
			text += L"off";
		else
			text += std::format(L"on, inbound {}, outbound {}", p.BlockAllInbound ? L"all blocked" : p.InboundAllowed ? L"allowed" : L"blocked",
				p.OutboundAllowed ? L"allowed" : L"blocked");
	}
	SetDlgItemText(IDC_FWR_PROFILES, text.c_str());
}

std::wstring CFirewallDlg::ExpandPath(std::wstring const& path) {
	if (path.empty())
		return path;
	WCHAR expanded[MAX_PATH * 2];
	std::wstring result = ::ExpandEnvironmentStrings(path.c_str(), expanded, _countof(expanded)) ? expanded : path;
	::CharLowerBuff(result.data(), (DWORD)result.size());
	return result;
}

std::vector<uint32_t> const* CFirewallDlg::GetProcesses(FirewallRule const& r) const {
	if (r.Application.empty())
		return nullptr;
	auto it = m_Processes.find(ExpandPath(r.Application));
	return it == m_Processes.end() ? nullptr : &it->second;
}

std::wstring CFirewallDlg::GetText(FirewallRule const& r, Column column) const {
	auto any = [](std::wstring const& s) { return s.empty() || s == L"*" ? std::wstring(L"Any") : s; };
	switch (column) {
		case Column::Name: return r.Name;
		case Column::Group: return r.Grouping;
		case Column::Direction: return r.Inbound ? L"Inbound" : L"Outbound";
		case Column::Action: return r.Allow ? L"Allow" : L"Block";
		case Column::Enabled: return r.Enabled ? L"Yes" : L"No";
		case Column::Profiles: return Firewall::ProfilesText(r.Profiles);
		case Column::Protocol: return Firewall::ProtocolName(r.Protocol) + (r.IcmpTypes.empty() || r.IcmpTypes == L"*" ? L"" : L" (" + r.IcmpTypes + L")");
		case Column::LocalPorts: return r.Protocol == 6 || r.Protocol == 17 ? any(r.LocalPorts) : L"";
		case Column::RemotePorts: return r.Protocol == 6 || r.Protocol == 17 ? any(r.RemotePorts) : L"";
		case Column::LocalAddresses: return any(r.LocalAddresses);
		case Column::RemoteAddresses: return any(r.RemoteAddresses);
		case Column::Program: return r.Application;
		case Column::Service: return r.Service == L"*" ? L"(services)" : r.Service;
		case Column::Package: return r.PackageId;
		case Column::Interfaces: return r.InterfaceTypes;
		case Column::EdgeTraversal: return r.EdgeTraversal ? L"Yes" : L"";
		case Column::Description: return r.Description;
	}
	return L"";
}

void CFirewallDlg::ShowRows() {
	bool inbound = IsDlgButtonChecked(IDC_FWR_INBOUND) == BST_CHECKED;
	bool outbound = IsDlgButtonChecked(IDC_FWR_OUTBOUND) == BST_CHECKED;
	bool enabledOnly = IsDlgButtonChecked(IDC_FWR_ENABLEDONLY) == BST_CHECKED;
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int row = 0;
	size_t enabled = 0, in = 0;
	for (int i = 0; i < (int)m_Rules.size(); i++) {
		auto& r = m_Rules[i];
		enabled += r.Enabled;
		in += r.Inbound;
		if ((r.Inbound ? !inbound : !outbound) || (enabledOnly && !r.Enabled))
			continue;
		if (!m_Filter.empty()) {
			auto text = r.Name + L"\n" + r.Grouping + L"\n" + r.Application + L"\n" + r.Service + L"\n" + r.LocalPorts + L"\n" + r.RemotePorts + L"\n" + r.Description;
			::CharLowerBuff(text.data(), (DWORD)text.size());
			if (text.find(m_Filter) == std::wstring::npos)
				continue;
		}
		m_List.InsertItem(row, r.Name.c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(row, c, GetText(r, (Column)c).c_str());
		m_List.SetItemData(row, i);
		row++;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	m_List.SetRedraw(TRUE);

	std::wstring status;
	if (m_Error)
		status = L"Can't read the firewall rules: " + Helpers::GetErrorText(m_Error);
	else {
		status = std::format(L"{} rules ({} inbound, {} outbound), {} enabled", m_Rules.size(), in, m_Rules.size() - in, enabled);
		if (row != (int)m_Rules.size())
			status += std::format(L"; {} shown", row);
	}
	SetDlgItemText(IDC_FWR_STATUS, status.c_str());
}

int CALLBACK CFirewallDlg::Compare(LPARAM index1, LPARAM index2, LPARAM param) {
	auto self = reinterpret_cast<CFirewallDlg*>(param);
	auto& a = self->m_Rules[index1];
	auto& b = self->m_Rules[index2];
	auto column = (Column)self->m_SortColumn;
	int result = ::_wcsicmp(self->GetText(a, column).c_str(), self->GetText(b, column).c_str());
	if (result == 0)
		result = ::_wcsicmp(a.Name.c_str(), b.Name.c_str());
	return self->m_SortAscending ? result : -result;
}

LRESULT CFirewallDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		m_SortAscending = true;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	return 0;
}

LRESULT CFirewallDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (size_t)cd->nmcd.lItemlParam;
			if (index >= m_Rules.size())
				break;
			auto& r = m_Rules[index];
			bool dark = WTLHelper::IsDarkMode();
			// disabled rules gray, block rules red
			if (!r.Enabled)
				cd->clrText = dark ? RGB(128, 128, 128) : RGB(140, 140, 140);
			else if (!r.Allow)
				cd->clrText = dark ? RGB(255, 110, 110) : RGB(190, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

std::vector<size_t> CFirewallDlg::GetSelected() const {
	std::vector<size_t> rules;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED))
		rules.push_back((size_t)m_List.GetItemData(i));
	return rules;
}

LRESULT CFirewallDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the focused row
		RECT rc{};
		int row = m_List.GetNextItem(-1, LVNI_FOCUSED);
		if (row >= 0 && m_List.GetItemRect(row, &rc, LVIR_LABEL))
			pt = { rc.left, rc.bottom };
		m_List.ClientToScreen(&pt);
	}
	auto selected = GetSelected();
	bool anyEnabled = false, anyDisabled = false;
	for (auto i : selected)
		(m_Rules[i].Enabled ? anyEnabled : anyDisabled) = true;
	// changing the firewall needs elevation
	bool elevated = SecurityHelper::IsRunningElevated();
	auto first = selected.empty() ? nullptr : &m_Rules[selected[0]];
	auto program = first ? ExpandPath(first->Application) : std::wstring();
	auto processes = first ? GetProcesses(*first) : nullptr;

	enum { Enable = 1, Disable, OpenLocation, GoToProcess, Copy };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (elevated && anyDisabled ? 0 : MF_GRAYED), Enable, selected.size() > 1 ? L"&Enable Rules" : L"&Enable Rule");
	menu.AppendMenu(MF_STRING | (elevated && anyEnabled ? 0 : MF_GRAYED), Disable, selected.size() > 1 ? L"&Disable Rules" : L"&Disable Rule");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (!program.empty() && program != L"system" ? 0 : MF_GRAYED), OpenLocation, L"Open File &Location");
	menu.AppendMenu(MF_STRING | (processes && m_GoToProcess ? 0 : MF_GRAYED), GoToProcess, L"&Go to Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, Copy, selected.empty() ? L"&Copy All" : L"&Copy");
	switch (::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd, nullptr)) {
		case Enable: SetEnabled(true); break;
		case Disable: SetEnabled(false); break;
		case OpenLocation:
		{
			// the folder, with the file selected
			auto path = first->Application;
			WCHAR expanded[MAX_PATH * 2];
			if (::ExpandEnvironmentStrings(path.c_str(), expanded, _countof(expanded)))
				path = expanded;
			if ((INT_PTR)::ShellExecute(m_hWnd, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL) <= 32)
				AtlMessageBox(m_hWnd, std::format(L"Can't open the location of {}", path).c_str(), IDR_MAINFRAME, MB_ICONERROR);
			break;
		}
		case GoToProcess:
		{
			auto pid = processes->front();
			EndDialog(IDOK);
			m_GoToProcess(pid);
			break;
		}
		case Copy:
		{
			BOOL handled;
			OnCopy(0, 0, nullptr, handled);
			break;
		}
	}
	return 0;
}

void CFirewallDlg::SetEnabled(bool enabled) {
	CWaitCursor wait;
	auto selected = GetSelected();
	int changed = 0, failed = 0;
	DWORD error = 0;
	for (auto i : selected) {
		auto& r = m_Rules[i];
		if (r.Enabled == enabled)
			continue;
		if (Firewall::SetEnabled(r, enabled)) {
			r.Enabled = enabled;
			changed++;
		}
		else {
			failed++;
			error = ::GetLastError();
		}
	}
	// the rows in place: their Enabled text and colors
	for (int row = m_List.GetNextItem(-1, LVNI_SELECTED); row >= 0; row = m_List.GetNextItem(row, LVNI_SELECTED))
		m_List.SetItemText(row, (int)Column::Enabled, GetText(m_Rules[m_List.GetItemData(row)], Column::Enabled).c_str());
	m_List.Invalidate();
	auto status = std::format(L"{} rule{} {}", changed, changed == 1 ? L"" : L"s", enabled ? L"enabled" : L"disabled");
	if (failed)
		status += std::format(L"; {} failed: {}", failed, error == ERROR_NOT_FOUND ? std::wstring(L"the rule isn't there anymore (Refresh)") : Helpers::GetErrorText(error));
	SetDlgItemText(IDC_FWR_STATUS, status.c_str());
}

LRESULT CFirewallDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_FWR_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CFirewallDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CFirewallDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the selected rows, else every row shown; the column names first
	bool selected = m_List.GetSelectedCount() > 0;
	std::wstring text;
	for (int c = 0; c < (int)Column::_Count; c++) {
		WCHAR name[64]{};
		LVCOLUMN column{ LVCF_TEXT };
		column.pszText = name;
		column.cchTextMax = _countof(name);
		m_List.GetColumn(c, &column);
		text += (c ? L"\t" : L"") + std::wstring(name);
	}
	text += L"\r\n";
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& r = m_Rules[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(r, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CFirewallDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
