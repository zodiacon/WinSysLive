#pragma once

#include <WinSys/ProcessManager.h>
#include <WinSys/Keys.h>
#include <WinSys/ServiceInfo.h>
#include "ProcessInfoEx.h"
#include "AlertRules.h"
#include "IntegrityChecker.h"
#include <WinSys/ProcessCreation.h>
#include <unordered_set>
#include <deque>
#include <mutex>
#include <WinSys/EventLog.h>

class LeakWatcher;

//
// Alerts (Options > Alerts..., CAlertsDlg): rules evaluated after every snapshot update (CMainFrame::Refresh), which
// raise a notification balloon (CAlertIcon) and go to a list of recent alerts. A process rule matches image names with
// PathMatchSpec (wildcards, several patterns separated by ';'; empty: any process). Threshold rules fire once the value
// has been above the threshold for the given seconds (0: at once), then not again until it's back under it; for process
// rules that's per process. Nothing fires on the first update: what's running then is the baseline. The rules, their
// setting's text and the conditions are in AlertRules.h.
//
// how often a rule fired since the app started: its matches, and the alerts they became (fewer when grouped or held
// back in its quiet period)
struct AlertCount {
	size_t Fired{ 0 }, Shown{ 0 };
};

struct AlertEvent {
	int64_t Time{ 0 };	// FILETIME, UTC
	std::wstring Text;
	uint32_t Pid{ 0 };	// a process alert's (an exited process's too, for the text)
	bool Running{ false };	// the process is still running (Go to Process makes sense)
};

// The event rules are fed by an event log watcher of its own (the rules' queries, AlertRule::EventQuery, on System,
// Application and Security; started while an event rule is on, again when the rules change); its callbacks queue the
// events (at most MaxQueued, as updates may be paused), which Evaluate matches on the UI thread.
class CAlerts : public WinSys::EventLogSink {
public:
	~CAlerts();
	// EventLogSink, on wevtapi's threads
	void OnEvent(WinSys::EventLogEntry&& entry) override;

	static std::vector<AlertRule> Load();
	static void Save(std::vector<AlertRule> const& rules);

	void SetRules(std::vector<AlertRule> rules);
	std::vector<AlertRule> const& GetRules() const;
	bool AnyEnabled() const;

	// after every update of the snapshot; cpu: the system's, percent (none on the first update). The new alerts, which
	// are also added to the recent ones
	// leaks: Leak Watch's findings (null while it's off: the ProcessLeaks rules fire nothing)
	std::vector<AlertEvent> Evaluate(WinSys::ProcessManager<ProcessInfoEx>& pm, std::optional<double> cpu, LeakWatcher const* leaks);

	std::deque<AlertEvent> const& GetRecent() const;
	// the rule's counts (kept across SetRules for a rule that stays the same, enabled or not)
	AlertCount GetCount(AlertRule const& rule) const;
	void ClearRecent();

	static constexpr size_t RecentLimit = 500;

private:
	struct RuleState {
		AlertCondition System;
		std::unordered_map<WinSys::ProcessOrThreadKey, AlertCondition> Processes;
		AlertThrottle Throttle;	// the rule's alerts grouped and held back (its quiet period)
		// ProcessTampered: the running processes it reported, an image and a parent apart (each once)
		std::unordered_set<WinSys::ProcessOrThreadKey> ImageReported, ParentReported;
		// ProcessLeaks: the counters reported of each process (bits by LeakCounter), each once while it runs
		std::unordered_map<WinSys::ProcessOrThreadKey, unsigned> LeakReported;
	};
	// while a ProcessTampered rule is on: the images checked (once a process is two seconds old: a hollowing is done
	// while it's suspended) and who created each process (elevated only)
	void UpdateTampering(WinSys::ProcessManager<ProcessInfoEx>& pm);
	IntegrityChecker m_Integrity;
	std::shared_ptr<WinSys::ProcessCreationTracker> m_Creations;
	bool m_CreationsTried{ false };

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
	std::unordered_map<std::wstring, AlertCount> m_Counts;	// by CountKey
	// the rule's text, enabled: a rule edited is a new one, one turned off and on the same
	static std::wstring CountKey(AlertRule rule);
	bool m_Baseline{ false };	// the first update was seen
	int64_t m_PageSize{ 0 }, m_PhysicalTotal{ 0 };

	// the event rules' watcher: started, restarted or stopped for the rules
	void UpdateEventWatcher();
	static constexpr size_t MaxQueued = 1000;
	std::mutex m_EventLock;
	std::vector<WinSys::EventLogEntry> m_Events;	// queued by OnEvent
	WinSys::EventLogWatcher m_EventWatcher;	// last: destroyed (stopped) before the queue it fills
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
