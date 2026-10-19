#include "pch.h"
#include "resource.h"
#include "PerfView.h"
#include "FormatHelper.h"
#include "AppSettings.h"
#include <WinSys/SystemInfo.h>

using namespace GraphCtrl;
using WinSys::SystemInfo;

namespace {
	// samples kept by the graphs
	const size_t HistoryLength = 120;

	const COLORREF KernelColor = RGB(214, 94, 94);
	const COLORREF UserColor = RGB(17, 125, 187);
	const COLORREF MemoryColor = RGB(139, 92, 196);

	std::wstring FormatBytes(int64_t bytes) {
		wchar_t text[32];
		Format::Bytes((float)bytes, text, _countof(text));
		return text;
	}
}

CString CPerfView::GetTitle() const {
	return L"Performance";
}

LRESULT CPerfView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	ModifyStyle(0, WS_CLIPCHILDREN);

	NONCLIENTMETRICS ncm = { sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(m_hWnd));
	m_Font.CreateFontIndirect(&ncm.lfMessageFont);
	{
		CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_Font);
		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);
		m_LineHeight = tm.tmHeight + tm.tmExternalLeading;
		dc.SelectFont(old);
	}

	if (auto basic = SystemInfo::GetBasicInformation(); basic) {
		m_PageSize = basic->PageSize;
		m_PhysicalTotal = (int64_t)basic->NumberOfPhysicalPages * basic->PageSize;
	}
	m_HasMemoryLists = SystemInfo::GetMemoryListInformation().has_value();

	auto style = GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP | GCS_STACKED | GCS_VALUEOVERLAY;
	m_Cpu.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE, style);
	// stacked bottom up: kernel time, then user time
	m_KernelSeries = m_Cpu.AddSeries(L"Kernel", SeriesStyle{ KernelColor, KernelColor });
	m_UserSeries = m_Cpu.AddSeries(L"User", SeriesStyle{ UserColor, UserColor });
	m_Cpu.SetTitle(L"CPU Usage");
	m_Cpu.SetRange(0, 100);
	m_Cpu.SetValueFormatter(Format::Percent);
	m_Cpu.SetHistoryLength(HistoryLength);

	m_PrevCpu = GetCpuTimes();
	m_Cores.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, GCS_GRID | GCS_FILL | GCS_TOOLTIP);
	m_Cores.SetGraphCount(m_PrevCpu.size());
	m_Cores.SetTitlePrefix(L"CPU ");
	m_Cores.SetRange(0, 100);
	m_Cores.SetValueFormatter(Format::Percent);
	m_Cores.SetHistoryLength(HistoryLength);
	m_Cores.SetSharedCrosshair(true);

	m_Memory.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_TOOLTIP | GCS_VALUEOVERLAY);
	m_InUseSeries = m_Memory.AddSeries(L"In use", SeriesStyle{ MemoryColor, MemoryColor });
	m_Memory.SetTitle(L"Physical Memory In Use");
	m_Memory.SetRange(0, (float)m_PhysicalTotal);
	m_Memory.SetValueFormatter(Format::Bytes);
	m_Memory.SetHistoryLength(HistoryLength);

	m_Composition.Create(m_hWnd, 0, 0, 0, 0);
	m_Composition.AddSegment(L"In use", MemoryColor);
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

	UpdateTimeSpan();
	SampleMemory();

	return 0;
}

std::vector<CPerfView::CpuTimes> CPerfView::GetCpuTimes() {
	std::vector<CpuTimes> times;
	// without a group, the query only covers the calling thread's processor group
	auto groups = ::GetActiveProcessorGroupCount();
	for (USHORT group = 0; group < groups; group++) {
		for (auto& info : SystemInfo::GetProcessorPerformanceInformation(group))
			times.push_back({ info.IdleTime.QuadPart, info.KernelTime.QuadPart, info.UserTime.QuadPart });
	}
	return times;
}

void CPerfView::OnRefresh(bool active) {
	UpdateTimeSpan();
	SampleCpu();
	SampleMemory();
	if (active)
		InvalidateRect(m_SummaryRect, FALSE);
}

void CPerfView::SampleCpu() {
	auto now = GetCpuTimes();
	if (now.size() != m_PrevCpu.size()) {
		// processors came or went (or the query failed); start over
		m_PrevCpu = std::move(now);
		return;
	}

	std::vector<float> cores(now.size());
	int64_t idle = 0, kernel = 0, user = 0;
	for (size_t i = 0; i < now.size(); i++) {
		auto dIdle = now[i].Idle - m_PrevCpu[i].Idle;
		auto dKernel = now[i].Kernel - m_PrevCpu[i].Kernel;
		auto dUser = now[i].User - m_PrevCpu[i].User;
		auto total = dKernel + dUser;
		cores[i] = total > 0 ? std::clamp(100.0f * (total - dIdle) / total, 0.0f, 100.0f) : 0.0f;
		idle += dIdle;
		kernel += dKernel;
		user += dUser;
	}
	m_PrevCpu = std::move(now);

	auto total = kernel + user;
	float kernelPercent = total > 0 ? std::clamp(100.0f * (kernel - idle) / total, 0.0f, 100.0f) : 0.0f;
	float userPercent = total > 0 ? std::clamp(100.0f * user / total, 0.0f, 100.0f - kernelPercent) : 0.0f;
	float values[] = { kernelPercent, userPercent };	// in AddSeries order
	m_Cpu.PushSamples(values, _countof(values));
	m_Cores.PushSamples(cores);

	// summary, first line
	auto& pm = Frame()->GetProcessManager();
	int64_t threads = 0, handles = 0;
	for (auto& p : pm.GetProcesses()) {
		threads += p->NativeInfo->NumberOfThreads;
		handles += p->NativeInfo->HandleCount;
	}
	auto uptime = ::GetTickCount64() / 1000;
	m_Summary[0] = std::format(L"CPU: {:.1f}% (kernel {:.1f}%)    Processes: {}    Threads: {}    Handles: {}    Up time: {}:{:02}:{:02}:{:02}",
		kernelPercent + userPercent, kernelPercent, FormatHelper::FormatNumber(pm.GetProcessCount()), FormatHelper::FormatNumber(threads),
		FormatHelper::FormatNumber(handles), uptime / 86400, uptime / 3600 % 24, uptime / 60 % 60, uptime % 60);
}

void CPerfView::SampleMemory() {
	auto perf = SystemInfo::GetPerformanceInformation();
	if (!perf || m_PageSize == 0) {
		m_Memory.PushSample(m_InUseSeries, MissingSample);
		return;
	}

	auto page = m_PageSize;
	int64_t available = (int64_t)perf->AvailablePages * page;
	int64_t inUse = m_PhysicalTotal - available;
	if (m_HasMemoryLists) {
		if (auto lists = SystemInfo::GetMemoryListInformation(); lists) {
			int64_t standby = 0;
			for (auto count : lists->PageCountByPriority)
				standby += count;
			standby *= page;
			int64_t free = (int64_t)(lists->ZeroPageCount + lists->FreePageCount) * page;
			int64_t modified = (int64_t)lists->ModifiedPageCount * page;
			inUse = m_PhysicalTotal - free - standby - modified;
			float values[] = { (float)inUse, (float)modified, (float)standby, (float)free };
			m_Composition.SetValues(values, _countof(values));
		}
	}
	else {
		float values[] = { (float)inUse, (float)available };
		m_Composition.SetValues(values, _countof(values));
	}
	m_Memory.PushSample(m_InUseSeries, (float)inUse);

	// summary, second line
	m_Summary[1] = std::format(L"Memory: {} of {} ({:.0f}%)    Commit: {} of {}    Cached: {}    Paged pool: {}    Non-paged pool: {}",
		FormatBytes(inUse), FormatBytes(m_PhysicalTotal), m_PhysicalTotal ? 100.0 * inUse / m_PhysicalTotal : 0.0,
		FormatBytes((int64_t)perf->CommittedPages * page), FormatBytes((int64_t)perf->CommitLimit * page),
		FormatBytes((int64_t)perf->ResidentSystemCachePage * page),
		FormatBytes((int64_t)perf->PagedPoolPages * page), FormatBytes((int64_t)perf->NonPagedPoolPages * page));
}

void CPerfView::UpdateTimeSpan() {
	// the graphs show HistoryLength samples, so the time they span follows the update interval
	auto interval = AppSettings::Get().UpdateInterval();
	if (interval == m_Interval)
		return;
	m_Interval = interval;

	auto seconds = HistoryLength * interval / 1000;
	auto text = seconds % 60 == 0 ? std::format(L"{} minutes", seconds / 60) : std::format(L"{} seconds", seconds);
	for (auto graph : { &m_Cpu, &m_Memory }) {
		graph->SetTimeSpanText(text);
		graph->SetExpectedInterval(interval);
	}
	m_Cores.SetTimeSpanText(text);
}

void CPerfView::OnThemeChanged(bool dark) {
	m_Theme = dark ? GraphTheme::Dark() : GraphTheme::Light();
	m_Cpu.SetTheme(m_Theme);
	m_Cores.SetTheme(m_Theme);
	m_Memory.SetTheme(m_Theme);
	m_Composition.SetTheme(m_Theme);
	Invalidate();
}

LRESULT CPerfView::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	Layout();
	return 0;
}

void CPerfView::Layout() {
	if (!m_Cpu)
		return;

	auto dpi = ::GetDpiForWindow(m_hWnd);
	int margin = ::MulDiv(8, dpi, 96), gap = ::MulDiv(6, dpi, 96), compositionHeight = ::MulDiv(64, dpi, 96);
	CRect rc;
	GetClientRect(&rc);
	rc.DeflateRect(margin, margin);
	int summaryHeight = 2 * m_LineHeight + gap;
	int graphs = std::max(0, rc.Height() - summaryHeight - compositionHeight - 3 * gap);
	int cpuHeight = graphs * 38 / 100, coresHeight = graphs * 32 / 100, memoryHeight = graphs - cpuHeight - coresHeight;

	int y = rc.top;
	m_Cpu.MoveWindow(rc.left, y, rc.Width(), cpuHeight);
	y += cpuHeight + gap;
	m_Cores.MoveWindow(rc.left, y, rc.Width(), coresHeight);
	// the grid's automatic layout arranges the tiles in a square, which leaves them far too short in a wide strip;
	// aim for square tiles instead (each tile's title takes part of its height, so wider tiles get short plots)
	if (auto count = (int)m_Cores.GetGraphCount(); count > 0 && coresHeight > 0) {
		int columns = std::clamp((int)std::lround(std::sqrt((double)count * rc.Width() / coresHeight)), 1, count);
		m_Cores.SetLayout(columns, (count + columns - 1) / columns);
	}
	y += coresHeight + gap;
	m_Memory.MoveWindow(rc.left, y, rc.Width(), memoryHeight);
	y += memoryHeight + gap;
	m_Composition.MoveWindow(rc.left, y, rc.Width(), compositionHeight);
	y += compositionHeight + gap;
	m_SummaryRect.SetRect(rc.left, y, rc.right, y + summaryHeight);
	Invalidate();
}

LRESULT CPerfView::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// OnPaint fills the background
}

LRESULT CPerfView::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CPaintDC dc(m_hWnd);
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, m_Theme.Background);

	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(m_Theme.TextColor);
	auto old = dc.SelectFont(m_Font);
	CRect line(m_SummaryRect);
	for (auto& text : m_Summary) {
		line.bottom = line.top + m_LineHeight;
		dc.DrawText(text.c_str(), (int)text.size(), &line, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		line.top = line.bottom;
	}
	dc.SelectFont(old);
	return 0;
}
