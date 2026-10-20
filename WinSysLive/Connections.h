#pragma once

#include <array>
#include <optional>

//
// The system's TCP connections (listening ones too) and UDP endpoints, IPv4 and IPv6, from the IP helper's owner tables
// (GetExtendedTcpTable/GetExtendedUdpTable: the owning process and the creation time; no elevation needed). Shared by
// the Network view (all of them) and the process's TCP/IP page (one process's), which both show them merged from read
// to read: new ones highlighted for a while, closed ones once more.
//
namespace Connections {
	enum class Protocol : uint8_t { Tcp, Tcp6, Udp, Udp6 };

	// what tells connections apart from one read to the next
	struct Key {
		Protocol Proto;
		std::array<BYTE, 16> LocalAddress, RemoteAddress;	// IPv4 in the first 4 bytes
		uint32_t LocalScope, RemoteScope;
		uint16_t LocalPort, RemotePort;	// host order
		uint32_t Pid;

		auto operator<=>(Key const&) const = default;
	};

	struct Connection {
		Key Id;
		DWORD State;		// MIB_TCP_STATE; 0 for UDP
		int64_t Created;	// FILETIME, 0 if unknown
		std::wstring LocalText, RemoteText;	// the addresses, formatted once; no remote end: empty
		int NewTicks{ 0 };	// > 0: still highlighted as new
		bool Closed{ false };	// shown once more, then removed

		bool IsTcp() const;
	};

	// the connections (pid: only that process's); nothing (with the last error set) if none of the tables can be read
	std::optional<std::vector<Connection>> Read(std::optional<uint32_t> pid = std::nullopt);

	// rows: the last merge's (none at first); current: a new read. The connections still there keep their highlighting,
	// counting down; the new ones are highlighted for newTicks reads (unless markNew: on the first read, they're all
	// new to the list, not opened); the ones gone are kept once more as Closed, and those already closed go
	void Merge(std::vector<Connection>& rows, std::vector<Connection> current, bool markNew, int newTicks = 2);

	PCWSTR ProtocolName(Protocol protocol);
	// "Established", "Listening"...; empty for UDP
	PCWSTR StateName(Connection const& c);
}
