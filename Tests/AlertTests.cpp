#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/AlertRules.h"

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
