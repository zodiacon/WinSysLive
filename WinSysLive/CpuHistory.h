#pragma once

#include <GraphControl/include/CellGraphs.h>
#include <optional>

//
// The threads' CPU History columns (the Threads tab and the Threads pages): a sparkline of the CPU at each refresh, as
// the Processes tab's CPU History. A series exists only while the column is shown (the Threads tab has thousands of
// threads), and a refresh that doesn't know the CPU (the first one) leaves a gap rather than a zero.
//
// A thread's samples are percents of one processor (the most a thread can use), so a thread that's always running is at
// 100% (as a percent of all the processors it was 3% with 32 of them, a flat line); a process row's (the Threads tab's
// Group by Process) are its threads' total as a percent of all the processors, as the Processes tab's CPU History. The
// CPU column's text stays a percent of all the processors for both.
//
namespace CpuHistory {
	enum class Scale {
		Processor,	// a thread's: 100% is one processor busy all the time
		Machine,	// a process's: 100% is every processor busy
	};

	// the sample for a CPU value: cpu in ProcessInfo::CPU units (1/10000 of a percent of all the processors), -1 when
	// not known (a gap)
	inline float Sample(int32_t cpu, Scale scale) {
		static const int processors = (std::max)(1, (int)::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
		if (cpu < 0)
			return GraphCtrl::MissingSample;
		double percent = cpu / 10000.0;
		if (scale == Scale::Processor)
			percent *= processors;
		return (float)std::clamp(percent, 0.0, 100.0);
	}

	// shown: whether the column is (the series goes when it isn't)
	inline void Update(std::optional<GraphCtrl::Series>& history, int32_t cpu, bool shown, Scale scale) {
		if (!shown) {
			history.reset();
			return;
		}
		if (!history)
			history.emplace();
		history->Push(Sample(cpu, scale));
	}

	// the column's sparkline, in the Processes tab's CPU History color, 0 to 100 for either scale; the tooltip says the
	// row's own percents
	inline void SetColumn(GraphCtrl::CellGraphs& graphs, int column) {
		GraphCtrl::SparklineStyle style;
		style.LineColor = RGB(0, 170, 80);
		style.Min = 0;
		style.Max = 100;
		graphs.SetSparkline(column, style);
		graphs.SetFormat(column, GraphCtrl::Format::Percent);
	}

	// the cell's samples, if there are any to draw
	inline bool GetData(std::optional<GraphCtrl::Series> const& history, GraphCtrl::CellData& data) {
		if (!history || history->ValidCount() == 0)
			return false;
		data.Samples = &*history;
		return true;
	}
}
