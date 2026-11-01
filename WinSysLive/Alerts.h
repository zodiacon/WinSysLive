#pragma once

#include <WinSys/ProcessManager.h>
#include <WinSys/Keys.h>
#include <WinSys/ServiceInfo.h>
#include "ProcessInfoEx.h"
#include "AlertRules.h"
#include <deque>
#include <mutex>
#include <WinSys/EventLog.h>

//
// Alerts (Options > Alerts..., CAlertsDlg): rules evaluated after every snapshot update (CMainFrame::Refresh), which
// raise a notification balloon (CAlertIcon) and go to a list of recent alerts. A process rule matches image names with
// PathMatchSpec (wildcards, several patterns separated by ';'; empty: any process). Threshold rules fire once the value
// has been above the threshold for the given seconds (0: at once), then not again until it's back under it; for process
// rules that's per process. Nothing fires on the first update: what's running then is the baseline. The rules, their
// setting's text and the conditions are in AlertRules.h.
//
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
	std::vector<AlertEvent> Evaluate(WinSys::ProcessManager<ProcessInfoEx>& pm, std::optional<double> cpu);

	std::deque<AlertEvent> const& GetRecent() const;
	void ClearRecent();

	static constexpr size_t RecentLimit = 500;

private:
	struct RuleState {
		AlertCondition System;
		std::unordered_map<WinSys::ProcessOrThreadKey, AlertCondition> Processes;
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
