#pragma once

#include <WinSys/ProcessManager.h>
#include <WinSys/Keys.h>
#include <WinSys/ServiceInfo.h>
#include "ProcessInfoEx.h"
#include <deque>

//
// Alerts (Options > Alerts..., CAlertsDlg): rules evaluated after every snapshot update (CMainFrame::Refresh), which
// raise a notification balloon (CAlertIcon) and go to a list of recent alerts. A process rule matches image names with
// PathMatchSpec (wildcards, several patterns separated by ';'; empty: any process). Threshold rules fire once the value
// has been above the threshold for the given seconds (0: at once), then not again until it's back under it; for process
// rules that's per process. Nothing fires on the first update: what's running then is the baseline. Rules are kept in
// the AlertRules setting, one per line: Kind|Enabled|Threshold|Seconds|Pattern (the kind's key, which must not change).
//
enum class AlertKind {
	ProcessStarted, ProcessExited, SystemCpu, CommitCharge, PhysicalMemory, ProcessCpu, ProcessPrivateBytes,
	ServiceStarted, ServiceStopped, ServiceFailed, _Count
};

struct AlertRule {
	AlertKind Kind{ AlertKind::ProcessStarted };
	bool Enabled{ true };
	std::wstring Pattern;	// image names, for the process kinds
	double Threshold{ 0 };	// percent; MB for private bytes
	int Seconds{ 0 };		// how long the value must stay above the threshold

	// "notepad.exe starts", "CPU above 90% for 10 seconds"
	std::wstring Describe() const;

	static PCWSTR KindName(AlertKind kind);	// for the rule dialog's combo box
	static bool HasPattern(AlertKind kind);
	static bool IsServiceKind(AlertKind kind);	// the pattern matches service names (or display names)
	static bool HasThreshold(AlertKind kind);
	static PCWSTR Unit(AlertKind kind);	// the threshold's: "%" or "MB"
};

struct AlertEvent {
	int64_t Time{ 0 };	// FILETIME, UTC
	std::wstring Text;
	uint32_t Pid{ 0 };	// a process alert's (an exited process's too, for the text)
	bool Running{ false };	// the process is still running (Go to Process makes sense)
};

class CAlerts {
public:
	static std::vector<AlertRule> Load();
	static void Save(std::vector<AlertRule> const& rules);

	void SetRules(std::vector<AlertRule> rules);
	std::vector<AlertRule> const& GetRules() const;
	bool AnyEnabled() const;

	// after every update of the snapshot; cpu: the system's, percent (none on the first update). The new alerts, which
	// are also added to the recent ones
	std::vector<AlertEvent> Evaluate(WinSys::ProcessManager<ProcessInfoEx>& pm, std::optional<double> cpu);

	std::deque<AlertEvent> const& GetRecent() const;
	void ClearRecent();

	static constexpr size_t RecentLimit = 500;

private:
	// a condition's state: since when it's held (GetTickCount64; 0: it doesn't) and whether it fired
	struct Condition {
		uint64_t Since{ 0 };
		bool Fired{ false };
		// true when it fires now
		bool Update(bool holds, uint64_t now, int seconds);
	};
	struct RuleState {
		Condition System;
		std::unordered_map<WinSys::ProcessOrThreadKey, Condition> Processes;
	};

	// a service as last seen: its state (not a pending one) and process
	struct KnownService {
		WinSys::ServiceState State;
		uint32_t Pid;
	};
	// the services' state changes since the last call (pending states waited out); none the first time (the baseline)
	struct ServiceChange {
		std::wstring Name, DisplayName;
		WinSys::ServiceState State;	// Running or Stopped
		uint32_t Pid;	// the process it ran or runs in
		DWORD ExitCode;	// a stopped service's Win32 exit code (ERROR_SERVICE_SPECIFIC_ERROR: ServiceExitCode is the code)
		DWORD ServiceExitCode;
	};
	std::vector<ServiceChange> UpdateServices();

	std::vector<AlertRule> m_Rules;
	std::vector<RuleState> m_States;	// by m_Rules' index
	std::unordered_map<std::wstring, KnownService> m_Services;	// while a service rule is on (empty: the next read is a baseline)
	std::deque<AlertEvent> m_Recent;	// the newest last
	bool m_Baseline{ false };	// the first update was seen
	int64_t m_PageSize{ 0 }, m_PhysicalTotal{ 0 };
};

//
// The notification area icon alerts are shown from (the app's icon, ID AlertIcon::Id), there while any rule is enabled,
// so that a balloon doesn't depend on the graph icons. Its mouse messages go to the frame like theirs (WM_TRAYICON);
// clicking a balloon sends NIN_BALLOONUSERCLICK.
//
class CAlertIcon {
public:
	static constexpr UINT Id = 100;

	~CAlertIcon();
	void Init(HWND hWnd, UINT callbackMessage);
	void Show(bool show);
	// Explorer restarted
	void AddAgain();
	void Notify(std::wstring const& title, std::wstring const& text);

private:
	bool Add();
	void Remove();

	HWND m_hWnd{ nullptr };
	UINT m_Message{ 0 };
	bool m_Shown{ false };
	bool m_Wanted{ false };
};
