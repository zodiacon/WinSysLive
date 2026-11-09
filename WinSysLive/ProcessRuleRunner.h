#pragma once

#include "ProcessInfoEx.h"
#include "ProcessRules.h"
#include <deque>

//
// Options > Process Rules...: the frame's rules, applied from the snapshots (CMainFrame::Refresh), so a process gets them
// within an update of starting (none while updates are paused, until they resume; a process that lives shorter than an
// update isn't seen), and to the running processes when the app starts and when a rule is added, changed or turned on.
// Once per process: what the process or the user changes afterwards stays. Each result (done or failed) is kept for the
// dialog; the last 500.
//
class CProcessRules {
public:
	struct Result {
		int64_t Time;	// FILETIME units
		std::wstring Text;
	};

	static std::vector<ProcessRule> Load();
	static void Save(std::vector<ProcessRule> const& rules);

	void SetRules(std::vector<ProcessRule> rules) {
		m_Rules = std::move(rules);
	}
	std::vector<ProcessRule> const& GetRules() const {
		return m_Rules;
	}

	// after every snapshot update: the new processes; the first time every process
	void Update(ProcessManager& pm);
	// a rule that was added, changed or turned on, on the processes running now (each rule alone: the other rules'
	// settings, or the user's since, aren't applied again)
	void ApplyToRunning(ProcessManager& pm, ProcessRule const& rule);

	std::deque<Result> const& GetRecent() const {
		return m_Recent;
	}
	void ClearRecent() {
		m_Recent.clear();
	}

private:
	void Apply(ProcessRule const& rule, ProcessInfoEx& process);

	static constexpr size_t MaxRecent = 500;

	std::vector<ProcessRule> m_Rules;
	std::deque<Result> m_Recent;
	bool m_First{ true };
};
