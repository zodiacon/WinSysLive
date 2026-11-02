#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <WinSys/EventLog.h>

//
// The Events tab's filter text: "id:" tokens choose event IDs ("id:4625", "id:4624,4625", "id:4720-4767"; several
// tokens add up), the rest is text matched against the other fields ("id:4625 bob"). A token that isn't a valid ID list
// stays text. Header-only, so the tests take it (Tests\AlertTests.cpp).
//
struct EventFilter {
	std::vector<std::pair<uint32_t, uint32_t>> Ids;	// ranges, inclusive; empty: any ID
	std::wstring Text;	// the rest, its words separated by single spaces

	static EventFilter Parse(std::wstring const& filter) {
		EventFilter result;
		size_t pos = 0;
		while (pos < filter.size()) {
			while (pos < filter.size() && iswspace(filter[pos]))
				pos++;
			auto end = pos;
			while (end < filter.size() && !iswspace(filter[end]))
				end++;
			if (end == pos)
				break;
			auto token = filter.substr(pos, end - pos);
			pos = end;
			if (token.size() > 3 && ::_wcsnicmp(token.c_str(), L"id:", 3) == 0 && ParseIds(token.substr(3), result.Ids))
				continue;
			result.Text += (result.Text.empty() ? L"" : L" ") + token;
		}
		return result;
	}

	// the levels the toolbar's Warnings and Information buttons choose: errors and critical events always; level 0 counts
	// as information; the Security log's audit failures come with the warnings (or the information), its successes with
	// the information
	static uint8_t MaxLevel(bool warnings, bool information) {
		return information ? 4 : warnings ? 3 : 2;
	}
	static bool IsIncluded(WinSys::EventLogEntry const& e, bool warnings, bool information) {
		if (e.IsAuditFailure())
			return warnings || information;
		if (e.IsAuditSuccess() || e.Level == 0)
			return information;
		return e.Level <= MaxLevel(warnings, information);
	}

	bool MatchesId(uint32_t id) const {
		if (Ids.empty())
			return true;
		for (auto& [first, last] : Ids)
			if (id >= first && id <= last)
				return true;
		return false;
	}

private:
	// "4624,4625,4720-4767" added to ids; false (nothing added) if any part isn't a number or a range of them
	static bool ParseIds(std::wstring const& list, std::vector<std::pair<uint32_t, uint32_t>>& ids) {
		std::vector<std::pair<uint32_t, uint32_t>> parsed;
		size_t pos = 0;
		while (pos <= list.size()) {
			auto comma = list.find(L',', pos);
			auto part = list.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
			auto dash = part.find(L'-');
			uint32_t first, last;
			if (!Number(part.substr(0, dash), first))
				return false;
			if (dash == std::wstring::npos)
				last = first;
			else if (!Number(part.substr(dash + 1), last) || last < first)
				return false;
			parsed.emplace_back(first, last);
			if (comma == std::wstring::npos)
				break;
			pos = comma + 1;
		}
		for (auto& range : parsed)
			ids.push_back(range);
		return !parsed.empty();
	}

	static bool Number(std::wstring const& text, uint32_t& value) {
		if (text.empty() || text.size() > 9)
			return false;
		value = 0;
		for (auto c : text) {
			if (c < L'0' || c > L'9')
				return false;
			value = value * 10 + (c - L'0');
		}
		return true;
	}
};
