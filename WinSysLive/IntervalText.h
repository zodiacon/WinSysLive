#pragma once

#include <optional>
#include <string>
#include <string_view>

//
// The update interval as people type and read it: seconds, with up to three decimals (a dot or a comma), 0.1 to 60.
// View > Update Interval > Other... parses it, and the menu, the status bar's tooltip and the dialog show it.
//
namespace IntervalText {
	constexpr int MinMilliseconds = 100;
	constexpr int MaxMilliseconds = 60000;

	// "2", "0.25", "1,5", " 3 " -> milliseconds; nothing for anything else, a fourth decimal or out of range
	inline std::optional<int> ParseSeconds(std::wstring_view text) {
		while (!text.empty() && iswspace(text.front()))
			text.remove_prefix(1);
		while (!text.empty() && iswspace(text.back()))
			text.remove_suffix(1);
		if (text.empty())
			return {};
		long long whole = 0;
		int fraction = 0, decimals = 0;
		bool point = false, digits = false;
		for (auto ch : text) {
			if (ch == L'.' || ch == L',') {
				if (point)
					return {};
				point = true;
			}
			else if (ch >= L'0' && ch <= L'9') {
				digits = true;
				if (point) {
					if (++decimals > 3)
						return {};
					fraction = fraction * 10 + (ch - L'0');
				}
				else {
					whole = whole * 10 + (ch - L'0');
					if (whole > MaxMilliseconds)
						return {};
				}
			}
			else
				return {};
		}
		if (!digits)
			return {};
		for (int i = decimals; i < 3; i++)
			fraction *= 10;
		auto ms = whole * 1000 + fraction;
		if (ms < MinMilliseconds || ms > MaxMilliseconds)
			return {};
		return (int)ms;
	}

	// 2000 -> "2", 250 -> "0.25", 1500 -> "1.5": as few decimals as it takes
	inline std::wstring Seconds(int milliseconds) {
		auto text = std::to_wstring(milliseconds / 1000);
		if (auto rest = milliseconds % 1000) {
			auto decimals = std::to_wstring(1000 + rest).substr(1);
			while (decimals.back() == L'0')
				decimals.pop_back();
			text += L"." + decimals;
		}
		return text;
	}

	// "2 seconds", "1 second", "0.25 seconds"
	inline std::wstring Describe(int milliseconds) {
		return Seconds(milliseconds) + (milliseconds == 1000 ? L" second" : L" seconds");
	}
}
