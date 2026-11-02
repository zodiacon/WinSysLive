#include "pch.h"
#include "TestFramework.h"
#include "ProcessHistory.h"

// ProcessHistory: the rates between two counters (as the process sheet's Performance page computes them) and the ring
// of the last Capacity samples

namespace {
	bool Equal(float a, float b) {
		return std::fabs(a - b) < 0.001f * std::max(1.0f, std::fabs(b));
	}

	ProcessCounters At(int64_t time) {
		ProcessCounters c;
		c.Time = time;
		c.Private = 100 << 20;
		c.WorkingSet = 50 << 20;
		return c;
	}
}

TEST(ProcessHistory_FirstSampleHasNoRates) {
	auto s = ProcessHistory::Compute(At(1000), nullptr, 4, 1000);
	CHECK(!s.HasRates);
	CHECK(!s.HasNetwork);
	CHECK(Equal(s.Private, float(100 << 20)));
	CHECK(Equal(s.WorkingSet, float(50 << 20)));
}

TEST(ProcessHistory_Rates) {
	// one second (frequency 1000) on two processors
	auto prev = At(1000), now = At(2000);
	now.KernelTime = prev.KernelTime + 5'000'000;	// half a second
	now.UserTime = prev.UserTime + 10'000'000;	// a second: half of two processors
	now.Read = prev.Read + 4096;
	now.Write = prev.Write + 1024;
	now.Other = prev.Other + 10;
	auto s = ProcessHistory::Compute(now, &prev, 2, 1000);
	CHECK(s.HasRates);
	CHECK(Equal(s.KernelCpu, 25.0f));
	CHECK(Equal(s.UserCpu, 50.0f));
	CHECK(Equal(s.Read, 4096.0f));
	CHECK(Equal(s.Write, 1024.0f));
	CHECK(Equal(s.Other, 10.0f));
	CHECK(!s.HasNetwork);

	// half a second: the rates double
	now.Time = 1500;
	s = ProcessHistory::Compute(now, &prev, 2, 1000);
	CHECK(Equal(s.Read, 8192.0f));
	CHECK(Equal(s.KernelCpu, 50.0f));

	// the time didn't move: no rates
	now.Time = prev.Time;
	CHECK(!ProcessHistory::Compute(now, &prev, 2, 1000).HasRates);
}

TEST(ProcessHistory_Network) {
	auto prev = At(1000), now = At(2000);
	now.HasNetwork = true;
	now.Received = 3000;
	now.Sent = 500;
	// the tracker wasn't running at the previous sample
	CHECK(!ProcessHistory::Compute(now, &prev, 1, 1000).HasNetwork);
	prev.HasNetwork = true;
	prev.Received = 1000;
	prev.Sent = 100;
	auto s = ProcessHistory::Compute(now, &prev, 1, 1000);
	CHECK(s.HasNetwork);
	CHECK(Equal(s.Received, 2000.0f));
	CHECK(Equal(s.Sent, 400.0f));
	// a restarted tracker counts from zero: no negative rate
	now.Received = 10;
	s = ProcessHistory::Compute(now, &prev, 1, 1000);
	CHECK(Equal(s.Received, 0.0f));
}

TEST(ProcessHistory_Ring) {
	ProcessHistory history;
	CHECK(!history.GetLast().has_value());
	for (int i = 0; i < (int)ProcessHistory::Capacity + 10; i++) {
		auto c = At(1000 + i * 1000);
		c.Read = i * 100;
		history.Push(c, 1, 1000);
	}
	CHECK_EQ(history.GetSamples().size(), ProcessHistory::Capacity);
	CHECK(history.GetLast().has_value());
	CHECK_EQ(history.GetLast()->Read, int64_t((ProcessHistory::Capacity + 9) * 100));
	// the oldest dropped: every sample kept has rates (only the very first had none)
	CHECK(std::ranges::all_of(history.GetSamples(), [](auto& s) { return s.HasRates; }));
	CHECK(Equal(history.GetSamples().back().Read, 100.0f));
}
