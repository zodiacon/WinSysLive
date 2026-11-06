#include "pch.h"
#include "TimelineModel.h"

namespace {
	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	bool Contains(std::wstring const& text, std::wstring const& lowerFilter) {
		return Lowercase(text).find(lowerFilter) != std::wstring::npos;
	}

	std::wstring LocalTime(int64_t time) {
		FILETIME local;
		SYSTEMTIME st{};
		if (!::FileTimeToLocalFileTime((FILETIME const*)&time, &local) || !::FileTimeToSystemTime(&local, &st))
			return L"";
		return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	}
}

std::vector<TimelineRow> TimelineModel::Build(std::unordered_map<WinSys::ProcessOrThreadKey, HistoryRecorder::ProcessLife> const& lives,
	Options const& options, size_t* hidden) {
	auto filter = Lowercase(options.Filter);
	std::vector<TimelineRow> rows;
	size_t throughout = 0;
	for (auto& [key, life] : lives) {
		auto end = EndOf(life, options.Now);
		if (life.Start > options.To || end < options.From)
			continue;
		if (!filter.empty() && !Contains(life.Name, filter) && !Contains(std::to_wstring(life.Key.Id), filter)
			&& !Contains(life.CommandLine, filter) && !Contains(life.User, filter))
			continue;
		if (options.HideThroughout && life.Start <= options.From && end >= options.To) {
			throughout++;
			continue;
		}
		rows.push_back({ &life });
	}
	std::ranges::sort(rows, [](auto& a, auto& b) {
		return a.Life->Start != b.Life->Start ? a.Life->Start < b.Life->Start : a.Life->Key.Id < b.Life->Key.Id;
		});
	std::unordered_map<WinSys::ProcessOrThreadKey, int> index;
	for (int i = 0; i < (int)rows.size(); i++)
		index[rows[i].Life->Key] = i;
	for (auto& row : rows)
		if (row.Life->Parent.Created)
			if (auto it = index.find(row.Life->Parent); it != index.end())
				row.ParentRow = it->second;
	if (hidden)
		*hidden = throughout;
	return rows;
}

int TimelineModel::X(int64_t time, int64_t from, int64_t to, int left, int width) {
	if (to <= from)
		return left;
	auto x = left + (double)(time - from) * width / (double)(to - from);
	// far outside: kept within what GDI draws
	return (int)std::clamp(x, -100000.0, 100000.0);
}

int64_t TimelineModel::TimeAt(int x, int64_t from, int64_t to, int left, int width) {
	if (width <= 0)
		return from;
	return from + (int64_t)((double)(x - left) * (double)(to - from) / width);
}

int64_t TimelineModel::TickStep(int64_t span, int width, int minPixels) {
	static const int64_t steps[] = {
		1, 2, 5, 10, 15, 30,							// seconds
		60, 2 * 60, 5 * 60, 10 * 60, 15 * 60, 30 * 60,	// minutes
		3600, 2 * 3600, 3 * 3600, 6 * 3600, 12 * 3600,	// hours
		86400, 2 * 86400, 7 * 86400,					// days
	};
	if (width <= 0 || span <= 0)
		return steps[0] * Second;
	for (auto s : steps) {
		auto step = s * Second;
		if ((double)step * width / (double)span >= minPixels)
			return step;
	}
	return steps[_countof(steps) - 1] * Second;
}

std::pair<int64_t, int64_t> TimelineModel::Zoom(int64_t from, int64_t to, int64_t anchor, double factor, int64_t minSpan) {
	auto span = (double)(to - from) * factor;
	if (span < (double)minSpan)
		span = (double)minSpan;
	// the anchor stays at the same fraction of the range
	double fraction = to > from ? (double)(anchor - from) / (double)(to - from) : 0.5;
	auto newFrom = anchor - (int64_t)(span * fraction);
	return { newFrom, newFrom + (int64_t)span };
}

std::wstring TimelineModel::Duration(int64_t ticks) {
	if (ticks < 0)
		ticks = 0;
	if (ticks < 10 * Second)
		return std::format(L"{:.1f} s", ticks / (double)Second);
	auto seconds = ticks / Second;
	if (seconds < 60)
		return std::format(L"{} s", seconds);
	if (seconds < 3600)
		return std::format(L"{}:{:02}", seconds / 60, seconds % 60);
	if (seconds < 86400)
		return std::format(L"{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
	return std::format(L"{} d {}:{:02}:{:02}", seconds / 86400, seconds / 3600 % 24, seconds / 60 % 60, seconds % 60);
}

std::wstring TimelineModel::Describe(HistoryRecorder::ProcessLife const& life, std::wstring const& parentName, int64_t now) {
	auto text = std::format(L"{} ({})", life.Name, life.Key.Id);
	text += L"\nParent: " + (parentName.empty() ? std::wstring(L"?") : parentName) + std::format(L" ({})", life.Parent.Id);
	if (!life.User.empty())
		text += L"\nUser: " + life.User;
	if (!life.CommandLine.empty())
		text += L"\n" + life.CommandLine;
	text += L"\nStarted " + LocalTime(life.Start) + (life.Baseline ? L" (before recording started)" : L"");
	if (life.Traced)
		text += L"\nBetween two refreshes: seen only by the process creation trace";
	if (life.End) {
		text += L"\nExited " + LocalTime(life.End);
		if (life.ExitCode)
			text += std::format(L", exit code {} (0x{:X})", (int32_t)*life.ExitCode, *life.ExitCode);
		text += L", ran " + Duration(life.End - life.Start);
	}
	else
		text += L"\nRunning for " + Duration(now - life.Start);
	return text;
}
