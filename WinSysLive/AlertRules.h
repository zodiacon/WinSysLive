#pragma once

#include <WinSys/EventLog.h>

//
// Alert rules without the app around them, so the tests can take them (Tests\AlertTests.cpp): the kinds, a rule and its
// description, the AlertRules setting's text (one rule a line, Kind|Enabled|Threshold|Seconds|Pattern, the kind's key,
// which must not change; the event kinds add |Log|EventId, and a quiet period |Log|EventId|Quiet, written only when set:
// lines older builds skip), the event matching and the event log queries built from the rules, a threshold condition's
// state and the throttle of a rule's alerts. CAlerts (Alerts.h) evaluates them.
//
enum class AlertKind {
	ProcessStarted, ProcessExited, SystemCpu, CommitCharge, PhysicalMemory, ProcessCpu, ProcessPrivateBytes,
	ServiceStarted, ServiceStopped, ServiceFailed, EventLogged, AuditFailure, ProcessTampered, _Count
};

struct AlertRule {
	AlertKind Kind{ AlertKind::ProcessStarted };
	bool Enabled{ true };
	std::wstring Pattern;	// image names (process kinds), service names (service kinds), the event's source (event kinds)
	double Threshold{ 0 };	// percent; MB for private bytes
	int Seconds{ 0 };		// how long the value must stay above the threshold
	std::wstring Log;		// the event kinds': System, Application or Security (empty: any of them); AuditFailure's is Security
	uint32_t EventId{ 0 };	// the event kinds': 0 for any
	int Quiet{ 0 };			// seconds after an alert in which the rule's next ones are held back (0: none)

	// "notepad.exe starts", "CPU above 90% for 10 seconds", with the quiet period ("..., at most once a minute")
	std::wstring Describe() const;
	// the same without the quiet period
	std::wstring DescribeCondition() const;

	static PCWSTR KindName(AlertKind kind);	// for the rule dialog's combo box
	static bool HasPattern(AlertKind kind);
	static bool IsServiceKind(AlertKind kind);	// the pattern matches service names (or display names)
	static bool HasThreshold(AlertKind kind);
	static PCWSTR Unit(AlertKind kind);	// the threshold's: "%" or "MB"
	static bool IsEventKind(AlertKind kind);
	// the logs event rules watch
	static std::vector<std::wstring> const& EventLogs();

	// an event of the log, source and ID (the audit failure keyword told) fires this rule (enabled or not)
	bool MatchesEvent(std::wstring const& log, std::wstring const& source, uint32_t eventId, bool auditFailure) const;
	// the XPath query for the enabled event rules watching the log: their sources (exact names only: a pattern with
	// wildcards is matched after arrival), IDs and the audit failure keyword ORed; "*" if one of them takes every event,
	// empty if none watches the log
	static std::wstring EventQuery(std::vector<AlertRule> const& rules, std::wstring const& log);

	// a name matches a pattern of PathMatchSpec's (wildcards, ';'-separated patterns); an empty pattern matches anything
	static bool Matches(std::wstring const& pattern, std::wstring const& name);
	// a threshold as shown: whole numbers without decimals, else one
	static std::wstring FormatNumber(double value);
	// " for 10 seconds", " for a second", empty for 0
	static std::wstring ForSeconds(int seconds);

	// the setting's text; lines that aren't five, seven or eight fields or name no known kind are left out
	static std::vector<AlertRule> ParseRules(std::wstring const& text);
	// a pattern loses '|' and line breaks, which the text uses
	static std::wstring FormatRules(std::vector<AlertRule> const& rules);
};

// a rule's alerts in one update, throttled: more than GroupLimit at once become one ("<the first> (and 24 more like
// it)"); with a quiet period, at most one an update, and none until the period after it ends, the next one saying how
// many were held back ("(5 more during the quiet period)"; those held back when nothing fires after the period aren't
// reported)
struct AlertThrottle {
	static constexpr size_t GroupLimit = 3;
	uint64_t QuietUntil{ 0 };	// a tick count in ms
	size_t Held{ 0 };			// held back since the last alert shown

	// the alerts to show of this update's texts: each one's index and its text (with the counts added)
	std::vector<std::pair<size_t, std::wstring>> Apply(std::vector<std::wstring> const& texts, uint64_t now, int quietSeconds);
};

// an event alert's text from the event's data fields (WinSys EventLog::GetData), rather than the message's first line
// alone, which says little for the Security log ("An account failed to log on.")
struct EventAlertText final {
	// the well-known Security events in words ("failed logon of ZODIAC\bob from 10.0.0.5 (network): wrong password",
	// "C:\Windows\System32\cmd.exe created by ZODIAC\zodia"); other Security events, and any event without a message,
	// the message's first line with up to three main fields after it ("... (user ZODIAC\bob, process C:\x.exe)"); else the
	// line
	static std::wstring Summarize(std::wstring const& log, uint32_t eventId, std::wstring const& firstLine,
		std::vector<WinSys::EventDataItem> const& data);
	// the events whose data Summarize uses (the Security log's, and those without a message): the caller reads the data
	// only for these
	static bool UsesData(std::wstring const& log, bool hasMessage);
	// a logon type in words: 3 "network", 10 "remote interactive (RDP)"; the number for others
	static std::wstring LogonTypeText(std::wstring const& type);
	// an NTSTATUS of a failed logon in words: 0xC000006A "wrong password"; empty for others
	static PCWSTR LogonFailureText(std::wstring const& status);
};

// a threshold condition: since when it's held (a tick count in ms; 0: it doesn't) and whether it fired
struct AlertCondition {
	uint64_t Since{ 0 };
	bool Fired{ false };
	// true when it fires now: it has held for the seconds and hasn't fired since it last stopped holding. now must not be 0
	bool Update(bool holds, uint64_t now, int seconds);
};
