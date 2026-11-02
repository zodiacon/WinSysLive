#include "pch.h"
#include "ColumnLayout.h"
#include <ranges>

// ColumnLayout's setting text, apart from the dialogs and list views of ColumnLayout.cpp, so the tests take it
// (Tests\AlertTests.cpp)

namespace ColumnLayout {
	namespace {
		bool Contains(std::vector<ShownColumn> const& columns, int id) {
			return std::ranges::contains(columns, id, &ShownColumn::Id);
		}

		// the entry that records the table's version: an unknown key to older builds, which skip it
		constexpr std::wstring_view VersionKey = L"~";
	}

	int Version(std::span<ColumnDef const> defs) {
		int version = 0;
		for (auto& d : defs)
			version = (std::max)(version, d.Added);
		return version;
	}

	std::vector<ShownColumn> Parse(std::wstring_view text, std::span<ColumnDef const> defs) {
		std::vector<ShownColumn> columns;
		int version = 0;	// the table's when the text was written; none: before versions
		for (auto part : std::views::split(text, L',')) {
			std::wstring_view entry(part.begin(), part.end());
			auto colon = entry.find(L':');
			if (colon == entry.npos)
				continue;
			auto key = entry.substr(0, colon);
			auto value = _wtoi(std::wstring(entry.substr(colon + 1)).c_str());
			if (key == VersionKey) {
				version = (std::max)(value, 0);
				continue;
			}
			auto def = std::ranges::find_if(defs, [&](auto& d) { return key == d.Key; });
			if (def == defs.end())
				continue;
			int id = (int)(def - defs.begin());
			if (value > 0 && !Contains(columns, id))
				columns.push_back({ id, value });
		}

		if (columns.empty()) {
			for (int i = 0; i < (int)defs.size(); i++)
				if (defs[i].Default)
					columns.push_back({ i, defs[i].Width });
			return columns;
		}
		// default columns added to the table since the text was written: after the shown column before them in the
		// table, or first (once: the text is written with the table's version, so hiding one then sticks)
		for (int i = 0; i < (int)defs.size(); i++) {
			if (!defs[i].Default || defs[i].Added <= version || Contains(columns, i))
				continue;
			auto pos = columns.begin();
			for (int before = i - 1; before >= 0; before--)
				if (auto it = std::ranges::find(columns, before, &ShownColumn::Id); it != columns.end()) {
					pos = it + 1;
					break;
				}
			columns.insert(pos, { i, defs[i].Width });
		}
		// a missing mandatory column goes after the mandatory column before it in the table, or first
		int previous = -1;
		for (int i = 0; i < (int)defs.size(); i++) {
			if (!defs[i].Mandatory)
				continue;
			if (!Contains(columns, i)) {
				auto pos = previous < 0 ? columns.begin() : std::ranges::find(columns, previous, &ShownColumn::Id) + 1;
				columns.insert(pos, { i, defs[i].Width });
			}
			previous = i;
		}
		return columns;
	}

	std::wstring Format(std::vector<ShownColumn> const& columns, std::span<ColumnDef const> defs) {
		std::wstring text;
		for (auto& c : columns) {
			if (!text.empty())
				text += L',';
			text += std::format(L"{}:{}", defs[c.Id].Key, c.Width);
		}
		// the table's version, when it has columns added since the first
		if (auto version = Version(defs); version > 0)
			text += std::format(L"{}{}:{}", text.empty() ? L"" : L",", VersionKey, version);
		return text;
	}
}
