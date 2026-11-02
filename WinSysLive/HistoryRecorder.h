#pragma once

#include "ProcessInfoEx.h"
#include <WinSys/Processes.h>
#include <WinSys/ServiceInfo.h>
#include <WinSys/Keys.h>
#include <unordered_set>
#include <deque>

//
// What the History tab shows, recorded apart from it so it can record from the app's start (Options > Record from
// Startup) as well as while the tab is open: processes started and exited (from the frame's snapshot: a start at the
// process's creation time, with its parent, user and command line; an exit with how long it ran and its exit code,
// through a handle opened when the process was first seen), and services started, stopped (a failure's exit code as
// a warning), paused or resumed, installed and deleted (enumerated on every update while a service kind is on,
// compared by name; pending states are waited out). What's there when it starts is the baseline, not events. Only
// the enabled kinds are recorded (the HistoryEvents setting). The frame calls Update once per refresh, after the
// snapshot and before the views (Recorders::OnRefresh); the view reads the lines it hasn't shown by sequence number.
// At most LineLimit lines are kept, the oldest dropped. UI thread only.
//
class HistoryRecorder {
public:
	// the kinds of events, each a bit of the HistoryEvents setting (and a menu command, ID_HISTORY_EVENT_FIRST + kind)
	enum class Event {
		ProcessStarted, ProcessExited, ServiceStarted, ServiceStopped, ServicePaused, ServiceInstalled,
		_Count
	};

	struct Line {
		int64_t Time;	// FILETIME, for the order within a refresh
		Event Kind;
		bool Warning;
		uint32_t Pid;	// 0: none
		std::wstring Text;
		PCWSTR Label;	// "Process started", "Service deleted"...
	};

	static constexpr size_t LineLimit = 100000;

	// takes the baseline from the snapshot as it is now
	HistoryRecorder(ProcessManager& pm, uint32_t events);

	// the snapshot's changes since the last update (and the services', if a service kind is on)
	void Update(ProcessManager& pm);

	[[nodiscard]] uint32_t GetEvents() const {
		return m_Events;
	}
	// a service kind turned on after they all were off starts from the services as they are now
	void SetEvents(uint32_t events);
	[[nodiscard]] bool IsEnabled(Event kind) const;

	// FILETIME: when it started recording (or was last cleared)
	[[nodiscard]] int64_t GetSince() const {
		return m_Since;
	}
	void Clear();

	// the lines kept, oldest first; the first one's sequence number is GetFirstSequence(), each next one's one more
	[[nodiscard]] std::deque<Line> const& GetLines() const {
		return m_Lines;
	}
	[[nodiscard]] uint64_t GetFirstSequence() const {
		return m_FirstSequence;
	}
	// one past the last line's sequence number
	[[nodiscard]] uint64_t GetEndSequence() const {
		return m_FirstSequence + m_Lines.size();
	}

	[[nodiscard]] static PCWSTR EventName(Event kind);
	// "2026-10-23 21:30:05.123  Process started    <text>"
	[[nodiscard]] static std::wstring Format(Line const& line);
	[[nodiscard]] static int64_t Now();

private:
	struct KnownService {
		WinSys::ServiceState State;
		uint32_t Pid;
		std::wstring DisplayName;
	};

	void TakeServices();
	void CollectProcesses(ProcessManager& pm, std::vector<Line>& lines);
	void CollectServices(std::vector<Line>& lines);

	uint32_t m_Events;	// the enabled kinds, 1 << Event
	int64_t m_Since;
	std::deque<Line> m_Lines;
	uint64_t m_FirstSequence{ 0 };
	// the processes there when it started: their first appearance as new isn't a start
	std::unordered_set<WinSys::ProcessOrThreadKey> m_BaselineProcesses;
	// each process seen, opened when it was, for its exit code once it's gone
	std::unordered_map<WinSys::ProcessOrThreadKey, std::unique_ptr<WinSys::Process>> m_Processes;
	std::unordered_map<std::wstring, KnownService> m_Services;	// by name
};
