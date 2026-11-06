#pragma once

#include "HistoryRecorder.h"

//
// The Timeline tab without its window (the tests take it): which processes' lifetimes (HistoryRecorder::ProcessLife) a
// time range shows and in what order, each one's parent's row, the time axis (pixels, tick steps) and zooming.
//
struct TimelineRow {
	HistoryRecorder::ProcessLife const* Life{ nullptr };	// nullptr: a group's heading (Header)
	int ParentRow{ -1 };	// the parent's row, if it's shown
	int Depth{ 0 };			// Grouping::Parent: how far below a shown ancestor (the name's indent)
	std::wstring Header;	// Grouping::User: the user and how many processes, above them
};

struct TimelineModel final {
	// the rows' order: by start time; as a tree (each process's children below it, by start time; Depth set); or by
	// user (a heading row before each user's processes, by start time; the users by name, one unknown last)
	enum class Grouping { None, Parent, User, _Count };

	struct Options {
		int64_t From{ 0 }, To{ 0 };	// FILETIME: the time range shown
		int64_t Now{ 0 };			// a running process's bar ends here
		std::wstring Filter;		// matches the name, PID, command line or user (any case); empty: all
		bool HideThroughout{ true };	// leave out what ran through the whole range (started before, still running after)
		Grouping Group{ Grouping::None };
	};

	// the lifetimes overlapping the range that the options keep, by start time (then PID); hidden: how many
	// HideThroughout left out
	[[nodiscard]] static std::vector<TimelineRow> Build(std::unordered_map<WinSys::ProcessOrThreadKey, HistoryRecorder::ProcessLife> const& lives,
		Options const& options, size_t* hidden = nullptr);
	// File > Export's table of the rows (not the headings): Name, PID, Parent PID, Parent, User, Started, Exited, Ran,
	// Exit Code, Command Line, Seen (before the recording, or between refreshes); the times local, to the millisecond
	[[nodiscard]] static std::vector<std::vector<std::wstring>> Table(std::vector<TimelineRow> const& rows,
		std::unordered_map<WinSys::ProcessOrThreadKey, HistoryRecorder::ProcessLife> const& lives, int64_t now);
	// "2026-11-06 14:25:44.123", local
	[[nodiscard]] static std::wstring LocalTime(int64_t time);
	// a lifetime's end: its exit, or now for one running
	[[nodiscard]] static int64_t EndOf(HistoryRecorder::ProcessLife const& life, int64_t now) {
		return life.End ? life.End : now;
	}

	// the time's x in [left, left + width) for the range [from, to] (outside it: past the edges)
	[[nodiscard]] static int X(int64_t time, int64_t from, int64_t to, int left, int width);
	// the time at x
	[[nodiscard]] static int64_t TimeAt(int x, int64_t from, int64_t to, int left, int width);
	// the axis's step (100 ns units) for ticks at least minPixels apart: 1, 2, 5, 10, 15 or 30 seconds, minutes and
	// so on up to days
	[[nodiscard]] static int64_t TickStep(int64_t span, int width, int minPixels);
	// the range zoomed by factor (below 1: in) around the anchor, which keeps its place; at least minSpan wide
	[[nodiscard]] static std::pair<int64_t, int64_t> Zoom(int64_t from, int64_t to, int64_t anchor, double factor, int64_t minSpan);
	// "0.4 s", "12 s", "3:05", "1:02:03", "2 d 3:04:05"
	[[nodiscard]] static std::wstring Duration(int64_t ticks);
	// the tooltip's lines: name and PID, parent, user, command line, start, exit (with the exit code) or running, how long
	[[nodiscard]] static std::wstring Describe(HistoryRecorder::ProcessLife const& life, std::wstring const& parentName, int64_t now);

	static constexpr int64_t Second = 10000000;
};
