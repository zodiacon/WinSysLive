#include "pch.h"
#include "NetworkInfoDlg.h"
#include "FormatHelper.h"
#include <WinSys/Connections.h>
#include <WinSys/NetworkTables.h>
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

LRESULT CNetworkInfoDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_NETWORK, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_NETWORK, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_Tabs.Attach(GetDlgItem(IDC_NI_TABS));
	static const PCWSTR tabs[] = { L"Listening Ports", L"DNS Cache", L"Neighbors (ARP)", L"Routes" };
	static_assert(_countof(tabs) == (int)Tab::_Count);
	for (int i = 0; i < _countof(tabs); i++)
		m_Tabs.InsertItem(i, tabs[i]);

	m_List.Attach(GetDlgItem(IDC_NI_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	// above the tab control, which would otherwise paint over them when the selection changes (its WS_CLIPSIBLINGS
	// clips only the siblings above it)
	m_List.SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	GetDlgItem(IDC_NI_FILTER).SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	::SendDlgItemMessage(m_hWnd, IDC_NI_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter");

	m_Tables[(int)Tab::Listening].Columns = { { L"Process", 180 }, { L"PID", 70, true }, { L"Protocol", 70 },
		{ L"Address", 200 }, { L"Port", 70, true } };
	m_Tables[(int)Tab::Listening].SortColumn = 4;	// by port
	m_Tables[(int)Tab::Dns].Columns = { { L"Name", 260 }, { L"Type", 70 }, { L"Data", 280 }, { L"TTL", 70, true }, { L"Section", 80 } };
	m_Tables[(int)Tab::Neighbors].Columns = { { L"Address", 220 }, { L"Physical Address", 150 }, { L"State", 90 },
		{ L"Router", 60 }, { L"Interface", 220 } };
	m_Tables[(int)Tab::Routes].Columns = { { L"Destination", 220 }, { L"Next Hop", 180 }, { L"Metric", 70, true },
		{ L"Interface Metric", 100, true }, { L"Protocol", 130 }, { L"Interface", 200 } };

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	BOOL handled;
	OnRefresh(0, 0, nullptr, handled);
	return TRUE;
}

void CNetworkInfoDlg::LoadListening() {
	auto& table = m_Tables[(int)Tab::Listening];
	table.Rows.clear();
	table.Error.clear();
	auto connections = WinSys::ConnectionInfo::Read();
	if (!connections) {
		table.Error = WinSys::Helpers::GetErrorText(::GetLastError());
		return;
	}
	for (auto& c : *connections) {
		// listening TCP sockets (MIB_TCP_STATE_LISTEN) and every UDP endpoint
		if (c.IsTcp() && c.State != 2)
			continue;
		auto pid = c.Id.Pid;
		auto name = pid == 0 ? std::wstring(L"Idle") : pid == 4 ? std::wstring(L"System") : m_ProcessName ? m_ProcessName(pid) : L"";
		table.Rows.push_back({ { name, std::to_wstring(pid), WinSys::ConnectionInfo::ProtocolName(c.Id.Proto), c.LocalText,
			std::to_wstring(c.Id.LocalPort) }, pid });
	}
}

void CNetworkInfoDlg::LoadDns() {
	auto& table = m_Tables[(int)Tab::Dns];
	table.Rows.clear();
	table.Error.clear();
	auto records = WinSys::NetworkTables::ReadDnsCache();
	if (!records) {
		table.Error = WinSys::Helpers::GetErrorText(::GetLastError());
		return;
	}
	for (auto& r : *records)
		table.Rows.push_back({ { r.Name, r.Type, r.Data, std::to_wstring(r.Ttl), r.Section } });
}

void CNetworkInfoDlg::LoadNeighbors() {
	auto& table = m_Tables[(int)Tab::Neighbors];
	table.Rows.clear();
	table.Error.clear();
	auto neighbors = WinSys::NetworkTables::ReadNeighbors();
	if (!neighbors) {
		table.Error = WinSys::Helpers::GetErrorText(::GetLastError());
		return;
	}
	for (auto& n : *neighbors)
		table.Rows.push_back({ { n.Address, n.PhysicalAddress, n.State, n.IsRouter ? L"Yes" : L"", n.Interface } });
}

void CNetworkInfoDlg::LoadRoutes() {
	auto& table = m_Tables[(int)Tab::Routes];
	table.Rows.clear();
	table.Error.clear();
	auto routes = WinSys::NetworkTables::ReadRoutes();
	if (!routes) {
		table.Error = WinSys::Helpers::GetErrorText(::GetLastError());
		return;
	}
	for (auto& r : *routes)
		table.Rows.push_back({ { r.Destination, r.NextHop, std::to_wstring(r.Metric), std::to_wstring(r.InterfaceMetric), r.Protocol, r.Interface } });
}

CNetworkInfoDlg::Table& CNetworkInfoDlg::Current() {
	int tab = m_Tabs.GetCurSel();
	return m_Tables[tab < 0 ? 0 : tab];
}

void CNetworkInfoDlg::ShowTab() {
	auto& table = Current();
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	while (m_List.DeleteColumn(0))
		;
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < (int)table.Columns.size(); i++) {
		auto& c = table.Columns[i];
		m_List.InsertColumn(i, c.Name, c.Numeric ? LVCFMT_RIGHT : LVCFMT_LEFT, ::MulDiv(c.Width, dpi, 96));
	}
	// the list view ignores column 0's alignment on insertion
	if (!table.Columns.empty() && table.Columns[0].Numeric) {
		LVCOLUMN col{ LVCF_FMT };
		col.fmt = LVCFMT_RIGHT;
		m_List.SetColumn(0, &col);
	}
	m_List.SetRedraw(TRUE);
	GetDlgItem(IDC_NI_FLUSH).ShowWindow(m_Tabs.GetCurSel() == (int)Tab::Dns ? SW_SHOW : SW_HIDE);
	ShowRows();
}

void CNetworkInfoDlg::ShowRows() {
	auto& table = Current();
	m_Shown.clear();
	for (int i = 0; i < (int)table.Rows.size(); i++) {
		if (!m_Filter.empty()) {
			bool match = false;
			for (auto& cell : table.Rows[i].Cells) {
				auto text = cell;
				::CharLowerBuff(text.data(), (DWORD)text.size());
				if (text.find(m_Filter) != std::wstring::npos) {
					match = true;
					break;
				}
			}
			if (!match)
				continue;
		}
		m_Shown.push_back(i);
	}
	auto column = table.SortColumn;
	bool numeric = column < (int)table.Columns.size() && table.Columns[column].Numeric;
	std::ranges::stable_sort(m_Shown, [&](int a, int b) {
		auto& x = table.Rows[a].Cells[column];
		auto& y = table.Rows[b].Cells[column];
		int result = numeric ? (int)(std::wcstoll(x.c_str(), nullptr, 10) > std::wcstoll(y.c_str(), nullptr, 10))
			- (int)(std::wcstoll(x.c_str(), nullptr, 10) < std::wcstoll(y.c_str(), nullptr, 10)) : ::_wcsicmp(x.c_str(), y.c_str());
		return table.SortAscending ? result < 0 : result > 0;
		});

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (int i = 0; i < (int)m_Shown.size(); i++) {
		auto& cells = table.Rows[m_Shown[i]].Cells;
		m_List.InsertItem(i, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			m_List.SetItemText(i, c, cells[c].c_str());
		m_List.SetItemData(i, m_Shown[i]);
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate();
	UpdateStatus();
}

void CNetworkInfoDlg::UpdateStatus() {
	auto& table = Current();
	std::wstring text;
	if (!table.Error.empty())
		text = L"Can't be read: " + table.Error;
	else {
		static const PCWSTR what[] = { L"listening ports", L"DNS records", L"neighbors", L"routes" };
		text = std::format(L"{} {}", FormatHelper::FormatNumber((int64_t)table.Rows.size()), what[m_Tabs.GetCurSel() < 0 ? 0 : m_Tabs.GetCurSel()]);
		if (!m_Filter.empty())
			text += std::format(L"; {} match the filter", FormatHelper::FormatNumber((int64_t)m_Shown.size()));
		if (m_Tabs.GetCurSel() == (int)Tab::Listening)
			text += L" (double-click: go to the process)";
	}
	SetDlgItemText(IDC_NI_STATUS, text.c_str());
}

LRESULT CNetworkInfoDlg::OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	ShowTab();
	return 0;
}

LRESULT CNetworkInfoDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto& table = Current();
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == table.SortColumn)
		table.SortAscending = !table.SortAscending;
	else {
		table.SortColumn = column;
		table.SortAscending = true;
	}
	ShowRows();
	return 0;
}

LRESULT CNetworkInfoDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnGoToProcess(0, 0, nullptr, handled);
}

LRESULT CNetworkInfoDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the selected row, or the list's corner
		CRect rc;
		if (selected >= 0)
			m_List.GetItemRect(selected, &rc, LVIR_LABEL);
		else
			m_List.GetClientRect(&rc);
		pt = selected >= 0 ? rc.CenterPoint() : rc.TopLeft();
		m_List.ClientToScreen(&pt);
	}
	auto tab = (Tab)m_Tabs.GetCurSel();
	CMenu menu;
	menu.CreatePopupMenu();
	if (tab == Tab::Listening) {
		bool process = selected >= 0 && m_Tables[(int)Tab::Listening].Rows[m_List.GetItemData(selected)].Pid > 4;
		menu.AppendMenu(MF_STRING | (process ? 0 : MF_GRAYED), ID_NETINFO_GOTOPROCESS, L"&Go to Process");
		menu.SetMenuDefaultItem(ID_NETINFO_GOTOPROCESS);
		menu.AppendMenu(MF_SEPARATOR);
	}
	else if (tab == Tab::Dns) {
		menu.AppendMenu(MF_STRING, IDC_NI_FLUSH, L"&Flush DNS Cache...");
		menu.AppendMenu(MF_SEPARATOR);
	}
	menu.AppendMenu(MF_STRING | (m_List.GetItemCount() ? 0 : MF_GRAYED), IDC_NI_COPY,
		m_List.GetSelectedCount() ? L"&Copy" : L"&Copy All");
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CNetworkInfoDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_Tabs.GetCurSel() != (int)Tab::Listening || !m_GoToProcess)
		return 0;
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return 0;
	auto pid = m_Tables[(int)Tab::Listening].Rows[m_List.GetItemData(selected)].Pid;
	if (pid > 4) {
		EndDialog(IDOK);
		m_GoToProcess(pid);
	}
	return 0;
}

LRESULT CNetworkInfoDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_NI_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CNetworkInfoDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CWaitCursor wait;
	LoadListening();
	LoadDns();
	LoadNeighbors();
	LoadRoutes();
	ShowTab();
	return 0;
}

LRESULT CNetworkInfoDlg::OnFlush(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (AtlMessageBox(m_hWnd, L"Flush the DNS cache?\n\nNames are looked up again as they're needed, as after ipconfig /flushdns.",
		IDR_MAINFRAME, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
		return 0;
	if (!WinSys::NetworkTables::FlushDnsCache()) {
		AtlMessageBox(m_hWnd, (L"Flushing the DNS cache failed: " + WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	LoadDns();
	ShowRows();
	return 0;
}

LRESULT CNetworkInfoDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& table = Current();
	std::wstring text;
	for (int c = 0; c < (int)table.Columns.size(); c++)
		text += (c ? L"\t" : L"") + std::wstring(table.Columns[c].Name);
	text += L"\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& cells = table.Rows[m_List.GetItemData(i)].Cells;
		for (int c = 0; c < (int)cells.size(); c++)
			text += (c ? L"\t" : L"") + cells[c];
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CNetworkInfoDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
