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

namespace {
	// a field's value; empty for none and for the log's placeholders ("-", the null SID, an unresolved "%%1793")
	std::wstring Field(std::vector<WinSys::EventDataItem> const& data, PCWSTR name) {
		for (auto& item : data)
			if (::_wcsicmp(item.Name.c_str(), name) == 0)
				return item.Value == L"-" || item.Value == L"S-1-0-0" || item.Value.starts_with(L"%%") ? std::wstring() : item.Value;
		return L"";
	}

	// DOMAIN\user from the fields with the prefix (Target, Subject); the user alone without a domain
	std::wstring Account(std::vector<WinSys::EventDataItem> const& data, std::wstring const& prefix) {
		auto user = Field(data, (prefix + L"UserName").c_str());
		auto domain = Field(data, (prefix + L"DomainName").c_str());
		if (user.empty())
			return L"";
		return domain.empty() ? user : domain + L"\\" + user;
	}

	// " from <address>" (the IP address, else the workstation), empty for neither or a local one
	std::wstring From(std::vector<WinSys::EventDataItem> const& data) {
		auto address = Field(data, L"IpAddress");
		if (address == L"::1" || address == L"127.0.0.1")
			address.clear();
		if (address.empty())
			address = Field(data, L"WorkstationName");
		if (address.empty())
			address = Field(data, L"Workstation");
		return address.empty() ? L"" : L" from " + address;
	}

	// the failure: the sub-status in words if it has one (the status is often the generic "bad user name or password"),
	// else the status's, else the code
	std::wstring Failure(std::vector<WinSys::EventDataItem> const& data) {
		for (auto name : { L"SubStatus", L"Status" }) {
			auto code = Field(data, name);
			if (auto text = EventAlertText::LogonFailureText(code); *text)
				return text;
		}
		auto code = Field(data, L"SubStatus");
		if (code.empty() || code == L"0x0")
			code = Field(data, L"Status");
		return code.empty() || code == L"0x0" ? L"" : L"status " + code;
	}

	std::wstring Join(std::initializer_list<std::wstring> parts, PCWSTR separator) {
		std::wstring text;
		for (auto& p : parts)
			if (!p.empty())
				text += (text.empty() ? L"" : separator) + p;
		return text;
	}
}

bool EventAlertText::UsesData(std::wstring const& log, bool hasMessage) {
	return !hasMessage || ::_wcsicmp(log.c_str(), L"Security") == 0;
}

std::wstring EventAlertText::LogonTypeText(std::wstring const& type) {
	switch (_wtoi(type.c_str())) {
		case 2: return L"interactive";
		case 3: return L"network";
		case 4: return L"batch";
		case 5: return L"service";
		case 7: return L"unlock";
		case 8: return L"network, cleartext";
		case 9: return L"new credentials";
		case 10: return L"remote interactive (RDP)";
		case 11: return L"cached interactive";
	}
	return type.empty() ? L"" : L"type " + type;
}

PCWSTR EventAlertText::LogonFailureText(std::wstring const& status) {
	static const struct {
		uint32_t Code;
		PCWSTR Text;
	} failures[] = {
		{ 0xC000006A, L"wrong password" },
		{ 0xC0000064, L"no such user" },
		{ 0xC000006D, L"bad user name or password" },
		{ 0xC000006E, L"account restriction" },
		{ 0xC000006F, L"outside the allowed hours" },
		{ 0xC0000070, L"not allowed from this workstation" },
		{ 0xC0000071, L"password expired" },
		{ 0xC0000072, L"account disabled" },
		{ 0xC0000133, L"clocks out of sync" },
		{ 0xC000015B, L"logon type not granted" },
		{ 0xC0000193, L"account expired" },
		{ 0xC0000224, L"password must be changed" },
		{ 0xC0000234, L"account locked out" },
		{ 0xC0000413, L"authentication firewall" },
		// Kerberos (4771's Status: KDC error codes)
		{ 0x18, L"wrong password" },
		{ 0x6, L"no such user" },
		{ 0x12, L"account disabled, expired or locked out" },
		{ 0x17, L"password expired" },
		{ 0x25, L"clocks out of sync" },
	};
	if (status.empty())
		return L"";
	auto code = (uint32_t)::wcstoul(status.c_str(), nullptr, 0);
	if (code == 0)
		return L"";
	for (auto& f : failures)
		if (f.Code == code)
			return f.Text;
	return L"";
}

std::wstring EventAlertText::Summarize(std::wstring const& log, uint32_t eventId, std::wstring const& firstLine,
	std::vector<WinSys::EventDataItem> const& data) {
	if (data.empty() || !UsesData(log, !firstLine.empty()))
		return firstLine;
	bool security =::_wcsicmp(log.c_str(), L"Security") == 0;
	if (security) {
		auto target = Account(data, L"Target");
		auto subject = Account(data, L"Subject");
		auto logonType = LogonTypeText(Field(data, L"LogonType"));
		auto typeText = logonType.empty() ? L"" : L" (" + logonType + L")";
		std::wstring text;
		switch (eventId) {
			case 4624:	// a logon
				text = L"logon of " + (target.empty() ? L"(unknown)" : target) + From(data) + typeText;
				break;
			case 4625:	// a failed logon
				text = Join({ L"failed logon of " + (target.empty() ? L"(unknown)" : target) + From(data) + typeText, Failure(data) }, L": ");
				break;
			case 4648:	// a logon with explicit credentials (runas)
				text = Join({ L"explicit credentials for " + (target.empty() ? L"(unknown)" : target), subject.empty() ? L"" : L"by " + subject,
					Field(data, L"ProcessName") }, L", ");
				break;
			case 4688:	// a process created
				if (auto name = Field(data, L"NewProcessName"); !name.empty())
					text = Join({ name + L" created" + (subject.empty() ? L"" : L" by " + subject),
						Field(data, L"ParentProcessName").empty() ? L"" : L"parent " + Field(data, L"ParentProcessName") }, L", ");
				break;
			case 4720: case 4722: case 4725: case 4726: case 4738: case 4724: case 4723: {	// an account changed
				PCWSTR action = eventId == 4720 ? L"created" : eventId == 4722 ? L"enabled" : eventId == 4725 ? L"disabled"
					: eventId == 4726 ? L"deleted" : eventId == 4738 ? L"changed" : eventId == 4724 ? L"password reset" : L"password changed";
				if (!target.empty())
					text = std::format(L"account {} {}{}", target, action, subject.empty() ? L"" : L" by " + subject);
				break;
			}
			case 4728: case 4732: case 4756: case 4729: case 4733: case 4757: {	// a group's members changed
				bool added = eventId == 4728 || eventId == 4732 || eventId == 4756;
				auto member = Field(data, L"MemberName");
				if (member.empty())
					member = Field(data, L"MemberSid");
				if (!target.empty())
					text = std::format(L"{} {} group {}{}", member.empty() ? L"a member" : member, added ? L"added to" : L"removed from",
						target, subject.empty() ? L"" : L" by " + subject);
				break;
			}
			case 4740:	// an account locked out (TargetDomainName is the caller's computer here, not a domain)
				if (auto user = Field(data, L"TargetUserName"); !user.empty()) {
					auto caller = Field(data, L"TargetDomainName");
					text = L"account " + user + L" locked out" + (caller.empty() ? L"" : L", from " + caller);
				}
				break;
			case 4771:	// Kerberos pre-authentication failed
				text = Join({ L"Kerberos pre-authentication failed for " + (Field(data, L"TargetUserName").empty() ? L"(unknown)" : Field(data, L"TargetUserName"))
					+ From(data), Failure(data) }, L": ");
				break;
			case 4776:	// NTLM credential validation
				if (auto user = Field(data, L"TargetUserName"); !user.empty()) {
					auto failure = Failure(data);
					text = Join({ std::wstring(failure.empty() ? L"credentials validated for " : L"credential validation failed for ") + user + From(data), failure }, L": ");
				}
				break;
			case 4656: case 4663:	// an object accessed
				if (auto object = Field(data, L"ObjectName"); !object.empty())
					text = Join({ object + L" accessed" + (subject.empty() ? L"" : L" by " + subject), Field(data, L"ProcessName") }, L", ");
				break;
			case 4697: case 7045:	// a service installed
				if (auto service = Field(data, L"ServiceName"); !service.empty())
					text = Join({ L"service " + service + L" installed" + (subject.empty() ? L"" : L" by " + subject), Field(data, L"ServiceFileName") }, L", ");
				break;
			case 1102:	// the log cleared (UserData: SubjectUserName, SubjectDomainName)
				text = L"the Security log was cleared" + (subject.empty() ? std::wstring() : L" by " + subject);
				break;
		}
		if (!text.empty())
			return text;
	}
	// the line and the main fields
	static const struct {
		PCWSTR Name, Label;
	} fields[] = {
		{ L"TargetUserName", L"user" }, { L"SubjectUserName", L"by" }, { L"IpAddress", L"from" }, { L"NewProcessName", L"process" },
		{ L"ProcessName", L"process" }, { L"ObjectName", L"object" }, { L"ServiceName", L"service" }, { L"param1", L"" }, { L"param2", L"" },
	};
	std::wstring details;
	int count = 0;
	for (auto& f : fields) {
		std::wstring value = f.Name == std::wstring(L"TargetUserName") ? Account(data, L"Target")
			: f.Name == std::wstring(L"SubjectUserName") ? Account(data, L"Subject") : Field(data, f.Name);
		if (value.empty() || details.find(value) != std::wstring::npos)
			continue;
		details += (details.empty() ? L"" : L", ") + (*f.Label ? std::wstring(f.Label) + L" " : L"") + value;
		if (++count == 3)
			break;
	}
	// nothing known: the first fields, with their names
	if (details.empty() && firstLine.empty()) {
		for (auto& item : data) {
			if (item.Value.empty() || item.Value == L"-")
				continue;
			details += (details.empty() ? L"" : L", ") + (item.Name.empty() ? item.Value : item.Name + L" " + item.Value);
			if (++count == 3)
				break;
		}
	}
	if (details.empty())
		return firstLine;
	return firstLine.empty() ? details : firstLine + L" (" + details + L")";
}
