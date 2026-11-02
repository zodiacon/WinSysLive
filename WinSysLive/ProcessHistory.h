#pragma once

#include <deque>
#include <optional>

//
// A process's recent performance, as its property sheet's Performance page graphs it, kept for every process from the
// frame's snapshots (one sample per refresh, the last Capacity), so the page opens with history instead of empty
// graphs. The rates come from the cumulative counters' differences, as the page computes them; network bytes only
// while the per-process network tracker runs (it isn't started for this).
//
struct ProcessCounters {
	int64_t Time{ 0 };		// QPC
	int64_t KernelTime{ 0 }, UserTime{ 0 };	// 100 ns units
	int64_t Read{ 0 }, Write{ 0 }, Other{ 0 };	// transfer counts, bytes
	int64_t Private{ 0 }, WorkingSet{ 0 };	// bytes, now
	bool HasNetwork{ false };
	uint64_t Received{ 0 }, Sent{ 0 };	// since the tracker started
};

struct ProcessHistorySample {
	bool HasRates{ false };		// not for the first sample
	float KernelCpu{ 0 }, UserCpu{ 0 };	// percent of all processors
	float Private{ 0 }, WorkingSet{ 0 };	// bytes
	float Read{ 0 }, Write{ 0 }, Other{ 0 };	// bytes per second
	bool HasNetwork{ false };	// the network rates are valid (the tracker ran for both samples)
	float Received{ 0 }, Sent{ 0 };		// bytes per second
};

class ProcessHistory {
public:
	// the Performance page's graphs keep as many (PerfHistoryLength)
	static constexpr size_t Capacity = 120;

	// a new sample from the counters, its rates from the previous ones'; the oldest dropped beyond Capacity
	void Push(ProcessCounters const& now, int processors, int64_t frequency);

	[[nodiscard]] std::deque<ProcessHistorySample> const& GetSamples() const {
		return m_Samples;
	}
	// the counters of the last sample, for the page to go on from
	[[nodiscard]] std::optional<ProcessCounters> const& GetLast() const {
		return m_Last;
	}

	// the rates between two counters (HasRates false if the time didn't move)
	[[nodiscard]] static ProcessHistorySample Compute(ProcessCounters const& now, ProcessCounters const* prev, int processors, int64_t frequency);

private:
	std::deque<ProcessHistorySample> m_Samples;
	std::optional<ProcessCounters> m_Last;
};
