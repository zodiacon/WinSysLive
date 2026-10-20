#include "pch.h"
#include "ConnectionRows.h"

namespace ConnectionRows {
	void Merge(std::vector<ConnectionRow>& rows, std::vector<WinSys::Connection> current, bool markNew, int newTicks) {
		std::map<WinSys::ConnectionKey, ConnectionRow*> previous;
		for (auto& c : rows)
			if (!c.Closed)
				previous[c.Id] = &c;
		std::vector<ConnectionRow> merged;
		merged.reserve(current.size() + previous.size());
		for (auto& c : current) {
			ConnectionRow row{ std::move(c) };
			if (auto it = previous.find(row.Id); it != previous.end()) {
				row.NewTicks = (std::max)(it->second->NewTicks - 1, 0);
				previous.erase(it);
			}
			else if (markNew)
				row.NewTicks = newTicks;
			merged.push_back(std::move(row));
		}
		for (auto& [key, c] : previous) {
			auto closed = std::move(*c);
			closed.Closed = true;
			closed.NewTicks = 0;
			merged.push_back(std::move(closed));
		}
		rows = std::move(merged);
	}
}
