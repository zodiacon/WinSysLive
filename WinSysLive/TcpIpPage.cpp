#include "pch.h"
#include "TcpIpPage.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ClipboardHelper.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <ip2string.h>
#include <map>

#pragma comment(lib, "iphlpapi")
#pragma comment(lib, "ntdll")

namespace {
	// highlighting of new connections lasts this many samples
	const int NewConnectionTicks = 2;

	PCWSTR TcpStateToString(DWORD state) {
		switch (state) {
			case MIB_TCP_STATE_CLOSED: return L"Closed";
			case MIB_TCP_STATE_LISTEN: return L"Listening";
			case MIB_TCP_STATE_SYN_SENT: return L"SYN Sent";
			case MIB_TCP_STATE_SYN_RCVD: return L"SYN Received";
			case MIB_TCP_STATE_ESTAB: return L"Established";
			case MIB_TCP_STATE_FIN_WAIT1: return L"FIN Wait 1";
			case MIB_TCP_STATE_FIN_WAIT2: return L"FIN Wait 2";
			case MIB_TCP_STATE_CLOSE_WAIT: return L"Close Wait";
			case MIB_TCP_STATE_CLOSING: return L"Closing";
			case MIB_TCP_STATE_LAST_ACK: return L"Last ACK";
			case MIB_TCP_STATE_TIME_WAIT: return L"Time Wait";
			case MIB_TCP_STATE_DELETE_TCB: return L"Delete TCB";
		}
		return L"";
	}

	// the table, or nullptr (with the last error set); query is GetExtendedTcpTable or GetExtendedUdpTable with its
	// other arguments bound
	template<typename Query>
	std::unique_ptr<BYTE[]> ReadTable(Query const& query) {
		DWORD size = 1 << 16;
		for (int attempt = 0; attempt < 4; attempt++) {
			auto buffer = std::make_unique<BYTE[]>(size);
			auto error = query(buffer.get(), &size);
			if (error == NO_ERROR)
				return buffer;
			if (error != ERROR_INSUFFICIENT_BUFFER) {
				::SetLastError(error);
				return nullptr;
			}
			// connections come and go between the calls
			size += 1 << 12;
		}
		::SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return nullptr;
	}

	uint16_t PortFromTable(DWORD port) {
		// in network order, in the low 16 bits
		return _byteswap_ushort((uint16_t)port);
	}

	std::wstring FormatIPv4(BYTE const* address) {
		WCHAR text[16];
		::RtlIpv4AddressToStringW(reinterpret_cast<in_addr const*>(address), text);
		return text;
	}

	std::wstring FormatIPv6(BYTE const* address, uint32_t scope) {
		WCHAR text[64];
		ULONG len = _countof(text);
		if (::RtlIpv6AddressToStringExW(reinterpret_cast<in6_addr const*>(address), scope, 0, text, &len) != 0)
			return L"";
		return text;
	}
}

CProcTcpIpPage::CProcTcpIpPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Props(props), m_Snapshot(snapshot) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_NETWORK);
}

LRESULT CProcTcpIpPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_TCPIP_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
	auto cm = GetColumnManager(m_List);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	cm->AddColumn(L"Protocol", LVCFMT_LEFT, width(65), Column::Protocol);
	cm->AddColumn(L"Local Address", LVCFMT_LEFT, width(150), Column::LocalAddress);
	cm->AddColumn(L"Local Port", LVCFMT_RIGHT, width(70), Column::LocalPort);
	cm->AddColumn(L"Remote Address", LVCFMT_LEFT, width(150), Column::RemoteAddress);
	cm->AddColumn(L"Remote Port", LVCFMT_RIGHT, width(80), Column::RemotePort);
	cm->AddColumn(L"State", LVCFMT_LEFT, width(90), Column::State);
	cm->AddColumn(L"Created", LVCFMT_LEFT, width(150), Column::Created);
	UpdateButtons();

	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcTcpIpPage::OnSetActive() {
	// the sheet's last snapshot; the sheet feeds the page only while it's active
	OnSample(m_Snapshot);
	return 0;
}

void CProcTcpIpPage::OnSample(ProcessSnapshot const& snapshot) {
	if (m_Exited)
		return;
	if (snapshot.Exited()) {
		// the last connections stay, as they were
		m_Exited = true;
		std::erase_if(m_Rows, [](auto& c) { return c.Closed; });
		for (auto& c : m_Rows)
			c.NewTicks = 0;
		m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
		SetDlgItemText(IDC_TCPIP_STATUS, L"The process has exited");
		return;
	}

	auto selected = GetSelectedKeys();
	if (!Load())
		return;

	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectConnections(selected);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());

	auto tcp = std::ranges::count_if(m_Rows, [](auto& c) { return !c.Closed && (c.Id.Proto == Protocol::Tcp || c.Id.Proto == Protocol::Tcp6); });
	auto udp = std::ranges::count_if(m_Rows, [](auto& c) { return !c.Closed && (c.Id.Proto == Protocol::Udp || c.Id.Proto == Protocol::Udp6); });
	SetDlgItemText(IDC_TCPIP_STATUS, std::format(L"{} TCP connection{}, {} UDP endpoint{}",
		tcp, tcp == 1 ? L"" : L"s", udp, udp == 1 ? L"" : L"s").c_str());
	GetDlgItem(IDC_TCPIP_COPYALL).EnableWindow(!m_Rows.empty());
	UpdateButtons();
}

bool CProcTcpIpPage::Load() {
	auto pid = m_Props.Id;
	std::vector<Connection> current;
	auto add = [&](Key const& key, DWORD state, LARGE_INTEGER created, std::wstring local, std::wstring remote) {
		Connection c;
		c.Id = key;
		c.State = state;
		c.Created = created.QuadPart;
		c.LocalText = std::move(local);
		c.RemoteText = std::move(remote);
		current.push_back(std::move(c));
	};

	auto tcp4 = ReadTable([](void* buffer, DWORD* size) {
		return ::GetExtendedTcpTable(buffer, size, FALSE, AF_INET, TCP_TABLE_OWNER_MODULE_ALL, 0);
		});
	auto tcp6 = ReadTable([](void* buffer, DWORD* size) {
		return ::GetExtendedTcpTable(buffer, size, FALSE, AF_INET6, TCP_TABLE_OWNER_MODULE_ALL, 0);
		});
	auto udp4 = ReadTable([](void* buffer, DWORD* size) {
		return ::GetExtendedUdpTable(buffer, size, FALSE, AF_INET, UDP_TABLE_OWNER_MODULE, 0);
		});
	auto udp6 = ReadTable([](void* buffer, DWORD* size) {
		return ::GetExtendedUdpTable(buffer, size, FALSE, AF_INET6, UDP_TABLE_OWNER_MODULE, 0);
		});
	if (!tcp4 && !tcp6 && !udp4 && !udp6) {
		SetDlgItemText(IDC_TCPIP_STATUS, std::format(L"Can't read the connections: {}",
			WinSys::Helpers::GetErrorText(::GetLastError())).c_str());
		return false;
	}

	if (tcp4) {
		auto table = reinterpret_cast<MIB_TCPTABLE_OWNER_MODULE const*>(tcp4.get());
		for (DWORD i = 0; i < table->dwNumEntries; i++) {
			auto& row = table->table[i];
			if (row.dwOwningPid != pid)
				continue;
			Key key{ Protocol::Tcp };
			memcpy(key.LocalAddress.data(), &row.dwLocalAddr, 4);
			memcpy(key.RemoteAddress.data(), &row.dwRemoteAddr, 4);
			key.LocalPort = PortFromTable(row.dwLocalPort);
			// a listening socket has no remote end
			bool listen = row.dwState == MIB_TCP_STATE_LISTEN;
			key.RemotePort = listen ? 0 : PortFromTable(row.dwRemotePort);
			add(key, row.dwState, row.liCreateTimestamp, FormatIPv4(key.LocalAddress.data()),
				listen ? L"" : FormatIPv4(key.RemoteAddress.data()));
		}
	}
	if (tcp6) {
		auto table = reinterpret_cast<MIB_TCP6TABLE_OWNER_MODULE const*>(tcp6.get());
		for (DWORD i = 0; i < table->dwNumEntries; i++) {
			auto& row = table->table[i];
			if (row.dwOwningPid != pid)
				continue;
			Key key{ Protocol::Tcp6 };
			memcpy(key.LocalAddress.data(), row.ucLocalAddr, 16);
			memcpy(key.RemoteAddress.data(), row.ucRemoteAddr, 16);
			key.LocalScope = row.dwLocalScopeId;
			key.RemoteScope = row.dwRemoteScopeId;
			key.LocalPort = PortFromTable(row.dwLocalPort);
			bool listen = row.dwState == MIB_TCP_STATE_LISTEN;
			key.RemotePort = listen ? 0 : PortFromTable(row.dwRemotePort);
			add(key, row.dwState, row.liCreateTimestamp, FormatIPv6(row.ucLocalAddr, row.dwLocalScopeId),
				listen ? L"" : FormatIPv6(row.ucRemoteAddr, row.dwRemoteScopeId));
		}
	}
	if (udp4) {
		auto table = reinterpret_cast<MIB_UDPTABLE_OWNER_MODULE const*>(udp4.get());
		for (DWORD i = 0; i < table->dwNumEntries; i++) {
			auto& row = table->table[i];
			if (row.dwOwningPid != pid)
				continue;
			Key key{ Protocol::Udp };
			memcpy(key.LocalAddress.data(), &row.dwLocalAddr, 4);
			key.LocalPort = PortFromTable(row.dwLocalPort);
			add(key, 0, row.liCreateTimestamp, FormatIPv4(key.LocalAddress.data()), L"");
		}
	}
	if (udp6) {
		auto table = reinterpret_cast<MIB_UDP6TABLE_OWNER_MODULE const*>(udp6.get());
		for (DWORD i = 0; i < table->dwNumEntries; i++) {
			auto& row = table->table[i];
			if (row.dwOwningPid != pid)
				continue;
			Key key{ Protocol::Udp6 };
			memcpy(key.LocalAddress.data(), row.ucLocalAddr, 16);
			key.LocalScope = row.dwLocalScopeId;
			key.LocalPort = PortFromTable(row.dwLocalPort);
			add(key, 0, row.liCreateTimestamp, FormatIPv6(row.ucLocalAddr, row.dwLocalScopeId), L"");
		}
	}

	// merge with the previous sample: the ones still there keep their highlighting, the ones gone are shown once more
	std::map<Key, Connection*> previous;
	for (auto& c : m_Rows)
		if (!c.Closed)
			previous[c.Id] = &c;
	for (auto& c : current) {
		if (auto it = previous.find(c.Id); it != previous.end()) {
			c.NewTicks = std::max(it->second->NewTicks - 1, 0);
			previous.erase(it);
		}
		else if (m_Loaded)
			// on the first read, all of them are new to the page, not opened
			c.NewTicks = NewConnectionTicks;
	}
	for (auto& [key, c] : previous) {
		auto closed = std::move(*c);
		closed.Closed = true;
		closed.NewTicks = 0;
		current.push_back(std::move(closed));
	}
	m_Rows = std::move(current);
	m_Loaded = true;
	return true;
}

std::wstring CProcTcpIpPage::GetText(Connection const& c, Column column) const {
	bool tcp = c.Id.Proto == Protocol::Tcp || c.Id.Proto == Protocol::Tcp6;
	switch (column) {
		case Column::Protocol:
			switch (c.Id.Proto) {
				case Protocol::Tcp: return L"TCP";
				case Protocol::Tcp6: return L"TCPv6";
				case Protocol::Udp: return L"UDP";
				case Protocol::Udp6: return L"UDPv6";
			}
			break;
		case Column::LocalAddress: return c.LocalText;
		case Column::LocalPort: return std::to_wstring(c.Id.LocalPort);
		case Column::RemoteAddress: return c.RemoteText;
		case Column::RemotePort: return c.RemoteText.empty() ? L"" : std::to_wstring(c.Id.RemotePort);
		case Column::State: return tcp ? TcpStateToString(c.State) : L"";
		case Column::Created: return c.Created ? FormatHelper::FormatDateTime(c.Created) : L"";
	}
	return L"";
}

CString CProcTcpIpPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

void CProcTcpIpPage::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKeys();

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Connection const& c1, Connection const& c2) {
		int result = 0;
		switch (column) {
			case Column::Protocol: result = compare(c1.Id.Proto, c2.Id.Proto); break;
			// by the address's bytes, so 10.0.0.9 comes before 10.0.0.10
			case Column::LocalAddress: result = compare(c1.Id.LocalAddress, c2.Id.LocalAddress); break;
			case Column::LocalPort: result = compare(c1.Id.LocalPort, c2.Id.LocalPort); break;
			case Column::RemoteAddress: result = compare(c1.Id.RemoteAddress, c2.Id.RemoteAddress); break;
			case Column::RemotePort: result = compare(c1.Id.RemotePort, c2.Id.RemotePort); break;
			case Column::State: result = ::_wcsicmp(GetText(c1, column).c_str(), GetText(c2, column).c_str()); break;
			case Column::Created: result = compare(c1.Created, c2.Created); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectConnections(selected);
}

std::vector<CProcTcpIpPage::Key> CProcTcpIpPage::GetSelectedKeys() const {
	// the focused row first, so it keeps the focus
	std::vector<Key> keys;
	int focused = m_List.GetNextItem(-1, LVNI_FOCUSED | LVNI_SELECTED);
	if (focused >= 0 && focused < (int)m_Rows.size())
		keys.push_back(m_Rows[focused].Id);
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size(); i = m_List.GetNextItem(i, LVNI_SELECTED))
		if (i != focused)
			keys.push_back(m_Rows[i].Id);
	return keys;
}

void CProcTcpIpPage::SelectConnections(std::vector<Key> const& keys) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	for (size_t k = 0; k < keys.size(); k++) {
		auto it = std::ranges::find_if(m_Rows, [&](auto& c) { return c.Id == keys[k]; });
		if (it != m_Rows.end())
			m_List.SetItemState((int)(it - m_Rows.begin()), k == 0 ? LVIS_SELECTED | LVIS_FOCUSED : LVIS_SELECTED,
				LVIS_SELECTED | LVIS_FOCUSED);
	}
}

void CProcTcpIpPage::OnStateChanged(HWND, int, int, UINT, UINT) {
	// a range of rows selected with Shift
	UpdateButtons();
}

LRESULT CProcTcpIpPage::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	UpdateButtons();
	bHandled = FALSE;
	return 0;
}

void CProcTcpIpPage::UpdateButtons() {
	GetDlgItem(IDC_TCPIP_COPY).EnableWindow(m_List.GetSelectedCount() > 0);
}

LRESULT CProcTcpIpPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
		{
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto& row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and exited threads on the Threads page
			if (row.Closed) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CProcTcpIpPage::OnCopy(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a line per connection, its columns separated by tabs
	std::wstring text;
	for (int i = id == IDC_TCPIP_COPYALL ? 0 : m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size();
		i = id == IDC_TCPIP_COPYALL ? i + 1 : m_List.GetNextItem(i, LVNI_SELECTED)) {
		for (int c = 0; c < (int)Column::_Count; c++) {
			if (c > 0)
				text += L'\t';
			text += GetText(m_Rows[i], (Column)c);
		}
		text += L"\r\n";
	}
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
