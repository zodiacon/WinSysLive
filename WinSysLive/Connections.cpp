#include "pch.h"
#include "Connections.h"
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <ip2string.h>
#include <map>

#pragma comment(lib, "iphlpapi")
#pragma comment(lib, "ntdll")

namespace {
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

namespace Connections {
	bool Connection::IsTcp() const {
		return Id.Proto == Protocol::Tcp || Id.Proto == Protocol::Tcp6;
	}

	PCWSTR ProtocolName(Protocol protocol) {
		switch (protocol) {
			case Protocol::Tcp: return L"TCP";
			case Protocol::Tcp6: return L"TCPv6";
			case Protocol::Udp: return L"UDP";
			case Protocol::Udp6: return L"UDPv6";
		}
		return L"";
	}

	PCWSTR StateName(Connection const& c) {
		if (!c.IsTcp())
			return L"";
		switch (c.State) {
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

	std::optional<std::vector<Connection>> Read(std::optional<uint32_t> pid) {
		std::vector<Connection> result;
		auto wanted = [&](DWORD owner) { return !pid || owner == *pid; };
		auto add = [&](Key const& key, DWORD state, LARGE_INTEGER created, std::wstring local, std::wstring remote) {
			Connection c;
			c.Id = key;
			c.State = state;
			c.Created = created.QuadPart;
			c.LocalText = std::move(local);
			c.RemoteText = std::move(remote);
			result.push_back(std::move(c));
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
		if (!tcp4 && !tcp6 && !udp4 && !udp6)
			return std::nullopt;

		if (tcp4) {
			auto table = reinterpret_cast<MIB_TCPTABLE_OWNER_MODULE const*>(tcp4.get());
			for (DWORD i = 0; i < table->dwNumEntries; i++) {
				auto& row = table->table[i];
				if (!wanted(row.dwOwningPid))
					continue;
				Key key{ Protocol::Tcp };
				key.Pid = row.dwOwningPid;
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
				if (!wanted(row.dwOwningPid))
					continue;
				Key key{ Protocol::Tcp6 };
				key.Pid = row.dwOwningPid;
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
				if (!wanted(row.dwOwningPid))
					continue;
				Key key{ Protocol::Udp };
				key.Pid = row.dwOwningPid;
				memcpy(key.LocalAddress.data(), &row.dwLocalAddr, 4);
				key.LocalPort = PortFromTable(row.dwLocalPort);
				add(key, 0, row.liCreateTimestamp, FormatIPv4(key.LocalAddress.data()), L"");
			}
		}
		if (udp6) {
			auto table = reinterpret_cast<MIB_UDP6TABLE_OWNER_MODULE const*>(udp6.get());
			for (DWORD i = 0; i < table->dwNumEntries; i++) {
				auto& row = table->table[i];
				if (!wanted(row.dwOwningPid))
					continue;
				Key key{ Protocol::Udp6 };
				key.Pid = row.dwOwningPid;
				memcpy(key.LocalAddress.data(), row.ucLocalAddr, 16);
				key.LocalScope = row.dwLocalScopeId;
				key.LocalPort = PortFromTable(row.dwLocalPort);
				add(key, 0, row.liCreateTimestamp, FormatIPv6(row.ucLocalAddr, row.dwLocalScopeId), L"");
			}
		}
		return result;
	}

	void Merge(std::vector<Connection>& rows, std::vector<Connection> current, bool markNew, int newTicks) {
		std::map<Key, Connection*> previous;
		for (auto& c : rows)
			if (!c.Closed)
				previous[c.Id] = &c;
		for (auto& c : current) {
			if (auto it = previous.find(c.Id); it != previous.end()) {
				c.NewTicks = (std::max)(it->second->NewTicks - 1, 0);
				previous.erase(it);
			}
			else if (markNew)
				c.NewTicks = newTicks;
		}
		for (auto& [key, c] : previous) {
			auto closed = std::move(*c);
			closed.Closed = true;
			closed.NewTicks = 0;
			current.push_back(std::move(closed));
		}
		rows = std::move(current);
	}
}
