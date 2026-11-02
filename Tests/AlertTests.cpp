#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/AlertRules.h"
#include "../WinSysLive/EventFilter.h"
#include "../WinSysLive/ColumnLayout.h"
#include "../WinSysLive/XmlText.h"

// the AlertRules setting's text, the descriptions, the patterns and the threshold conditions (WinSysLive\AlertRules.h)

TEST(Alerts_FormatThenParseKeepsEveryKind) {
	std::vector<AlertRule> rules;
	for (int k = 0; k < (int)AlertKind::_Count; k++) {
		AlertRule r;
		r.Kind = (AlertKind)k;
		r.Enabled = k % 2 == 0;
		r.Threshold = k * 10 + 0.5;
		r.Seconds = k;
		r.Pattern = L"app" + std::to_wstring(k) + L".exe";
		rules.push_back(r);
	}
	auto parsed = AlertRule::ParseRules(AlertRule::FormatRules(rules));
	CHECK_EQ(parsed.size(), rules.size());
	for (size_t i = 0; i < parsed.size() && i < rules.size(); i++) {
		CHECK_EQ(parsed[i].Kind, rules[i].Kind);
		CHECK_EQ(parsed[i].Enabled, rules[i].Enabled);
		CHECK_EQ(parsed[i].Threshold, rules[i].Threshold);
		CHECK_EQ(parsed[i].Seconds, rules[i].Seconds);
		CHECK_EQ(parsed[i].Pattern, rules[i].Pattern);
	}
}

TEST(Alerts_KindKeysAreStable) {
	// the keys are stored: changing one loses the users' rules
	AlertRule cpu;
	cpu.Kind = AlertKind::SystemCpu;
	cpu.Threshold = 90;
	cpu.Seconds = 10;
	CHECK_EQ(AlertRule::FormatRules({ cpu }), std::wstring(L"SystemCpu|1|90|10|"));
	auto parsed = AlertRule::ParseRules(L"ProcessStarted|1|0|0|notepad.exe\nServiceFailed|0|0|0|Spooler\nProcessPrivate|1|512|30|chrome.exe");
	CHECK_EQ(parsed.size(), 3u);
	if (parsed.size() == 3) {
		CHECK_EQ(parsed[0].Kind, AlertKind::ProcessStarted);
		CHECK_EQ(parsed[1].Kind, AlertKind::ServiceFailed);
		CHECK_EQ(parsed[1].Enabled, false);
		CHECK_EQ(parsed[2].Kind, AlertKind::ProcessPrivateBytes);
		CHECK_EQ(parsed[2].Threshold, 512.0);
	}
}

TEST(Alerts_ParseSkipsBadLines) {
	auto parsed = AlertRule::ParseRules(L"Bogus|1|0|0|x\nProcessStarted|1|0|0\n\nProcessExited|1|0|0|a|b\nProcessExited|1|0|0|cmd.exe");
	CHECK_EQ(parsed.size(), 1u);
	if (!parsed.empty())
		CHECK_EQ(parsed[0].Pattern, std::wstring(L"cmd.exe"));
	CHECK(AlertRule::ParseRules(L"").empty());
}

TEST(Alerts_ParseTakesCrLf) {
	auto parsed = AlertRule::ParseRules(L"ProcessStarted|1|0|0|a.exe\r\nProcessExited|1|0|0|b.exe\r\n");
	CHECK_EQ(parsed.size(), 2u);
	if (parsed.size() == 2) {
		CHECK_EQ(parsed[0].Pattern, std::wstring(L"a.exe"));
		CHECK_EQ(parsed[1].Pattern, std::wstring(L"b.exe"));
	}
}

TEST(Alerts_FormatDropsSeparatorsFromPatterns) {
	AlertRule r;
	r.Pattern = L"a|b\r\nc.exe";
	CHECK_EQ(AlertRule::FormatRules({ r }), std::wstring(L"ProcessStarted|1|0|0|abc.exe"));
}

TEST(Alerts_Describe) {
	AlertRule r;
	r.Kind = AlertKind::SystemCpu;
	r.Threshold = 90;
	r.Seconds = 10;
	CHECK_EQ(r.Describe(), std::wstring(L"CPU above 90% for 10 seconds"));
	r.Seconds = 1;
	CHECK_EQ(r.Describe(), std::wstring(L"CPU above 90% for a second"));
	r.Kind = AlertKind::ProcessPrivateBytes;
	r.Threshold = 512.5;
	r.Seconds = 0;
	r.Pattern = L"chrome.exe";
	CHECK_EQ(r.Describe(), std::wstring(L"chrome.exe: private bytes above 512.5 MB"));
	r.Kind = AlertKind::ProcessStarted;
	r.Pattern.clear();
	CHECK_EQ(r.Describe(), std::wstring(L"Any process starts"));
	r.Kind = AlertKind::ServiceFailed;
	CHECK_EQ(r.Describe(), std::wstring(L"Any service fails (service)"));
}

TEST(Alerts_KindProperties) {
	CHECK(AlertRule::HasThreshold(AlertKind::ProcessCpu));
	CHECK(!AlertRule::HasThreshold(AlertKind::ProcessStarted));
	CHECK(!AlertRule::HasPattern(AlertKind::CommitCharge));
	CHECK(AlertRule::IsServiceKind(AlertKind::ServiceStopped));
	CHECK(!AlertRule::IsServiceKind(AlertKind::ProcessExited));
	CHECK_EQ(std::wstring(AlertRule::Unit(AlertKind::ProcessPrivateBytes)), std::wstring(L"MB"));
}

TEST(Alerts_PatternMatching) {
	CHECK(AlertRule::Matches(L"", L"anything.exe"));
	CHECK(AlertRule::Matches(L"note*.exe;calc.exe", L"notepad.exe"));
	CHECK(AlertRule::Matches(L"note*.exe;calc.exe", L"CALC.EXE"));
	CHECK(!AlertRule::Matches(L"note*.exe;calc.exe", L"cmd.exe"));
	CHECK(AlertRule::Matches(L"Windows Update", L"Windows Update"));
}

TEST(Alerts_ConditionFiresAfterItsSecondsOnce) {
	AlertCondition c;
	CHECK(!c.Update(true, 1000, 5));
	CHECK(!c.Update(true, 5999, 5));
	CHECK(c.Update(true, 6000, 5));		// held for 5 seconds
	CHECK(!c.Update(true, 7000, 5));	// fired already
	CHECK(!c.Update(true, 60000, 5));
}

TEST(Alerts_ConditionRearmsWhenItStopsHolding) {
	AlertCondition c;
	CHECK(c.Update(true, 1000, 0));		// no seconds: at once
	CHECK(!c.Update(true, 2000, 0));
	CHECK(!c.Update(false, 3000, 0));	// back under the threshold
	CHECK(c.Update(true, 4000, 0));		// above again: fires again
	AlertCondition d;
	CHECK(!d.Update(true, 1000, 10));
	CHECK(!d.Update(false, 5000, 10));	// a dip starts the wait over
	CHECK(!d.Update(true, 6000, 10));
	CHECK(!d.Update(true, 15999, 10));
	CHECK(d.Update(true, 16000, 10));
}

TEST(Alerts_Numbers) {
	CHECK_EQ(AlertRule::FormatNumber(90), std::wstring(L"90"));
	CHECK_EQ(AlertRule::FormatNumber(12.5), std::wstring(L"12.5"));
	CHECK_EQ(AlertRule::ForSeconds(0), std::wstring(L""));
	CHECK_EQ(AlertRule::ForSeconds(2), std::wstring(L" for 2 seconds"));
}

TEST(Alerts_EventRulesRoundTrip) {
	AlertRule e;
	e.Kind = AlertKind::EventLogged;
	e.Pattern = L"Service Control Manager";
	e.Log = L"System";
	e.EventId = 7031;
	AlertRule a;
	a.Kind = AlertKind::AuditFailure;
	a.Log = L"Security";
	AlertRule cpu;
	cpu.Kind = AlertKind::SystemCpu;
	auto text = AlertRule::FormatRules({ e, a, cpu });
	// the event kinds have seven fields, the others still five (older builds read those)
	CHECK_EQ(text, std::wstring(L"EventLogged|1|0|0|Service Control Manager|System|7031\nAuditFailure|1|0|0||Security|0\nSystemCpu|1|0|0|"));
	auto parsed = AlertRule::ParseRules(text);
	CHECK_EQ(parsed.size(), 3u);
	if (parsed.size() == 3) {
		CHECK_EQ(parsed[0].Log, std::wstring(L"System"));
		CHECK_EQ(parsed[0].EventId, 7031u);
		CHECK_EQ(parsed[1].Kind, AlertKind::AuditFailure);
		CHECK(parsed[2].Log.empty());
	}
}

TEST(Alerts_EventMatching) {
	AlertRule e;
	e.Kind = AlertKind::EventLogged;
	e.Pattern = L"Service Control*";
	e.Log = L"System";
	e.EventId = 7031;
	CHECK(e.MatchesEvent(L"System", L"Service Control Manager", 7031, false));
	CHECK(e.MatchesEvent(L"system", L"service control manager", 7031, false));
	CHECK(!e.MatchesEvent(L"Application", L"Service Control Manager", 7031, false));
	CHECK(!e.MatchesEvent(L"System", L"Service Control Manager", 7036, false));
	CHECK(!e.MatchesEvent(L"System", L"Tcpip", 7031, false));
	e.Log.clear();
	e.EventId = 0;
	CHECK(e.MatchesEvent(L"Application", L"Service Control Manager", 1, false));
	AlertRule a;
	a.Kind = AlertKind::AuditFailure;
	CHECK(a.MatchesEvent(L"Security", L"Microsoft-Windows-Security-Auditing", 4625, true));
	CHECK(!a.MatchesEvent(L"Security", L"Microsoft-Windows-Security-Auditing", 4624, false));
	CHECK(!a.MatchesEvent(L"System", L"x", 1, true));
	a.EventId = 4625;
	CHECK(!a.MatchesEvent(L"Security", L"Microsoft-Windows-Security-Auditing", 4656, true));
	AlertRule cpu;
	cpu.Kind = AlertKind::SystemCpu;
	CHECK(!cpu.MatchesEvent(L"System", L"x", 1, false));
}

TEST(Alerts_EventQueries) {
	AlertRule scm;
	scm.Kind = AlertKind::EventLogged;
	scm.Pattern = L"Service Control Manager";
	scm.Log = L"System";
	scm.EventId = 7031;
	AlertRule audit;
	audit.Kind = AlertKind::AuditFailure;
	audit.EventId = 4625;
	std::vector<AlertRule> rules{ scm, audit };
	CHECK_EQ(AlertRule::EventQuery(rules, L"System"), std::wstring(L"*[System[(Provider[@Name='Service Control Manager'] and EventID=7031)]]"));
	CHECK_EQ(AlertRule::EventQuery(rules, L"Security"), std::wstring(L"*[System[(EventID=4625 and band(Keywords,4503599627370496))]]"));
	CHECK(AlertRule::EventQuery(rules, L"Application").empty());
	// a wildcard source is matched after arrival: only the ID narrows it; nothing narrows it: every event
	AlertRule wild;
	wild.Kind = AlertKind::EventLogged;
	wild.Pattern = L"Microsoft-Windows-*";
	wild.EventId = 1000;
	CHECK_EQ(AlertRule::EventQuery({ wild }, L"Application"), std::wstring(L"*[System[(EventID=1000)]]"));
	wild.EventId = 0;
	CHECK_EQ(AlertRule::EventQuery({ wild }, L"Application"), std::wstring(L"*"));
	// several rules on a log are ORed; a disabled one counts for nothing
	AlertRule tcp = scm;
	tcp.Pattern = L"Tcpip";
	tcp.EventId = 4266;
	AlertRule off = scm;
	off.Enabled = false;
	off.EventId = 1;
	CHECK_EQ(AlertRule::EventQuery({ scm, tcp, off }, L"System"),
		std::wstring(L"*[System[(Provider[@Name='Service Control Manager'] and EventID=7031) or (Provider[@Name='Tcpip'] and EventID=4266)]]"));
	// a source with an apostrophe is quoted with double quotes
	AlertRule quoted = scm;
	quoted.Pattern = L"O'Brien's Service";
	CHECK_EQ(AlertRule::EventQuery({ quoted }, L"System"), std::wstring(L"*[System[(Provider[@Name=\"O'Brien's Service\"] and EventID=7031)]]"));
}

TEST(Alerts_EventDescriptions) {
	AlertRule e;
	e.Kind = AlertKind::EventLogged;
	e.Pattern = L"Service Control Manager";
	e.Log = L"System";
	e.EventId = 7031;
	CHECK_EQ(e.Describe(), std::wstring(L"Event 7031 from Service Control Manager in System"));
	e.EventId = 0;
	e.Log.clear();
	CHECK_EQ(e.Describe(), std::wstring(L"An event from Service Control Manager in any log"));
	AlertRule a;
	a.Kind = AlertKind::AuditFailure;
	CHECK_EQ(a.Describe(), std::wstring(L"Security audit failure"));
	a.EventId = 4625;
	CHECK_EQ(a.Describe(), std::wstring(L"Security audit failure 4625"));
}

TEST(Alerts_EventText) {
	using Data = std::vector<WinSys::EventDataItem>;
	// a failed logon: the sub-status's reason rather than the generic status, the address, the type
	Data failed = { { L"SubjectUserSid", L"S-1-0-0" }, { L"TargetUserName", L"bob" }, { L"TargetDomainName", L"ZODIAC" },
		{ L"Status", L"0xc000006d" }, { L"FailureReason", L"%%2313" }, { L"SubStatus", L"0xc000006a" }, { L"LogonType", L"3" },
		{ L"WorkstationName", L"LAPTOP" }, { L"IpAddress", L"10.0.0.5" } };
	CHECK_EQ(EventAlertText::Summarize(L"Security", 4625, L"An account failed to log on.", failed),
		std::wstring(LR"(failed logon of ZODIAC\bob from 10.0.0.5 (network): wrong password)"));
	// a process created
	Data created = { { L"SubjectUserName", L"zodia" }, { L"SubjectDomainName", L"ZODIAC" },
		{ L"NewProcessName", LR"(C:\Windows\System32\cmd.exe)" }, { L"ParentProcessName", LR"(C:\Windows\explorer.exe)" } };
	CHECK_EQ(EventAlertText::Summarize(L"Security", 4688, L"A new process has been created.", created),
		std::wstring(LR"(C:\Windows\System32\cmd.exe created by ZODIAC\zodia, parent C:\Windows\explorer.exe)"));
	// the log cleared (UserData's fields)
	Data cleared = { { L"SubjectUserSid", L"S-1-5-21-1" }, { L"SubjectUserName", L"zodia" }, { L"SubjectDomainName", L"ZODIAC" } };
	CHECK_EQ(EventAlertText::Summarize(L"Security", 1102, L"The audit log was cleared.", cleared),
		std::wstring(LR"(the Security log was cleared by ZODIAC\zodia)"));
	// another Security event: the line and its main fields
	Data other = { { L"SubjectUserName", L"zodia" }, { L"SubjectDomainName", L"ZODIAC" }, { L"ObjectName", LR"(\Device\X)" } };
	CHECK_EQ(EventAlertText::Summarize(L"Security", 4907, L"Auditing settings on object were changed.", other),
		std::wstring(LR"(Auditing settings on object were changed. (by ZODIAC\zodia, object \Device\X))"));
	// a System event with a message: unchanged; without one: its first fields
	CHECK_EQ(EventAlertText::Summarize(L"System", 7031, L"The X service terminated unexpectedly.", other),
		std::wstring(L"The X service terminated unexpectedly."));
	CHECK(!EventAlertText::UsesData(L"System", true));
	CHECK(EventAlertText::UsesData(L"System", false));
	CHECK(EventAlertText::UsesData(L"Security", true));
	Data unnamed = { { L"", L"first" }, { L"", L"-" }, { L"", L"second" } };
	CHECK_EQ(EventAlertText::Summarize(L"Application", 1000, L"", unnamed), std::wstring(L"first, second"));
	// no data: the line
	CHECK_EQ(EventAlertText::Summarize(L"Security", 4625, L"An account failed to log on.", {}), std::wstring(L"An account failed to log on."));
	// the words
	CHECK_EQ(EventAlertText::LogonTypeText(L"10"), std::wstring(L"remote interactive (RDP)"));
	CHECK_EQ(EventAlertText::LogonTypeText(L"99"), std::wstring(L"type 99"));
	CHECK_EQ(std::wstring(EventAlertText::LogonFailureText(L"0xC0000234")), std::wstring(L"account locked out"));
	CHECK_EQ(std::wstring(EventAlertText::LogonFailureText(L"0x0")), std::wstring());
}

TEST(Alerts_Throttle) {
	// without a quiet period: up to GroupLimit each, more as one
	AlertThrottle t;
	auto shown = t.Apply({ L"a", L"b", L"c" }, 1000, 0);
	CHECK_EQ(shown.size(), (size_t)3);
	shown = t.Apply({ L"a", L"b", L"c", L"d", L"e" }, 2000, 0);
	CHECK_EQ(shown.size(), (size_t)1);
	if (shown.size() == 1) {
		CHECK_EQ(shown[0].first, (size_t)0);
		CHECK_EQ(shown[0].second, std::wstring(L"a (and 4 more like it)"));
	}
	CHECK(t.Apply({}, 3000, 0).empty());

	// a quiet period of 10 seconds: one an update, nothing during it, the next one counting those held back
	AlertThrottle q;
	shown = q.Apply({ L"first", L"second" }, 10000, 10);
	CHECK_EQ(shown.size(), (size_t)1);
	if (shown.size() == 1)
		CHECK_EQ(shown[0].second, std::wstring(L"first (and 1 more like it)"));
	CHECK(q.Apply({ L"x", L"y" }, 15000, 10).empty());
	CHECK(q.Apply({ L"z" }, 19999, 10).empty());
	shown = q.Apply({ L"after" }, 20000, 10);
	CHECK_EQ(shown.size(), (size_t)1);
	if (shown.size() == 1)
		CHECK_EQ(shown[0].second, std::wstring(L"after (3 more during the quiet period)"));
	// held and grouped together
	CHECK(q.Apply({ L"h" }, 25000, 10).empty());
	shown = q.Apply({ L"p", L"q" }, 30000, 10);
	if (shown.size() == 1)
		CHECK_EQ(shown[0].second, std::wstring(L"p (and 1 more like it; 1 more during the quiet period)"));
	// the period turned off: everything again
	shown = q.Apply({ L"a", L"b" }, 30001, 0);
	CHECK_EQ(shown.size(), (size_t)2);
}

TEST(Alerts_QuietPeriodText) {
	// written as an eighth field only when set (empty event fields for the other kinds); read back
	AlertRule r;
	r.Kind = AlertKind::ProcessStarted;
	r.Pattern = L"cmd.exe";
	CHECK_EQ(AlertRule::FormatRules({ r }), std::wstring(L"ProcessStarted|1|0|0|cmd.exe"));
	r.Quiet = 60;
	CHECK_EQ(AlertRule::FormatRules({ r }), std::wstring(L"ProcessStarted|1|0|0|cmd.exe||0|60"));
	CHECK_EQ(r.Describe(), std::wstring(L"cmd.exe starts, at most once a minute"));
	auto rules = AlertRule::ParseRules(L"ProcessStarted|1|0|0|cmd.exe||0|60\nEventLogged|1|0|0|X|System|7031|30\nEventLogged|1|0|0|Y|System|1");
	CHECK_EQ(rules.size(), (size_t)3);
	if (rules.size() == 3) {
		CHECK_EQ(rules[0].Quiet, 60);
		CHECK(rules[0].Log.empty());
		CHECK_EQ(rules[1].Quiet, 30);
		CHECK_EQ(rules[1].EventId, (uint32_t)7031);
		CHECK_EQ(rules[1].Describe(), std::wstring(L"Event 7031 from X in System, at most once in 30 seconds"));
		CHECK_EQ(rules[2].Quiet, 0);
	}
	r.Quiet = 300;
	CHECK_EQ(r.Describe(), std::wstring(L"cmd.exe starts, at most once in 5 minutes"));
}

TEST(Events_FilterIds) {
	// ids, lists and ranges, with text around them
	auto f = EventFilter::Parse(L"id:4625");
	CHECK(f.Text.empty());
	CHECK(f.MatchesId(4625));
	CHECK(!f.MatchesId(4624));
	f = EventFilter::Parse(L"  bob id:4624,4720-4767   ZODIAC ");
	CHECK_EQ(f.Text, std::wstring(L"bob ZODIAC"));
	CHECK(f.MatchesId(4624));
	CHECK(f.MatchesId(4720));
	CHECK(f.MatchesId(4767));
	CHECK(!f.MatchesId(4768));
	CHECK(!f.MatchesId(4625));
	// tokens add up; case doesn't matter
	f = EventFilter::Parse(L"ID:1 id:2");
	CHECK(f.MatchesId(1) && f.MatchesId(2) && !f.MatchesId(3));
	// not ID lists: text, and no ID restriction
	for (auto text : { L"id:", L"id:abc", L"id:5-3", L"id:1,,2", L"id:-4", L"id:12345678901" }) {
		f = EventFilter::Parse(text);
		CHECK_EQ(f.Text, std::wstring(text));
		CHECK(f.MatchesId(12345));
	}
	f = EventFilter::Parse(L"");
	CHECK(f.Text.empty());
	CHECK(f.MatchesId(0));
}

namespace {
	using ColumnLayout::ColumnDef;
	// a table as a view's: Name mandatory, Size added in version 1 (shown by default), Owner in version 2 (not shown)
	const ColumnDef s_Defs[] = {
		{ L"Name", L"Name", 200, LVCFMT_LEFT, true, true },
		{ L"Type", L"Type", 100, LVCFMT_LEFT, true },
		{ L"Path", L"Path", 300, LVCFMT_LEFT, false },
		{ L"Size", L"Size", 80, LVCFMT_RIGHT, true, false, nullptr, 1 },
		{ L"Owner", L"Owner", 120, LVCFMT_LEFT, false, false, nullptr, 2 },
	};

	std::wstring Keys(std::vector<ColumnLayout::ShownColumn> const& columns) {
		std::wstring text;
		for (auto& c : columns)
			text += (text.empty() ? L"" : L",") + std::wstring(s_Defs[c.Id].Key);
		return text;
	}
}

TEST(ColumnLayout_Parse) {
	// nothing: the defaults, in table order
	CHECK_EQ(Keys(ColumnLayout::Parse(L"", s_Defs)), std::wstring(L"Name,Type,Size"));
	// widths kept, unknown keys and bad widths skipped, duplicates once, the mandatory column added first
	auto columns = ColumnLayout::Parse(L"Type:150,Gone:10,Path:0,Type:90,Path:250", s_Defs);
	CHECK_EQ(Keys(columns), std::wstring(L"Name,Type,Path,Size"));
	if (columns.size() == 4)
		CHECK_EQ(columns[1].Width, 150);
}

TEST(ColumnLayout_NewDefaultColumns) {
	// a layout saved before versions: Size, a default column added since, comes after the nearest column before it in
	// the table that's shown (Path, wherever it's been dragged)
	auto columns = ColumnLayout::Parse(L"Name:200,Path:300,Type:100", s_Defs);
	CHECK_EQ(Keys(columns), std::wstring(L"Name,Path,Size,Type"));
	// written with the table's version, so hiding it then sticks; Owner (not a default) never comes by itself
	CHECK_EQ(ColumnLayout::Version(s_Defs), 2);
	auto text = ColumnLayout::Format(columns, s_Defs);
	CHECK_EQ(text, std::wstring(L"Name:200,Path:300,Size:80,Type:100,~:2"));
	CHECK_EQ(Keys(ColumnLayout::Parse(L"Name:200,Type:100,~:2", s_Defs)), std::wstring(L"Name,Type"));
	// saved at version 1: Size was there then (hidden since), nothing new to add
	CHECK_EQ(Keys(ColumnLayout::Parse(L"Type:100,Name:200,~:1", s_Defs)), std::wstring(L"Type,Name"));
	// a shown column before it in the table missing: after the nearest one that's there
	CHECK_EQ(Keys(ColumnLayout::Parse(L"Type:100,Name:200", s_Defs)), std::wstring(L"Type,Size,Name"));
	// a table without added columns writes no version
	const ColumnDef plain[] = { { L"A", L"A", 10, LVCFMT_LEFT, true } };
	CHECK_EQ(ColumnLayout::Format({ { 0, 10 } }, plain), std::wstring(L"A:10"));
}

TEST(XmlText_Indent) {
	// a manifest as the linker writes it: the declaration, a line break, then everything on one line
	auto text = XmlText::Indent(L"<?xml version=\"1.0\"?>\r\n<assembly a='1'><dependency><x/></dependency><b>text</b></assembly>");
	CHECK_EQ(text, std::wstring(L"<?xml version=\"1.0\"?>\n<assembly a='1'>\n  <dependency>\n    <x/>\n  </dependency>\n  <b>text</b>\n</assembly>"));
	// the source's own layout replaced, an event's data on its lines
	text = XmlText::Indent(L"<Event>\n    <Data Name='a'>1</Data>\n\t<Data Name='b'/>\n</Event>");
	CHECK_EQ(text, std::wstring(L"<Event>\n  <Data Name='a'>1</Data>\n  <Data Name='b'/>\n</Event>"));
	// broken: what's there, no crash
	CHECK_EQ(XmlText::Indent(L"<a><b"), std::wstring(L"<a><b"));
	CHECK(XmlText::Indent(L"").empty());
}
