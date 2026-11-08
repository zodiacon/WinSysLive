#pragma once

#include <array>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

//
// Leak Watch's detection, without the app around it so the tests can take it (Tests\LeakWatchTests.cpp): a process's
// counters sampled over the last half hour (LeakSeries), and a counter's samples judged (LeakDetector::Analyze). A leak
// is suspected when a counter has grown for at least 10 minutes, by enough (an absolute amount and a quarter of where it
// started), steadily (a straight line fits it, both halves of the span grew, and it never fell back by more than a tenth
// of the growth): a program that allocates and frees (a garbage collector's sawtooth), grows once and stays (a cache
// filled at startup), or wobbles doesn't qualify. LeakWatcher (LeakWatcher.h) feeds it from the snapshots.
//
enum class LeakCounter { Handles, PrivateBytes, GdiObjects, UserObjects, _Count };

struct LeakPoint {
	uint64_t Time;	// ms (a tick count)
	int64_t Value;
};

struct LeakFinding {
	LeakCounter Counter{ LeakCounter::Handles };
	int64_t From{ 0 }, To{ 0 };	// the value at the span's start and now
	uint64_t Span{ 0 };			// ms
};

struct LeakDetector final {
	static constexpr uint64_t Window = 30 * 60 * 1000;		// the samples kept
	static constexpr uint64_t MinSpan = 10 * 60 * 1000;	// the samples needed before anything is judged
	static constexpr size_t MinPoints = 20;

	// the counter's leak, if its samples (in time order) show one
	static std::optional<LeakFinding> Analyze(LeakCounter counter, std::span<LeakPoint const> points);
	// the growth a leak must reach at least: 500 handles, 100 MB, 300 GDI or USER objects
	static int64_t MinGrowth(LeakCounter counter);

	static wchar_t const* CounterName(LeakCounter counter);	// "handles", "private bytes", "GDI objects", "USER objects"
	// "handles 1,200 to 3,450 in 15 minutes" (bytes as sizes)
	static std::wstring Describe(LeakFinding const& finding);
	// the findings described, joined with "; "
	static std::wstring Describe(std::vector<LeakFinding> const& findings);
};

// one process's counters over the last Window
class LeakSeries {
public:
	static constexpr int Count = (int)LeakCounter::_Count;

	// a sample of each counter (a negative value: unknown, not kept); the samples older than Window dropped
	void Add(uint64_t time, std::array<int64_t, Count> const& values);
	// each counter's leak, in LeakCounter's order
	[[nodiscard]] std::vector<LeakFinding> Analyze() const;
	[[nodiscard]] std::deque<LeakPoint> const& GetPoints(LeakCounter counter) const {
		return m_Points[(int)counter];
	}

private:
	std::array<std::deque<LeakPoint>, Count> m_Points;
};
