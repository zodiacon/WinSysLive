#include "pch.h"
#include "LeakWatch.h"

namespace {
	// 1234567 as "1,234,567" (fixed separators: the alert texts and the tests don't follow the locale)
	std::wstring Grouped(int64_t value) {
		auto text = std::to_wstring(value < 0 ? -value : value);
		for (int i = (int)text.size() - 3; i > 0; i -= 3)
			text.insert(i, 1, L',');
		return value < 0 ? L"-" + text : text;
	}

	// bytes in MB below a GB, else GB, one decimal
	std::wstring Size(int64_t bytes) {
		return bytes < (1LL << 30) ? std::format(L"{:.1f} MB", bytes / 1048576.0) : std::format(L"{:.1f} GB", bytes / 1073741824.0);
	}
}

std::optional<LeakFinding> LeakDetector::Analyze(LeakCounter counter, std::span<LeakPoint const> points) {
	if (points.size() < MinPoints)
		return std::nullopt;
	auto span = points.back().Time - points.front().Time;
	if (span < MinSpan)
		return std::nullopt;
	auto first = points.front().Value, last = points.back().Value;
	auto growth = last - first;
	// enough, absolutely and relatively
	if (growth < MinGrowth(counter) || growth * 4 < first)
		return std::nullopt;

	// never fell back by more than a tenth of the growth (a sawtooth, a drop after a peak)
	int64_t high = first;
	for (auto& p : points) {
		high = std::max(high, p.Value);
		if ((high - p.Value) * 10 > growth)
			return std::nullopt;
	}

	// both halves of the span grew, a quarter of the growth each at least (not a step and a plateau)
	auto middle = points.front().Time + span / 2;
	auto it = std::ranges::find_if(points, [&](auto& p) { return p.Time >= middle; });
	auto half = it->Value;
	if ((half - first) * 4 < growth || (last - half) * 4 < growth)
		return std::nullopt;

	// a straight line fits (R squared at least 0.8), with the time from the first sample in seconds
	double n = (double)points.size(), sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
	for (auto& p : points) {
		double x = (p.Time - points.front().Time) / 1000.0, y = (double)(p.Value - first);
		sx += x;
		sy += y;
		sxx += x * x;
		sxy += x * y;
		syy += y * y;
	}
	double vx = n * sxx - sx * sx, vy = n * syy - sy * sy;
	if (vx <= 0 || vy <= 0)
		return std::nullopt;
	double r = (n * sxy - sx * sy) / std::sqrt(vx * vy);
	if (r <= 0 || r * r < 0.8)
		return std::nullopt;
	return LeakFinding{ counter, first, last, span };
}

int64_t LeakDetector::MinGrowth(LeakCounter counter) {
	switch (counter) {
		case LeakCounter::Handles: return 500;
		case LeakCounter::PrivateBytes: return 100LL << 20;
		case LeakCounter::GdiObjects:
		case LeakCounter::UserObjects: return 300;
	}
	return INT64_MAX;
}

wchar_t const* LeakDetector::CounterName(LeakCounter counter) {
	switch (counter) {
		case LeakCounter::Handles: return L"handles";
		case LeakCounter::PrivateBytes: return L"private bytes";
		case LeakCounter::GdiObjects: return L"GDI objects";
		case LeakCounter::UserObjects: return L"USER objects";
	}
	return L"";
}

std::wstring LeakDetector::Describe(LeakFinding const& f) {
	auto value = [&](int64_t v) {
		return f.Counter == LeakCounter::PrivateBytes ? Size(v) : Grouped(v);
	};
	auto minutes = (f.Span + 30000) / 60000;
	return std::format(L"{} {} to {} in {} minute{}", CounterName(f.Counter), value(f.From), value(f.To), minutes, minutes == 1 ? L"" : L"s");
}

std::wstring LeakDetector::Describe(std::vector<LeakFinding> const& findings) {
	std::wstring text;
	for (auto& f : findings)
		text += (text.empty() ? L"" : L"; ") + Describe(f);
	return text;
}

void LeakSeries::Add(uint64_t time, std::array<int64_t, Count> const& values) {
	for (int i = 0; i < Count; i++) {
		auto& points = m_Points[i];
		if (values[i] >= 0)
			points.push_back({ time, values[i] });
		while (!points.empty() && time - points.front().Time > LeakDetector::Window)
			points.pop_front();
	}
}

std::vector<LeakFinding> LeakSeries::Analyze() const {
	std::vector<LeakFinding> findings;
	for (int i = 0; i < Count; i++) {
		// a deque isn't contiguous: a copy for the span
		std::vector<LeakPoint> points(m_Points[i].begin(), m_Points[i].end());
		if (auto f = LeakDetector::Analyze((LeakCounter)i, points))
			findings.push_back(*f);
	}
	return findings;
}
