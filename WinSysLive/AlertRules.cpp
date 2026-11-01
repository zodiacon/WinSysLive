#include "pch.h"
#include "AlertRules.h"
#include <Shlwapi.h>
#include <ranges>

#pragma comment(lib, "shlwapi")

namespace {
	struct KindInfo {
		PCWSTR Key;		// in the setting: must not change
		PCWSTR Name;	// in the rule dialog
		bool Pattern, Threshold;
		PCWSTR Unit;
	};

	const KindInfo s_Kinds[] = {
		{ L"ProcessStarted", L"A process starts", true, false, L"" },
		{ L"ProcessExited", L"A process exits", true, false, L"" },
		{ L"SystemCpu", L"CPU usage is above", false, true, L"%" },
		{ L"Commit", L"Commit charge is above", false, true, L"%" },
		{ L"Memory", L"Physical memory in use is above", false, true, L"%" },
		{ L"ProcessCpu", L"A process's CPU usage is above", true, true, L"%" },
		{ L"ProcessPrivate", L"A process's private bytes are above", true, true, L"MB" },
		{ L"ServiceStarted", L"A service starts", true, false, L"" },
		{ L"ServiceStopped", L"A service stops", true, false, L"" },
		{ L"ServiceFailed", L"A service fails (stops with an error)", true, false, L"" },
	};
	static_assert(_countof(s_Kinds) == (int)AlertKind::_Count);

	KindInfo const& Info(AlertKind kind) {
		return s_Kinds[(int)kind];
	}
}

PCWSTR AlertRule::KindName(AlertKind kind) {
	return Info(kind).Name;
}

bool AlertRule::HasPattern(AlertKind kind) {
	return Info(kind).Pattern;
}

bool AlertRule::HasThreshold(AlertKind kind) {
	return Info(kind).Threshold;
}

PCWSTR AlertRule::Unit(AlertKind kind) {
	return Info(kind).Unit;
}

bool AlertRule::IsServiceKind(AlertKind kind) {
	return kind == AlertKind::ServiceStarted || kind == AlertKind::ServiceStopped || kind == AlertKind::ServiceFailed;
}

bool AlertRule::Matches(std::wstring const& pattern, std::wstring const& name) {
	return pattern.empty() || ::PathMatchSpecW(name.c_str(), pattern.c_str());
}

std::wstring AlertRule::FormatNumber(double value) {
	return value == (int64_t)value ? std::to_wstring((int64_t)value) : std::format(L"{:.1f}", value);
}

std::wstring AlertRule::ForSeconds(int seconds) {
	return seconds <= 0 ? L"" : seconds == 1 ? L" for a second" : std::format(L" for {} seconds", seconds);
}

std::wstring AlertRule::Describe() const {
	auto who = !Pattern.empty() ? Pattern : IsServiceKind(Kind) ? std::wstring(L"Any service") : std::wstring(L"Any process");
	auto limit = FormatNumber(Threshold) + (Kind == AlertKind::ProcessPrivateBytes ? L" MB" : L"%");
	switch (Kind) {
		case AlertKind::ProcessStarted: return who + L" starts";
		case AlertKind::ProcessExited: return who + L" exits";
		case AlertKind::SystemCpu: return L"CPU above " + limit + ForSeconds(Seconds);
		case AlertKind::CommitCharge: return L"Commit charge above " + limit + ForSeconds(Seconds);
		case AlertKind::PhysicalMemory: return L"Physical memory above " + limit + ForSeconds(Seconds);
		case AlertKind::ProcessCpu: return who + L": CPU above " + limit + ForSeconds(Seconds);
		case AlertKind::ProcessPrivateBytes: return who + L": private bytes above " + limit + ForSeconds(Seconds);
		case AlertKind::ServiceStarted: return who + L" starts (service)";
		case AlertKind::ServiceStopped: return who + L" stops (service)";
		case AlertKind::ServiceFailed: return who + L" fails (service)";
	}
	return L"";
}

std::vector<AlertRule> AlertRule::ParseRules(std::wstring const& text) {
	std::vector<AlertRule> rules;
	for (auto line : std::views::split(std::wstring_view(text), L'\n')) {
		std::vector<std::wstring> fields;
		for (auto field : std::views::split(std::wstring_view(line.begin(), line.end()), L'|'))
			fields.emplace_back(field.begin(), field.end());
		// a line written with CRLF keeps its CR on the last field
		if (!fields.empty() && !fields.back().empty() && fields.back().back() == L'\r')
			fields.back().pop_back();
		if (fields.size() != 5)
			continue;
		auto it = std::ranges::find_if(s_Kinds, [&](auto& k) { return fields[0] == k.Key; });
		if (it == std::end(s_Kinds))
			continue;
		AlertRule rule;
		rule.Kind = (AlertKind)(it - std::begin(s_Kinds));
		rule.Enabled = fields[1] == L"1";
		rule.Threshold = ::_wtof(fields[2].c_str());
		rule.Seconds = ::_wtoi(fields[3].c_str());
		rule.Pattern = fields[4];
		rules.push_back(std::move(rule));
	}
	return rules;
}

std::wstring AlertRule::FormatRules(std::vector<AlertRule> const& rules) {
	std::wstring text;
	for (auto& r : rules) {
		if (!text.empty())
			text += L'\n';
		auto pattern = r.Pattern;
		std::erase_if(pattern, [](wchar_t c) { return c == L'|' || c == L'\n' || c == L'\r'; });
		text += std::format(L"{}|{}|{}|{}|{}", Info(r.Kind).Key, r.Enabled ? 1 : 0, FormatNumber(r.Threshold), r.Seconds, pattern);
	}
	return text;
}

bool AlertCondition::Update(bool holds, uint64_t now, int seconds) {
	if (!holds) {
		Since = 0;
		Fired = false;
		return false;
	}
	if (Since == 0)
		Since = now;
	if (Fired || now - Since < (uint64_t)(std::max)(seconds, 0) * 1000)
		return false;
	Fired = true;
	return true;
}
