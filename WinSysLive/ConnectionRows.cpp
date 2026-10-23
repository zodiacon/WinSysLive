#include "pch.h"
#include "ConnectionRows.h"
#include "FormatHelper.h"

namespace ConnectionRows {
	void Merge(std::vector<ConnectionRow>& rows, std::vector<WinSys::Connection> current, bool markNew, int newTicks,
		int closedTicks) {
		// by key, several rows for sockets that share one
		std::map<WinSys::ConnectionKey, std::vector<ConnectionRow*>> previous;
		std::vector<ConnectionRow> merged;
		merged.reserve(current.size() + rows.size());
		for (auto& c : rows)
			if (!c.Closed)
				previous[c.Id].push_back(&c);
			else if (c.ClosedTicks > 1) {
				// closed before, still shown
				auto closed = std::move(c);
				closed.ClosedTicks--;
				merged.push_back(std::move(closed));
			}
		for (auto& c : current) {
			ConnectionRow row{ std::move(c) };
			if (auto it = previous.find(row.Id); it != previous.end()) {
				// the one created at the same time, else any: each previous row matches once
				auto& same = it->second;
				auto match = std::ranges::find_if(same, [&](auto r) { return r->Created == row.Created; });
				if (match == same.end())
					match = same.begin();
				row.NewTicks = (std::max)((*match)->NewTicks - 1, 0);
				same.erase(match);
				if (same.empty())
					previous.erase(it);
			}
			else if (markNew)
				row.NewTicks = newTicks;
			merged.push_back(std::move(row));
		}
		if (closedTicks > 0)
			for (auto& [key, same] : previous)
				for (auto c : same) {
					auto closed = std::move(*c);
					closed.Closed = true;
					closed.ClosedTicks = closedTicks;
					closed.NewTicks = 0;
					merged.push_back(std::move(closed));
				}
		rows = std::move(merged);
	}
}

void ConnectionBytes::Update(WinSys::ProcessNetworkTracker const& tracker, std::vector<ConnectionRow> const& rows) {
	auto now = ::GetTickCount64();
	std::map<ConnectionRows::Key, Bytes> bytes;
	std::map<ConnectionRows::Key, std::deque<Count>> history;
	for (auto& c : rows) {
		auto counters = tracker.GetConnectionCounters(WinSys::ProcessNetworkTracker::IdOf(c));
		auto key = ConnectionRows::KeyOf(c);
		Bytes b{ counters.Sent, counters.Received };
		// the rows gone take their counts along
		auto& counts = history[key];
		if (auto it = m_History.find(key); it != m_History.end())
			counts = std::move(it->second);
		counts.push_back({ now, b.Sent, b.Received });
		while (counts.size() > RateUpdates + 1)
			counts.pop_front();
		// from the oldest count kept, which a row new since the last update has none of
		auto& first = counts.front();
		if (double seconds = (now - first.Time) / 1000.0; seconds > 0) {
			b.SendRate = (b.Sent - (std::min)(b.Sent, first.Sent)) / seconds;
			b.ReceiveRate = (b.Received - (std::min)(b.Received, first.Received)) / seconds;
		}
		bytes[key] = b;
	}
	m_Bytes.swap(bytes);
	m_History.swap(history);
}

void ConnectionBytes::Clear() {
	m_Bytes.clear();
	m_History.clear();
}

ConnectionBytes::Bytes const* ConnectionBytes::Get(WinSys::Connection const& c) const {
	auto it = m_Bytes.find(ConnectionRows::KeyOf(c));
	return it == m_Bytes.end() ? nullptr : &it->second;
}

std::wstring ConnectionBytes::Text(WinSys::Connection const& c, Value value) const {
	auto b = Get(c);
	if (!b)
		return L"";
	auto rate = [](double r) {
		auto text = FormatHelper::FormatBytes((uint64_t)r);
		return text.empty() ? text : text + L"/s";
	};
	switch (value) {
		case Value::Sent: return FormatHelper::FormatBytes(b->Sent);
		case Value::Received: return FormatHelper::FormatBytes(b->Received);
		case Value::SendRate: return rate(b->SendRate);
		default: return rate(b->ReceiveRate);
	}
}

int ConnectionBytes::Compare(WinSys::Connection const& c1, WinSys::Connection const& c2, Value value) const {
	static Bytes const none;
	auto b1 = Get(c1), b2 = Get(c2);
	if (!b1)
		b1 = &none;
	if (!b2)
		b2 = &none;
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	switch (value) {
		case Value::Sent: return compare(b1->Sent, b2->Sent);
		case Value::Received: return compare(b1->Received, b2->Received);
		case Value::SendRate: return compare(b1->SendRate, b2->SendRate);
		default: return compare(b1->ReceiveRate, b2->ReceiveRate);
	}
}

std::wstring ConnectionBytes::SinceText(WinSys::ProcessNetworkTracker const& tracker) {
	auto start = tracker.GetStartTime();
	return start ? L"bytes since " + FormatHelper::FormatDateTime(start) : L"";
}
