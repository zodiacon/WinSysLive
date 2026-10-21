#include "pch.h"
#include "ConnectionRows.h"

namespace ConnectionRows {
	void Merge(std::vector<ConnectionRow>& rows, std::vector<WinSys::Connection> current, bool markNew, int newTicks) {
		// by key, several rows for sockets that share one
		std::map<WinSys::ConnectionKey, std::vector<ConnectionRow*>> previous;
		for (auto& c : rows)
			if (!c.Closed)
				previous[c.Id].push_back(&c);
		std::vector<ConnectionRow> merged;
		merged.reserve(current.size() + rows.size());
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
		for (auto& [key, same] : previous)
			for (auto c : same) {
				auto closed = std::move(*c);
				closed.Closed = true;
				closed.NewTicks = 0;
				merged.push_back(std::move(closed));
			}
		rows = std::move(merged);
	}
}
