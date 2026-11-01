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
		{ L"EventLogged", L"An event is logged", true, false, L"" },
		{ L"AuditFailure", L"A Security audit failure is logged", true, false, L"" },
	};

	const uint64_t AuditFailureKeyword = 0x10000000000000;

	// a source PathMatchSpec takes as itself: no wildcards, no list
	bool IsExactName(std::wstring const& pattern) {
		return !pattern.empty() && pattern.find_first_of(L"*?;") == std::wstring::npos;
	}

	// an XPath string literal (XPath 1.0 has no escapes: the other quote; none if it has both)
	std::wstring XPathLiteral(std::wstring const& text) {
		if (text.find(L'\'') == std::wstring::npos)
			return L"'" + text + L"'";
		if (text.find(L'"') == std::wstring::npos)
			return L"\"" + text + L"\"";
		return L"";
	}
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

bool AlertRule::IsEventKind(AlertKind kind) {
	return kind == AlertKind::EventLogged || kind == AlertKind::AuditFailure;
}

std::vector<std::wstring> const& AlertRule::EventLogs() {
	static const std::vector<std::wstring> logs{ L"System", L"Application", L"Security" };
	return logs;
}

bool AlertRule::MatchesEvent(std::wstring const& log, std::wstring const& source, uint32_t eventId, bool auditFailure) const {
	if (Kind == AlertKind::AuditFailure) {
		if (!auditFailure || ::_wcsicmp(log.c_str(), L"Security") != 0)
			return false;
	}
	else if (Kind != AlertKind::EventLogged || (!Log.empty() && ::_wcsicmp(Log.c_str(), log.c_str()) != 0))
		return false;
	return (EventId == 0 || EventId == eventId) && Matches(Pattern, source);
}

std::wstring AlertRule::EventQuery(std::vector<AlertRule> const& rules, std::wstring const& log) {
	std::wstring conditions;
	for (auto& r : rules) {
		if (!r.Enabled || !IsEventKind(r.Kind))
			continue;
		auto ruleLog = r.Kind == AlertKind::AuditFailure ? std::wstring(L"Security") : r.Log;
		if (!ruleLog.empty() && ::_wcsicmp(ruleLog.c_str(), log.c_str()) != 0)
			continue;
		std::wstring parts;
		auto add = [&](std::wstring const& part) { parts += (parts.empty() ? L"" : L" and ") + part; };
		if (IsExactName(r.Pattern))
			if (auto literal = XPathLiteral(r.Pattern); !literal.empty())
				add(L"Provider[@Name=" + literal + L"]");
		if (r.EventId)
			add(L"EventID=" + std::to_wstring(r.EventId));
		if (r.Kind == AlertKind::AuditFailure)
			add(L"band(Keywords," + std::to_wstring(AuditFailureKeyword) + L")");
		// nothing to narrow it: every event of the log
		if (parts.empty())
			return L"*";
		conditions += (conditions.empty() ? L"" : L" or ") + (L"(" + parts + L")");
	}
	return conditions.empty() ? L"" : L"*[System[" + conditions + L"]]";
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
		case AlertKind::EventLogged:
			return (EventId ? std::format(L"Event {}", EventId) : std::wstring(L"An event")) + (Pattern.empty() ? L"" : L" from " + Pattern)
				+ L" in " + (Log.empty() ? std::wstring(L"any log") : Log);
		case AlertKind::AuditFailure:
			return L"Security audit failure" + (EventId ? std::format(L" {}", EventId) : std::wstring())
				+ (Pattern.empty() ? L"" : L" from " + Pattern);
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
		if (fields.size() != 5 && fields.size() != 7)
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
		if (fields.size() == 7) {
			rule.Log = fields[5];
			rule.EventId = (uint32_t)::wcstoul(fields[6].c_str(), nullptr, 10);
		}
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
		// the event kinds' fields (the log is a channel name, which has neither separator)
		if (IsEventKind(r.Kind))
			text += std::format(L"|{}|{}", r.Log, r.EventId);
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
