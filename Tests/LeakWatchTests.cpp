#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/LeakWatch.h"
#include "../WinSysLive/AlertRules.h"

// Leak Watch's detection (LeakWatch.h) on series made up for each case: a steady leak, too short or too few samples,
// too little growth, a garbage collector's sawtooth, a step and a plateau, a leak that fell back; the series' window;
// the texts; and the alert kind's rule text

namespace {
	const uint64_t Second = 1000, Minute = 60 * Second;

	// a sample every 10 seconds for the minutes, the value from the function of the sample's index and count
	template<typename F>
	std::vector<LeakPoint> Series(uint64_t minutes, F value) {
		std::vector<LeakPoint> points;
		size_t count = (size_t)(minutes * 6) + 1;
		for (size_t i = 0; i < count; i++)
			points.push_back({ 1000000 + i * 10 * Second, value(i, count) });
		return points;
	}

	// a steady rise from start to end, with a small wobble (+/- 5) so it isn't a perfect line
	std::vector<LeakPoint> Rise(uint64_t minutes, int64_t start, int64_t end) {
		return Series(minutes, [&](size_t i, size_t count) {
			return start + (end - start) * (int64_t)i / (int64_t)(count - 1) + (i % 3 == 1 ? 5 : i % 3 == 2 ? -5 : 0);
			});
	}
}

TEST(LeakWatch_SteadyLeak) {
	auto points = Rise(20, 1000, 3000);
	auto f = LeakDetector::Analyze(LeakCounter::Handles, points);
	CHECK(f.has_value());
	if (!f)
		return;
	CHECK(f->Counter == LeakCounter::Handles);
	CHECK_EQ(f->From, 1000ll);
	CHECK_EQ(f->To, 3000ll);
	CHECK_EQ(f->Span, 20 * Minute);
	CHECK(LeakDetector::Describe(*f) == L"handles 1,000 to 3,000 in 20 minutes");

	// private bytes: 200 MB to 500 MB
	auto memory = Rise(15, 200ll << 20, 500ll << 20);
	auto m = LeakDetector::Analyze(LeakCounter::PrivateBytes, memory);
	CHECK(m.has_value());
	if (m)
		CHECK(LeakDetector::Describe(*m) == L"private bytes 200.0 MB to 500.0 MB in 15 minutes");
	CHECK(LeakDetector::Describe(std::vector<LeakFinding>{ *f, LeakFinding{ LeakCounter::GdiObjects, 400, 1500, 11 * Minute } })
		== L"handles 1,000 to 3,000 in 20 minutes; GDI objects 400 to 1,500 in 11 minutes");
	CHECK(LeakDetector::Describe(std::vector<LeakFinding>{}).empty());
}

TEST(LeakWatch_NotYet) {
	// 9 minutes: too short to judge
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, Rise(9, 1000, 3000)).has_value());
	// 15 minutes but 15 samples: too few
	std::vector<LeakPoint> sparse;
	for (int i = 0; i < 15; i++)
		sparse.push_back({ i * Minute + Minute, 1000 + i * 200 });
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, sparse).has_value());
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, {}).has_value());
}

TEST(LeakWatch_TooLittle) {
	// 400 handles: under the 500
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, Rise(20, 1000, 1400)).has_value());
	// 600 more on 10,000: under a quarter
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, Rise(20, 10000, 10600)).has_value());
	// 300 GDI objects from 100: enough for GDI, not for handles
	CHECK(LeakDetector::Analyze(LeakCounter::GdiObjects, Rise(20, 100, 400)).has_value());
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, Rise(20, 100, 400)).has_value());
	// shrinking
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, Rise(20, 3000, 1000)).has_value());
}

TEST(LeakWatch_NotLeaks) {
	// a garbage collector's sawtooth: up 550 in each 2 minutes, then back down, climbing a little
	auto sawtooth = Series(20, [](size_t i, size_t) { return (int64_t)(1000 + (i % 12) * 50 + i * 6); });	// net +720: enough, but it falls back by 550
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, sawtooth).has_value());
	// grew at startup, then stayed: a step in the first minutes and a plateau
	auto step = Series(20, [](size_t i, size_t) { return (int64_t)(i < 12 ? 1000 + i * 150 : 2800); });
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, step).has_value());
	// grew for 12 minutes, then stopped for the last 8: a straight line still fits (R squared 0.88), but the second half
	// grew too little (a sixth): no longer leaking
	auto stopped = Series(20, [](size_t i, size_t) { return (int64_t)(i < 72 ? 1000 + i * 2000 / 72 : 3000); });
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, stopped).has_value());
	// a plateau, then a late rise: the first half didn't grow
	auto late = Series(20, [](size_t i, size_t count) { return (int64_t)(i < count / 2 ? 1000 : 1000 + (i - count / 2) * 40); });
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, late).has_value());
	// a leak that fell back: up 2,000, then down 500 at the end
	auto fell = Rise(20, 1000, 3000);
	fell.back().Value = 2500;
	CHECK(!LeakDetector::Analyze(LeakCounter::Handles, fell).has_value());
	// but a dip of under a tenth of the growth is fine
	auto dip = Rise(20, 1000, 3000);
	dip[dip.size() / 2].Value -= 150;
	CHECK(LeakDetector::Analyze(LeakCounter::Handles, dip).has_value());
}

TEST(LeakWatch_Series) {
	LeakSeries series;
	// 40 minutes of samples: handles rising, private bytes flat, GDI unknown, USER objects rising slowly
	for (int i = 0; i <= 240; i++)
		series.Add(i * 10 * Second, { 1000 + i * 10, 50ll << 20, -1, 100 + i });
	auto& handles = series.GetPoints(LeakCounter::Handles);
	CHECK(handles.back().Time - handles.front().Time <= LeakDetector::Window);
	CHECK_EQ(handles.front().Time, 10 * Minute);	// the first 10 minutes dropped
	CHECK(series.GetPoints(LeakCounter::GdiObjects).empty());
	auto findings = series.Analyze();
	CHECK_EQ(findings.size(), 1u);	// USER objects grew 180 in the window: under 300
	if (!findings.empty()) {
		CHECK(findings[0].Counter == LeakCounter::Handles);
		CHECK_EQ(findings[0].From, 1600ll);
		CHECK_EQ(findings[0].To, 3400ll);
	}
}

TEST(LeakWatch_AlertRule) {
	auto rules = AlertRule::ParseRules(L"ProcessLeaks|1|0|0|chrome.exe;msedge.exe");
	CHECK_EQ(rules.size(), 1u);
	if (rules.empty())
		return;
	CHECK(rules[0].Kind == AlertKind::ProcessLeaks);
	CHECK(rules[0].Describe() == L"chrome.exe;msedge.exe leaks (handles, memory, GDI or USER objects)");
	CHECK(AlertRule::HasPattern(AlertKind::ProcessLeaks));
	CHECK(!AlertRule::HasThreshold(AlertKind::ProcessLeaks));
	CHECK(AlertRule::FormatRules(rules) == L"ProcessLeaks|1|0|0|chrome.exe;msedge.exe");
	CHECK(std::wstring(AlertRule::KindName(AlertKind::ProcessLeaks)).find(L"Leak Watch") != std::wstring::npos);
}
