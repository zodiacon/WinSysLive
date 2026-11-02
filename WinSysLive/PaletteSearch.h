#pragma once

#include <string>
#include <unordered_map>
#include <vector>

//
// The command palette's matching (CCommandPaletteDlg), without the dialog: the words typed, each to be found in an
// item's text; the search's results in their order and within their caps. Header-only, so the tests take it
// (Tests\AlertTests.cpp).
//
namespace PaletteSearch {
	inline std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	// the words of a filter (already lowercase), split at spaces
	inline std::vector<std::wstring> Words(std::wstring const& filter) {
		std::vector<std::wstring> words;
		for (size_t start = 0; start < filter.size(); ) {
			auto end = filter.find(L' ', start);
			if (end == std::wstring::npos)
				end = filter.size();
			if (end > start)
				words.push_back(filter.substr(start, end - start));
			start = end + 1;
		}
		return words;
	}

	// every word in the text (lowercase)
	inline bool MatchesAll(std::vector<std::wstring> const& words, std::wstring const& text) {
		for (auto& w : words)
			if (text.find(w) == std::wstring::npos)
				return false;
		return true;
	}

	struct Item {
		std::wstring const* Name;
		std::wstring const* Kind;
		std::wstring const* Detail;
	};

	// the search's results for a filter (lowercase), as indexes into the items: none under two characters; every word
	// in the name, kind or detail; the names starting with the filter first, each group in the items' order; at most
	// maxPerKind of a kind (thirty svchost.exe processes mustn't hide the services) and maxResults in all
	inline std::vector<int> Find(std::vector<Item> const& items, std::wstring const& filter, size_t maxResults, size_t maxPerKind) {
		std::vector<int> found;
		if (filter.size() < 2)
			return found;
		auto words = Words(filter);
		std::vector<int> rest;
		for (int i = 0; i < (int)items.size() && found.size() + rest.size() < maxResults * 4; i++) {
			auto name = Lowercase(*items[i].Name);
			if (MatchesAll(words, name + L" " + Lowercase(*items[i].Kind) + L" " + Lowercase(*items[i].Detail)))
				(name.starts_with(filter) ? found : rest).push_back(i);
		}
		for (auto i : rest)
			found.push_back(i);
		std::unordered_map<std::wstring, size_t> perKind;
		std::erase_if(found, [&](int i) { return ++perKind[*items[i].Kind] > maxPerKind; });
		if (found.size() > maxResults)
			found.resize(maxResults);
		return found;
	}
}
