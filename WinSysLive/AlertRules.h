#pragma once

//
// Alert rules without the app around them, so the tests can take them (Tests\AlertTests.cpp): the kinds, a rule and its
// description, the AlertRules setting's text (one rule a line, Kind|Enabled|Threshold|Seconds|Pattern, the kind's key,
// which must not change) and a threshold condition's state. CAlerts (Alerts.h) evaluates them.
//
enum class AlertKind {
	ProcessStarted, ProcessExited, SystemCpu, CommitCharge, PhysicalMemory, ProcessCpu, ProcessPrivateBytes,
	ServiceStarted, ServiceStopped, ServiceFailed, _Count
};

struct AlertRule {
	AlertKind Kind{ AlertKind::ProcessStarted };
	bool Enabled{ true };
	std::wstring Pattern;	// image names (process kinds) or service names (service kinds)
	double Threshold{ 0 };	// percent; MB for private bytes
	int Seconds{ 0 };		// how long the value must stay above the threshold

	// "notepad.exe starts", "CPU above 90% for 10 seconds"
	std::wstring Describe() const;

	static PCWSTR KindName(AlertKind kind);	// for the rule dialog's combo box
	static bool HasPattern(AlertKind kind);
	static bool IsServiceKind(AlertKind kind);	// the pattern matches service names (or display names)
	static bool HasThreshold(AlertKind kind);
	static PCWSTR Unit(AlertKind kind);	// the threshold's: "%" or "MB"

	// a name matches a pattern of PathMatchSpec's (wildcards, ';'-separated patterns); an empty pattern matches anything
	static bool Matches(std::wstring const& pattern, std::wstring const& name);
	// a threshold as shown: whole numbers without decimals, else one
	static std::wstring FormatNumber(double value);
	// " for 10 seconds", " for a second", empty for 0
	static std::wstring ForSeconds(int seconds);

	// the setting's text; lines that aren't five fields or name no known kind are left out
	static std::vector<AlertRule> ParseRules(std::wstring const& text);
	// a pattern loses '|' and line breaks, which the text uses
	static std::wstring FormatRules(std::vector<AlertRule> const& rules);
};

// a threshold condition: since when it's held (a tick count in ms; 0: it doesn't) and whether it fired
struct AlertCondition {
	uint64_t Since{ 0 };
	bool Fired{ false };
	// true when it fires now: it has held for the seconds and hasn't fired since it last stopped holding. now must not be 0
	bool Update(bool holds, uint64_t now, int seconds);
};
