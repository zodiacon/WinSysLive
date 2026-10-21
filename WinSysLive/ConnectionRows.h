#pragma once

#include <WinSys/Connections.h>
#include <WinSys/ProcessNetwork.h>
#include <deque>

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

// The bytes over connection rows, from the shared ETW tracker (NetworkTracking): each Update takes every row's counts,
// by row key, and the rates over the last RateUpdates updates: the events come in batches about once a second, as
// the updates do, so a rate over one update was twice or none of the traffic at times. The Network view's and the
// TCP/IP page's byte columns
class ConnectionBytes {
public:
	struct Bytes {
		uint64_t Sent{ 0 }, Received{ 0 };
		double SendRate{ 0 }, ReceiveRate{ 0 };	// bytes a second over the last updates
	};
	enum class Value { Sent, Received, SendRate, ReceiveRate };

	void Update(WinSys::ProcessNetworkTracker const& tracker, std::vector<ConnectionRow> const& rows);
	void Clear();
	Bytes const* Get(WinSys::Connection const& c) const;
	// empty for none and for zero
	std::wstring Text(WinSys::Connection const& c, Value value) const;
	// -1, 0 or 1, for sorting; none counts as zero
	int Compare(WinSys::Connection const& c1, WinSys::Connection const& c2, Value value) const;
	// "bytes since <when the tracker started>": nothing before is counted
	static std::wstring SinceText(WinSys::ProcessNetworkTracker const& tracker);

private:
	static constexpr size_t RateUpdates = 3;
	struct Count {
		ULONGLONG Time;
		uint64_t Sent, Received;
	};
	std::map<ConnectionRows::Key, Bytes> m_Bytes;
	std::map<ConnectionRows::Key, std::deque<Count>> m_History;	// the last RateUpdates + 1 counts, oldest first
};
