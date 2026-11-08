#pragma once

#include "LeakWatch.h"
#include "ProcessInfoEx.h"
#include <WinSys/Keys.h>
#include <unordered_map>

//
// Options > Leak Watch (the LeakWatch setting, off by default): the frame samples every process every 10 seconds
// while it's on (CMainFrame::Refresh): its handles and private bytes from the snapshot, its GDI and USER objects
// (GetGuiResources, through the handle ProcessInfoEx keeps; unknown for the processes it can't open), and judges each
// process's last half hour (LeakSeries, LeakDetector) after each sample. The Processes tab's Leaks column and the
// Alerts' ProcessLeaks rules read the findings. Turning it off drops everything.
//
class LeakWatcher {
public:
	static constexpr uint64_t Interval = 10000;	// ms

	// after every snapshot update: a sample of every process when Interval has passed since the last; the exited
	// processes dropped
	void Update(ProcessManager& pm);
	void Clear();

	// the process's suspected leaks (empty: none, or not judged yet)
	[[nodiscard]] std::vector<LeakFinding> const& GetFindings(WinSys::ProcessOrThreadKey const& key) const;
	// samples taken since Leak Watch came on (none: 0)
	[[nodiscard]] size_t GetSampleCount() const {
		return m_Samples;
	}

private:
	struct Entry {
		LeakSeries Series;
		std::vector<LeakFinding> Findings;
	};
	std::unordered_map<WinSys::ProcessOrThreadKey, Entry> m_Processes;
	uint64_t m_Last{ 0 };
	size_t m_Samples{ 0 };
};
