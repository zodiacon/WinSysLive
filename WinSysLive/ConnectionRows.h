#pragma once

#include <WinSys/Connections.h>

// A connection as the Network view and the TCP/IP page show it (WinSys::ConnectionInfo reads them): new ones highlighted
// for a while, closed ones shown once more.
struct ConnectionRow : WinSys::Connection {
	int NewTicks{ 0 };	// > 0: still highlighted as new
	bool Closed{ false };	// shown once more, then removed
};

namespace ConnectionRows {
	// a row's identity, to keep the selection: sockets may share the connection key (several UDP sockets on one address
	// and port, with SO_REUSEADDR); their creation times mostly tell them apart
	struct Key {
		WinSys::ConnectionKey Id;
		int64_t Created;

		auto operator<=>(Key const&) const = default;
	};
	inline Key KeyOf(WinSys::Connection const& c) {
		return { c.Id, c.Created };
	}

	// rows: the last merge's (none at first); current: a new read. The connections still there keep their highlighting,
	// counting down; the new ones are highlighted for newTicks reads (unless markNew: on the first read, they're all
	// new to the list, not opened); the ones gone are kept once more as Closed, and those already closed go. Rows
	// sharing a connection key are paired up one to one, by creation time first
	void Merge(std::vector<ConnectionRow>& rows, std::vector<WinSys::Connection> current, bool markNew, int newTicks = 2);
}
