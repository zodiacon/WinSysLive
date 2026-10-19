#include "pch.h"
#include "PerfPages.h"
#include "FormatHelper.h"

using namespace GraphCtrl;

namespace {
	std::wstring FormatBytes(int64_t bytes) {
		wchar_t text[32];
		Format::Bytes((float)bytes, text, _countof(text));
		return text;
	}

	std::wstring FormatUptime(uint64_t uptime) {
		return std::format(L"{}:{:02}:{:02}:{:02}", uptime / 86400, uptime / 3600 % 24, uptime / 60 % 60, uptime % 60);
	}

	std::wstring CpuLine(PerfSample const& s) {
		return std::format(L"CPU: {:.1f}% (kernel {:.1f}%)    Processes: {}    Threads: {}    Handles: {}    Up time: {}",
			s.CpuPercent(), s.KernelCpu, FormatHelper::FormatNumber(s.Processes), FormatHelper::FormatNumber(s.Threads),
			FormatHelper::FormatNumber(s.Handles), FormatUptime(s.Uptime));
	}

	std::wstring MemoryLine(PerfSample const& s) {
		return std::format(L"Memory: {} of {} ({:.0f}%)    Commit: {} of {}    Cached: {}    Paged pool: {}    Non-paged pool: {}",
			FormatBytes(s.InUse), FormatBytes(s.PhysicalTotal), s.MemoryPercent(), FormatBytes(s.Committed), FormatBytes(s.CommitLimit),
			FormatBytes(s.Cached), FormatBytes(s.PagedPool), FormatBytes(s.NonPagedPool));
	}
}

void CCpuGraph::Create(HWND hParent) {
	CGraphControl::Create(hParent, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP | GCS_STACKED | GCS_VALUEOVERLAY);
	// stacked bottom up: kernel time, then user time
	AddSeries(L"Kernel", SeriesStyle{ PerfColors::Kernel, PerfColors::Kernel });
	AddSeries(L"User", SeriesStyle{ PerfColors::User, PerfColors::User });
	SetTitle(L"CPU Usage");
	SetRange(0, 100);
	SetValueFormatter(Format::Percent);
	SetHistoryLength(PerfHistoryLength);
}

void CCpuGraph::Push(PerfSample const& sample) {
	if (!sample.HasCpu)
		return;
	float values[] = { sample.KernelCpu, sample.UserCpu };	// in AddSeries order
	PushSamples(values, _countof(values));
}

void CMemoryGraph::Create(HWND hParent, int64_t physicalTotal) {
	CGraphControl::Create(hParent, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_TOOLTIP | GCS_VALUEOVERLAY);
	m_InUse = AddSeries(L"In use", SeriesStyle{ PerfColors::Memory, PerfColors::Memory });
	SetTitle(L"Physical Memory In Use");
	SetRange(0, (float)physicalTotal);
	SetValueFormatter(Format::Bytes);
	SetHistoryLength(PerfHistoryLength);
}

void CMemoryGraph::Push(PerfSample const& sample) {
	PushSample(m_InUse, sample.HasMemory ? (float)sample.InUse : MissingSample);
}

//
// Summary
//

void CPerfSummaryPage::CreateGraphs() {
	m_Cpu.Create(m_hWnd);
	m_Memory.Create(m_hWnd, m_PhysicalTotal);
	m_Lines.resize(2);
}

void CPerfSummaryPage::LayoutGraphs(CRect const& rc, int gap) {
	int height = std::max(0, rc.Height() - gap);
	int cpuHeight = height / 2;
	m_Cpu.MoveWindow(rc.left, rc.top, rc.Width(), cpuHeight);
	m_Memory.MoveWindow(rc.left, rc.top + cpuHeight + gap, rc.Width(), height - cpuHeight);
}

void CPerfSummaryPage::Sample(PerfSample const& sample) {
	m_Cpu.Push(sample);
	m_Memory.Push(sample);
	if (sample.HasCpu)
		m_Lines[0] = CpuLine(sample);
	if (sample.HasMemory)
		m_Lines[1] = MemoryLine(sample);
}

void CPerfSummaryPage::ApplyTheme() {
	m_Cpu.SetTheme(m_Theme);
	m_Memory.SetTheme(m_Theme);
}

void CPerfSummaryPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	for (CGraphControl* graph : { (CGraphControl*)&m_Cpu, (CGraphControl*)&m_Memory }) {
		graph->SetTimeSpanText(text);
		graph->SetExpectedInterval(interval);
	}
}

//
// CPU
//

void CPerfCpuPage::CreateGraphs() {
	m_Cpu.Create(m_hWnd);
	m_Cores.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, GCS_GRID | GCS_FILL | GCS_TOOLTIP);
	m_Cores.SetGraphCount(m_Processors);
	m_Cores.SetTitlePrefix(L"CPU ");
	m_Cores.SetRange(0, 100);
	m_Cores.SetValueFormatter(Format::Percent);
	m_Cores.SetHistoryLength(PerfHistoryLength);
	m_Cores.SetSharedCrosshair(true);
	m_Lines.resize(2);
}

void CPerfCpuPage::LayoutGraphs(CRect const& rc, int gap) {
	int height = std::max(0, rc.Height() - gap);
	int cpuHeight = height * 45 / 100, coresHeight = height - cpuHeight;
	m_Cpu.MoveWindow(rc.left, rc.top, rc.Width(), cpuHeight);
	m_Cores.MoveWindow(rc.left, rc.top + cpuHeight + gap, rc.Width(), coresHeight);
	// the grid's automatic layout arranges the tiles in a square, which leaves them far too short in a wide strip;
	// aim for square tiles instead (each tile's title takes part of its height, so wider tiles get short plots)
	if (auto count = (int)m_Cores.GetGraphCount(); count > 0 && coresHeight > 0) {
		int columns = std::clamp((int)std::lround(std::sqrt((double)count * rc.Width() / coresHeight)), 1, count);
		m_Cores.SetLayout(columns, (count + columns - 1) / columns);
	}
}

void CPerfCpuPage::Sample(PerfSample const& sample) {
	if (!sample.HasCpu)
		return;
	m_Cpu.Push(sample);
	if (sample.Cores.size() == m_Cores.GetGraphCount())
		m_Cores.PushSamples(sample.Cores);

	m_Lines[0] = std::format(L"Utilization: {:.1f}%    Kernel: {:.1f}%    User: {:.1f}%    Processes: {}    Threads: {}    Handles: {}",
		sample.CpuPercent(), sample.KernelCpu, sample.UserCpu, FormatHelper::FormatNumber(sample.Processes),
		FormatHelper::FormatNumber(sample.Threads), FormatHelper::FormatNumber(sample.Handles));
	m_Lines[1] = std::format(L"Logical processors: {}    Processor groups: {}    Up time: {}",
		sample.Cores.size(), ::GetActiveProcessorGroupCount(), FormatUptime(sample.Uptime));
}

void CPerfCpuPage::ApplyTheme() {
	m_Cpu.SetTheme(m_Theme);
	m_Cores.SetTheme(m_Theme);
}

void CPerfCpuPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	m_Cpu.SetTimeSpanText(text);
	m_Cpu.SetExpectedInterval(interval);
	m_Cores.SetTimeSpanText(text);
}

//
// Memory
//

void CPerfMemoryPage::CreateGraphs() {
	m_Memory.Create(m_hWnd, m_PhysicalTotal);

	m_Composition.Create(m_hWnd, 0, 0, 0, 0);
	m_Composition.AddSegment(L"In use", PerfColors::Memory);
	if (m_HasMemoryLists) {
		m_Composition.AddSegment(L"Modified", RGB(196, 136, 60));
		m_Composition.AddSegment(L"Standby", RGB(96, 118, 170));
		m_Composition.AddSegment(L"Free", RGB(90, 90, 100));
	}
	else {
		m_Composition.AddSegment(L"Available", RGB(96, 118, 170));
	}
	m_Composition.SetTitle(L"Memory Composition");
	m_Composition.SetValueFormatter(Format::Bytes);
	m_Composition.SetTotal((float)m_PhysicalTotal);
	m_Lines.resize(2);
}

void CPerfMemoryPage::LayoutGraphs(CRect const& rc, int gap) {
	int compositionHeight = std::min(Scale(64), rc.Height());
	int memoryHeight = std::max(0, rc.Height() - compositionHeight - gap);
	m_Memory.MoveWindow(rc.left, rc.top, rc.Width(), memoryHeight);
	m_Composition.MoveWindow(rc.left, rc.bottom - compositionHeight, rc.Width(), compositionHeight);
}

void CPerfMemoryPage::Sample(PerfSample const& sample) {
	m_Memory.Push(sample);
	if (!sample.HasMemory)
		return;

	if (m_HasMemoryLists && sample.HasMemoryLists) {
		float values[] = { (float)sample.InUse, (float)sample.Modified, (float)sample.Standby, (float)sample.Free };
		m_Composition.SetValues(values, _countof(values));
	}
	else if (!m_HasMemoryLists) {
		float values[] = { (float)sample.InUse, (float)sample.Available };
		m_Composition.SetValues(values, _countof(values));
	}

	m_Lines[0] = std::format(L"In use: {} of {} ({:.0f}%)    Available: {}    Commit: {} of {}    Cached: {}",
		FormatBytes(sample.InUse), FormatBytes(sample.PhysicalTotal), sample.MemoryPercent(), FormatBytes(sample.Available),
		FormatBytes(sample.Committed), FormatBytes(sample.CommitLimit), FormatBytes(sample.Cached));
	m_Lines[1] = sample.HasMemoryLists
		? std::format(L"Modified: {}    Standby: {}    Free: {}    Paged pool: {}    Non-paged pool: {}",
			FormatBytes(sample.Modified), FormatBytes(sample.Standby), FormatBytes(sample.Free),
			FormatBytes(sample.PagedPool), FormatBytes(sample.NonPagedPool))
		: std::format(L"Paged pool: {}    Non-paged pool: {}", FormatBytes(sample.PagedPool), FormatBytes(sample.NonPagedPool));
}

void CPerfMemoryPage::ApplyTheme() {
	m_Memory.SetTheme(m_Theme);
	m_Composition.SetTheme(m_Theme);
}

void CPerfMemoryPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	m_Memory.SetTimeSpanText(text);
	m_Memory.SetExpectedInterval(interval);
}
