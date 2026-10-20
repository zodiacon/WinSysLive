#pragma once

#include <WinSys/Connections.h>

// A connection as the Network view and the TCP/IP page show it (WinSys::ConnectionInfo reads them): new ones highlighted
// for a while, closed ones shown once more.
struct ConnectionRow : WinSys::Connection {
	int NewTicks{ 0 };	// > 0: still highlighted as new
	bool Closed{ false };	// shown once more, then removed
};

namespace ConnectionRows {
	// rows: the last merge's (none at first); current: a new read. The connections still there keep their highlighting,
	// counting down; the new ones are highlighted for newTicks reads (unless markNew: on the first read, they're all
	// new to the list, not opened); the ones gone are kept once more as Closed, and those already closed go
	void Merge(std::vector<ConnectionRow>& rows, std::vector<WinSys::Connection> current, bool markNew, int newTicks = 2);
}
