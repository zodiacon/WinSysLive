#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/TimelineModel.h"

// the Timeline tab's model (WinSysLive\TimelineModel.h): which lifetimes a range shows, their order and parents, the
// axis and zooming

namespace {
	using Lives = std::unordered_map<WinSys::ProcessOrThreadKey, HistoryRecorder::ProcessLife>;
	const int64_t S = TimelineModel::Second;
	const int64_t T0 = 0x01DD6D5A00000000;	// the recording's start

	void Add(Lives& lives, uint32_t pid, int64_t start, int64_t end, std::wstring name, uint32_t parentPid = 0, int64_t parentStart = 0) {
		HistoryRecorder::ProcessLife life;
		life.Key = { start, pid };
		life.Parent = { parentStart, parentPid };
		life.Name = std::move(name);
		life.Start = start;
		life.End = end;
		life.Baseline = start < T0;
		lives[life.Key] = life;
	}
}

TEST(Timeline_Build) {
	Lives lives;
	Add(lives, 4, T0 - 3600 * S, 0, L"System");						// throughout
	Add(lives, 100, T0 - 600 * S, 0, L"explorer.exe");				// throughout
	Add(lives, 200, T0 + 10 * S, T0 + 20 * S, L"setup.exe", 100, T0 - 600 * S);
	Add(lives, 300, T0 + 12 * S, 0, L"msiexec.exe", 200, T0 + 10 * S);
	Add(lives, 400, T0 - 60 * S, T0 + 5 * S, L"old.exe");			// started before, exited during
	Add(lives, 500, T0 + 500 * S, T0 + 501 * S, L"later.exe");		// after the range

	TimelineModel::Options options{ T0, T0 + 100 * S, T0 + 600 * S, L"", true };
	size_t hidden = 0;
	auto rows = TimelineModel::Build(lives, options, &hidden);
	CHECK_EQ(rows.size(), (size_t)3);
	CHECK_EQ(hidden, (size_t)2);
	// by start time
	CHECK_EQ(rows[0].Life->Name, std::wstring(L"old.exe"));
	CHECK_EQ(rows[1].Life->Name, std::wstring(L"setup.exe"));
	CHECK_EQ(rows[2].Life->Name, std::wstring(L"msiexec.exe"));
	// msiexec's parent is setup's row; setup's parent (explorer) isn't shown
	CHECK_EQ(rows[2].ParentRow, 1);
	CHECK_EQ(rows[1].ParentRow, -1);

	// everything that overlaps when nothing's hidden
	options.HideThroughout = false;
	rows = TimelineModel::Build(lives, options, &hidden);
	CHECK_EQ(rows.size(), (size_t)5);
	CHECK_EQ(hidden, (size_t)0);
	CHECK_EQ(rows[1].ParentRow, -1);	// explorer, shown now, has no recorded parent
	auto setup = std::ranges::find_if(rows, [](auto& r) { return r.Life->Key.Id == 200; });
	CHECK(setup != rows.end() && setup->ParentRow >= 0 && rows[setup->ParentRow].Life->Key.Id == 100);

	// a PID reused: the parent is matched by its creation time too
	Add(lives, 600, T0 + 30 * S, T0 + 31 * S, L"child.exe", 100, T0 - 1 * S);	// another process 100
	rows = TimelineModel::Build(lives, options);
	auto child = std::ranges::find_if(rows, [](auto& r) { return r.Life->Key.Id == 600; });
	CHECK(child != rows.end() && child->ParentRow == -1);

	// the filter: name, PID, any case
	options.Filter = L"SETUP";
	rows = TimelineModel::Build(lives, options);
	CHECK(rows.size() == 1 && rows[0].Life->Key.Id == 200);
	options.Filter = L"300";
	rows = TimelineModel::Build(lives, options);
	CHECK(rows.size() == 1 && rows[0].Life->Key.Id == 300);
}

TEST(Timeline_Axis) {
	CHECK_EQ(TimelineModel::X(T0, T0, T0 + 100 * S, 10, 1000), 10);
	CHECK_EQ(TimelineModel::X(T0 + 50 * S, T0, T0 + 100 * S, 10, 1000), 510);
	CHECK_EQ(TimelineModel::X(T0 + 100 * S, T0, T0 + 100 * S, 10, 1000), 1010);
	CHECK(TimelineModel::X(T0 - 100 * S, T0, T0 + 100 * S, 10, 1000) < 10);
	CHECK_EQ(TimelineModel::TimeAt(510, T0, T0 + 100 * S, 10, 1000), T0 + 50 * S);
	// ticks at least 80 pixels apart
	CHECK_EQ(TimelineModel::TickStep(100 * S, 1000, 80), 10 * S);
	CHECK_EQ(TimelineModel::TickStep(3600 * S, 1000, 80), 5 * 60 * S);
	CHECK_EQ(TimelineModel::TickStep(10 * S, 1000, 80), 1 * S);
	CHECK_EQ(TimelineModel::TickStep(30LL * 86400 * S, 100, 80), 7LL * 86400 * S);

	// zoom in by half around the middle: the middle stays
	auto [from, to] = TimelineModel::Zoom(T0, T0 + 100 * S, T0 + 50 * S, 0.5, S);
	CHECK_EQ(from, T0 + 25 * S);
	CHECK_EQ(to, T0 + 75 * S);
	// around the left edge: the edge stays
	std::tie(from, to) = TimelineModel::Zoom(T0, T0 + 100 * S, T0, 0.5, S);
	CHECK_EQ(from, T0);
	CHECK_EQ(to, T0 + 50 * S);
	// not below the minimum
	std::tie(from, to) = TimelineModel::Zoom(T0, T0 + 2 * S, T0 + S, 0.1, S);
	CHECK_EQ(to - from, S);

	CHECK_EQ(TimelineModel::Duration(4 * S + S / 10 * 3), std::wstring(L"4.3 s"));
	CHECK_EQ(TimelineModel::Duration(42 * S), std::wstring(L"42 s"));
	CHECK_EQ(TimelineModel::Duration(185 * S), std::wstring(L"3:05"));
	CHECK_EQ(TimelineModel::Duration(3723 * S), std::wstring(L"1:02:03"));
	CHECK_EQ(TimelineModel::Duration((2 * 86400 + 3723) * S), std::wstring(L"2 d 1:02:03"));
}

TEST(Timeline_Describe) {
	HistoryRecorder::ProcessLife life;
	life.Key = { T0 + 10 * S, 200 };
	life.Parent = { T0 - 600 * S, 100 };
	life.Name = L"setup.exe";
	life.CommandLine = L"setup.exe /quiet";
	life.Start = T0 + 10 * S;
	life.End = T0 + 20 * S;
	life.ExitCode = 1603;
	auto text = TimelineModel::Describe(life, L"explorer.exe", T0 + 30 * S);
	CHECK(text.starts_with(L"setup.exe (200)\nParent: explorer.exe (100)\nsetup.exe /quiet\nStarted "));
	CHECK(text.find(L"exit code 1603 (0x643), ran 10 s") != std::wstring::npos);
	life.End = 0;
	text = TimelineModel::Describe(life, L"", T0 + 30 * S);
	CHECK(text.find(L"Parent: ? (100)") != std::wstring::npos);
	CHECK(text.ends_with(L"Running for 20 s"));
}
