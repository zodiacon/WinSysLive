#include "pch.h"
#include "PerfPages.h"
#include "FormatHelper.h"
#include "AppSettings.h"

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

	std::wstring FormatBits(double bytesPerSecond) {
		wchar_t text[32];
		Format::BitsPerSec((float)(bytesPerSecond * 8), text, _countof(text));
		return text;
	}

	std::wstring NetworkLine(PerfSample const& s) {
		auto up = std::ranges::count_if(*s.Adapters, [](auto& a) { return a.OperStatus == WinSys::NetworkOperStatus::Up; });
		return std::format(L"Network: receive {}    send {}    Adapters up: {}", FormatBits(s.NetReceive), FormatBits(s.NetSend), up);
	}

	std::wstring MemoryLine(PerfSample const& s) {
		return std::format(L"Memory: {} of {} ({:.0f}%)    Commit: {} of {}    Cached: {}    Paged pool: {}    Non-paged pool: {}",
			FormatBytes(s.InUse), FormatBytes(s.PhysicalTotal), s.MemoryPercent(), FormatBytes(s.Committed), FormatBytes(s.CommitLimit),
			FormatBytes(s.Cached), FormatBytes(s.PagedPool), FormatBytes(s.NonPagedPool));
	}

	std::wstring FormatRate(double bytesPerSecond) {
		wchar_t text[32];
		Format::BytesPerSec((float)bytesPerSecond, text, _countof(text));
		return text;
	}

	std::wstring DiskLine(PerfSample const& s) {
		auto line = std::format(L"Disk: read {}    write {}    Drives: {}", FormatRate(s.DiskRead), FormatRate(s.DiskWrite), s.Disks->size());
		// among the drives with counters: one without them (no IOCTL_DISK_PERFORMANCE) has no activity to compare
		WinSys::DiskTracker::Volume const* busiest = nullptr;
		for (auto& v : *s.Disks)
			if (v.HasPerformance && (!busiest || v.ActivePercent > busiest->ActivePercent))
				busiest = &v;
		if (busiest)
			line += std::format(L"    Busiest: {}: ({:.0f}% active)", busiest->Letter, busiest->ActivePercent);
		return line;
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

void CNetworkGraph::Create(HWND hParent, std::wstring title) {
	CGraphControl::Create(hParent, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
	AddSeries(L"Receive", SeriesStyle{ PerfColors::Receive, PerfColors::Receive });
	AddSeries(L"Send", SeriesStyle{ PerfColors::Send, PerfColors::Send });
	SetTitle(std::move(title));
	SetRange(0, 1000);
	SetAutoScale(true);
	SetValueFormatter(Format::BitsPerSec);
	SetHistoryLength(PerfHistoryLength);
}

void CNetworkGraph::Push(double receive, double send, bool valid) {
	float values[] = { valid ? (float)(receive * 8) : MissingSample, valid ? (float)(send * 8) : MissingSample };
	PushSamples(values, _countof(values));
}

void CDiskGraph::Create(HWND hParent, std::wstring title) {
	CGraphControl::Create(hParent, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
	AddSeries(L"Read", SeriesStyle{ PerfColors::DiskRead, PerfColors::DiskRead });
	AddSeries(L"Write", SeriesStyle{ PerfColors::DiskWrite, PerfColors::DiskWrite });
	SetTitle(std::move(title));
	SetRange(0, 1024 * 1024);
	SetAutoScale(true);
	SetValueFormatter(Format::BytesPerSec);
	SetHistoryLength(PerfHistoryLength);
}

void CDiskGraph::Push(double read, double write, bool valid) {
	float values[] = { valid ? (float)read : MissingSample, valid ? (float)write : MissingSample };
	PushSamples(values, _countof(values));
}

//
// Summary
//

void CPerfSummaryPage::CreateGraphs() {
	m_Cpu.Create(m_hWnd);
	m_Memory.Create(m_hWnd, m_PhysicalTotal);
	m_Network.Create(m_hWnd, L"Network Throughput");
	m_Disk.Create(m_hWnd, L"Disk Transfer Rate");
	m_Lines.resize(4);
}

void CPerfSummaryPage::LayoutGraphs(CRect const& rc, int gap) {
	CGraphControl* graphs[] = { &m_Cpu, &m_Memory, &m_Network, &m_Disk };
	// two columns when there is room for them, so the graphs don't get too flat; otherwise one above the other
	int columns = rc.Width() >= Scale(800) ? 2 : 1;
	int rows = _countof(graphs) / columns;
	int width = std::max(0, rc.Width() - (columns - 1) * gap);
	int height = std::max(0, rc.Height() - (rows - 1) * gap);
	for (int i = 0; i < _countof(graphs); i++) {
		int row = i / columns, column = i % columns;
		// the last row and column take the rounding remainder
		int x = rc.left + column * (width / columns + gap), y = rc.top + row * (height / rows + gap);
		int w = column == columns - 1 ? rc.right - x : width / columns;
		int h = row == rows - 1 ? rc.bottom - y : height / rows;
		graphs[i]->MoveWindow(x, y, w, h);
	}
}

void CPerfSummaryPage::Sample(PerfSample const& sample) {
	m_Cpu.Push(sample);
	m_Memory.Push(sample);
	m_Network.Push(sample.NetReceive, sample.NetSend, sample.HasNetwork);
	m_Disk.Push(sample.DiskRead, sample.DiskWrite, sample.HasDisks);
	if (sample.HasCpu)
		m_Lines[0] = CpuLine(sample);
	if (sample.HasMemory)
		m_Lines[1] = MemoryLine(sample);
	if (sample.HasNetwork)
		m_Lines[2] = NetworkLine(sample);
	if (sample.HasDisks)
		m_Lines[3] = DiskLine(sample);
}

void CPerfSummaryPage::ApplyTheme() {
	m_Cpu.SetTheme(m_Theme);
	m_Memory.SetTheme(m_Theme);
	m_Network.SetTheme(m_Theme);
	m_Disk.SetTheme(m_Theme);
}

void CPerfSummaryPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	for (CGraphControl* graph : { (CGraphControl*)&m_Cpu, (CGraphControl*)&m_Memory, (CGraphControl*)&m_Network, (CGraphControl*)&m_Disk }) {
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
	m_ShowCores.Create(m_hWnd, rcDefault, L"Show &processors", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, IDC_PERF_SHOWCORES);
	bool show = AppSettings::Get().PerfShowProcessors() != 0;
	m_ShowCores.SetCheck(show ? BST_CHECKED : BST_UNCHECKED);
	m_Cores.ShowWindow(show ? SW_SHOWNA : SW_HIDE);
}

LRESULT CPerfCpuPage::OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	auto hdc = (HDC)wParam;
	if (::WindowFromDC(hdc) == m_hWnd) {
		// the page's own: WM_PAINT fills it
		bHandled = FALSE;
		return 0;
	}
	CRect rc;
	GetClientRect(&rc);
	CDCHandle(hdc).FillSolidRect(&rc, m_Theme.Background);
	return 1;
}

LRESULT CPerfCpuPage::OnCtlColor(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CDCHandle dc((HDC)wParam);
	dc.SetTextColor(m_Theme.TextColor);
	dc.SetBkColor(m_Theme.Background);
	if (m_Background.IsNull())
		m_Background.CreateSolidBrush(m_Theme.Background);
	return (LRESULT)m_Background.m_hBrush;
}

LRESULT CPerfCpuPage::OnPrintClient(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CRect rc;
	GetClientRect(&rc);
	CDCHandle((HDC)wParam).FillSolidRect(&rc, m_Theme.Background);
	return 0;
}

LRESULT CPerfCpuPage::OnShowCores(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	bool show = m_ShowCores.GetCheck() == BST_CHECKED;
	auto& settings = AppSettings::Get();
	settings.PerfShowProcessors(show);
	settings.Save();
	m_Cores.ShowWindow(show ? SW_SHOWNA : SW_HIDE);
	LayoutGraphs(m_GraphsRect, m_Gap);
	return 0;
}

void CPerfCpuPage::LayoutGraphs(CRect const& rc, int gap) {
	m_GraphsRect = rc;
	m_Gap = gap;
	// the check box at the end of the text's last line, in its font
	auto& text = GetTextRect();
	m_ShowCores.SetFont(GetTextFont());
	{
		CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(GetTextFont());
		CSize size;
		dc.GetTextExtent(L"Show processors", -1, &size);
		dc.SelectFont(old);
		int width = size.cx + ::GetSystemMetricsForDpi(SM_CXMENUCHECK, ::GetDpiForWindow(m_hWnd)) + Scale(8);
		int height = std::max(GetLineHeight(), Scale(16));
		m_ShowCores.MoveWindow(text.right - width, text.bottom - height, width, height);
	}

	// without the per-processor graphs, the CPU graph takes it all
	if (m_ShowCores.GetCheck() != BST_CHECKED) {
		m_Cpu.MoveWindow(rc.left, rc.top, rc.Width(), rc.Height());
		return;
	}
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
	// the check box's background, made again in its colors
	if (!m_Background.IsNull())
		m_Background.DeleteObject();
	if (m_ShowCores)
		m_ShowCores.Invalidate();
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

//
// Network
//

void CPerfNetworkPage::CreateGraphs() {
	m_Total.Create(m_hWnd, L"Network Throughput (all adapters)");
	m_Lines.resize(1);
}

void CPerfNetworkPage::LayoutGraphs(CRect const& rc, int gap) {
	m_GraphRect = rc;
	m_Gap = gap;
	Relayout();
}

void CPerfNetworkPage::Relayout() {
	auto& rc = m_GraphRect;
	int gap = m_Gap;
	auto count = (int)m_Adapters.size();
	if (count == 0) {
		m_Total.MoveWindow(&rc);
		return;
	}

	int totalHeight = (rc.Height() - gap) * 40 / 100;
	m_Total.MoveWindow(rc.left, rc.top, rc.Width(), totalHeight);

	// the adapters share the rest, in two columns once there are more than two
	int columns = count > 2 ? 2 : 1, rows = (count + columns - 1) / columns;
	int top = rc.top + totalHeight + gap;
	int width = (rc.Width() - (columns - 1) * gap) / columns;
	int height = std::max(0, (int)(rc.bottom - top - (rows - 1) * gap) / rows);
	for (int i = 0; i < count; i++) {
		int column = i % columns, row = i / columns;
		m_Adapters[i].Graph->MoveWindow(rc.left + column * (width + gap), top + row * (height + gap), width, height);
	}
}

void CPerfNetworkPage::SyncAdapters(std::vector<WinSys::NetworkTracker::Interface const*> const& adapters) {
	bool changed = adapters.size() != m_Adapters.size();
	for (size_t i = 0; !changed && i < adapters.size(); i++)
		changed = adapters[i]->Luid != m_Adapters[i].Luid;

	if (changed) {
		// keep the graphs (and their history) of adapters that are still there
		std::vector<Adapter> list;
		for (auto adapter : adapters) {
			auto it = std::find_if(m_Adapters.begin(), m_Adapters.end(), [&](auto& a) { return a.Luid == adapter->Luid; });
			if (it != m_Adapters.end()) {
				list.push_back(std::move(*it));
				continue;
			}
			Adapter a{ adapter->Luid, {}, std::make_unique<CNetworkGraph>() };
			a.Graph->Create(m_hWnd, {});
			a.Graph->SetTheme(m_Theme);
			if (m_Interval) {
				a.Graph->SetTimeSpanText(m_TimeSpanText);
				a.Graph->SetExpectedInterval(m_Interval);
			}
			list.push_back(std::move(a));
		}
		for (auto& a : m_Adapters) {
			if (a.Graph && a.Graph->IsWindow())
				a.Graph->DestroyWindow();
		}
		m_Adapters = std::move(list);
		Relayout();
	}

	// the title shows the state and the link speed, which can change (Wi-Fi rates, a cable unplugged)
	for (size_t i = 0; i < adapters.size(); i++) {
		auto& adapter = *adapters[i];
		std::wstring state;
		if (!adapter.Connected) {
			state = L"not connected";
		}
		else {
			wchar_t speed[32];
			Format::BitsPerSec((float)adapter.ReceiveLinkSpeed, speed, _countof(speed));
			state = speed;
		}
		auto title = std::format(L"{} ({}) - {}", adapter.Alias, adapter.Description, state);
		if (title != m_Adapters[i].Title) {
			m_Adapters[i].Title = title;
			m_Adapters[i].Graph->SetTitle(std::move(title));
		}
	}
}

void CPerfNetworkPage::Sample(PerfSample const& sample) {
	m_Total.Push(sample.NetReceive, sample.NetSend, sample.HasNetwork);
	if (!sample.HasNetwork)
		return;

	std::vector<WinSys::NetworkTracker::Interface const*> up;
	for (auto& adapter : *sample.Adapters) {
		if (adapter.OperStatus == WinSys::NetworkOperStatus::Up)
			up.push_back(&adapter);
	}
	SyncAdapters(up);
	for (size_t i = 0; i < up.size(); i++)
		m_Adapters[i].Graph->Push(up[i]->ReceiveRate, up[i]->SendRate, true);
	m_Lines[0] = NetworkLine(sample);
}

void CPerfNetworkPage::ApplyTheme() {
	m_Total.SetTheme(m_Theme);
	for (auto& a : m_Adapters)
		a.Graph->SetTheme(m_Theme);
}

void CPerfNetworkPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	m_TimeSpanText = text;
	m_Interval = interval;
	m_Total.SetTimeSpanText(text);
	m_Total.SetExpectedInterval(interval);
	for (auto& a : m_Adapters) {
		a.Graph->SetTimeSpanText(text);
		a.Graph->SetExpectedInterval(interval);
	}
}
//
// I/O
//

void CPerfIoPage::CreateGraph(CGraphControl& graph, PCWSTR title, ValueFormatFn formatter) {
	graph.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE, GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
	graph.AddSeries(L"Read", SeriesStyle{ PerfColors::DiskRead, PerfColors::DiskRead });
	graph.AddSeries(L"Write", SeriesStyle{ PerfColors::DiskWrite, PerfColors::DiskWrite });
	graph.AddSeries(L"Other", SeriesStyle{ PerfColors::IoOther, PerfColors::IoOther });
	graph.SetTitle(title);
	graph.SetRange(0, 1000);
	graph.SetAutoScale(true);
	graph.SetValueFormatter(std::move(formatter));
	graph.SetHistoryLength(PerfHistoryLength);
}

void CPerfIoPage::CreateGraphs() {
	CreateGraph(m_Transfer, L"I/O Transfer Rate", Format::BytesPerSec);
	CreateGraph(m_Operations, L"I/O Operations", [](float value, wchar_t* buffer, size_t cch) {
		auto text = std::format(L"{}/s", FormatHelper::FormatNumber((int64_t)std::lround(value)));
		wcsncpy_s(buffer, cch, text.c_str(), _TRUNCATE);
	});
	m_Lines.resize(1);
}

void CPerfIoPage::LayoutGraphs(CRect const& rc, int gap) {
	int height = std::max(0, rc.Height() - gap);
	int transferHeight = height * 55 / 100;
	m_Transfer.MoveWindow(rc.left, rc.top, rc.Width(), transferHeight);
	m_Operations.MoveWindow(rc.left, rc.top + transferHeight + gap, rc.Width(), height - transferHeight);
}

void CPerfIoPage::Sample(PerfSample const& sample) {
	auto valid = [&](double value) { return sample.HasIo ? (float)value : MissingSample; };
	float transfer[] = { valid(sample.IoRead), valid(sample.IoWrite), valid(sample.IoOther) };
	m_Transfer.PushSamples(transfer, _countof(transfer));
	float operations[] = { valid(sample.IoReadOps), valid(sample.IoWriteOps), valid(sample.IoOtherOps) };
	m_Operations.PushSamples(operations, _countof(operations));
	if (!sample.HasIo)
		return;

	auto ops = [](double value) { return FormatHelper::FormatNumber((int64_t)std::lround(value)); };
	m_Lines[0] = std::format(L"Read: {} ({}/s)    Write: {} ({}/s)    Other: {} ({}/s)    Total: {} ({}/s)",
		FormatRate(sample.IoRead), ops(sample.IoReadOps), FormatRate(sample.IoWrite), ops(sample.IoWriteOps),
		FormatRate(sample.IoOther), ops(sample.IoOtherOps),
		FormatRate(sample.IoRead + sample.IoWrite + sample.IoOther), ops(sample.IoReadOps + sample.IoWriteOps + sample.IoOtherOps));
}

void CPerfIoPage::ApplyTheme() {
	m_Transfer.SetTheme(m_Theme);
	m_Operations.SetTheme(m_Theme);
}

void CPerfIoPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	for (CGraphControl* graph : { &m_Transfer, &m_Operations }) {
		graph->SetTimeSpanText(text);
		graph->SetExpectedInterval(interval);
	}
}

//
// Disk
//

void CPerfDiskPage::CreateGraphs() {
	auto title = std::format(L"Disk {}:", m_Letter);
	m_Active.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_TOOLTIP | GCS_VALUEOVERLAY);
	m_ActiveSeries = m_Active.AddSeries(L"Active", SeriesStyle{ PerfColors::DiskActive, PerfColors::DiskActive });
	m_Active.SetTitle(title + L" Active Time");
	m_Active.SetRange(0, 100);
	m_Active.SetValueFormatter(Format::Percent);
	m_Active.SetHistoryLength(PerfHistoryLength);

	m_Transfer.Create(m_hWnd, title + L" Transfer Rate");

	m_Space.Create(m_hWnd, 0, 0, 0, 0);
	m_Space.AddSegment(L"Used", PerfColors::DiskActive);
	m_Space.AddSegment(L"Free", RGB(90, 90, 100));
	m_Space.SetTitle(L"Space");
	m_Space.SetValueFormatter(Format::Bytes);
	m_Lines.resize(2);
}

void CPerfDiskPage::LayoutGraphs(CRect const& rc, int gap) {
	int spaceHeight = std::min(Scale(64), rc.Height());
	int height = std::max(0, rc.Height() - spaceHeight - 2 * gap);
	int activeHeight = height / 2;
	m_Active.MoveWindow(rc.left, rc.top, rc.Width(), activeHeight);
	m_Transfer.MoveWindow(rc.left, rc.top + activeHeight + gap, rc.Width(), height - activeHeight);
	m_Space.MoveWindow(rc.left, rc.bottom - spaceHeight, rc.Width(), spaceHeight);
}

void CPerfDiskPage::Sample(PerfSample const& sample) {
	auto it = std::ranges::find_if(*sample.Disks, [&](auto& v) { return v.Letter == m_Letter; });
	if (it == sample.Disks->end() || !it->HasPerformance) {
		m_Active.PushSample(m_ActiveSeries, MissingSample);
		m_Transfer.Push(0, 0, false);
		return;
	}

	auto& v = *it;
	m_Active.PushSample(m_ActiveSeries, (float)v.ActivePercent);
	m_Transfer.Push(v.ReadRate, v.WriteRate, true);
	m_Space.SetTotal((float)v.TotalBytes);
	float space[] = { (float)(v.TotalBytes - v.FreeBytes), (float)v.FreeBytes };
	m_Space.SetValues(space, _countof(space));

	m_Lines[0] = std::format(L"Active: {:.0f}%    Read: {} ({:.0f}/s)    Write: {} ({:.0f}/s)    Response time: {:.2f} ms    Queue: {}",
		v.ActivePercent, FormatRate(v.ReadRate), v.ReadsPerSecond, FormatRate(v.WriteRate), v.WritesPerSecond, v.ResponseTime, v.QueueDepth);
	m_Lines[1] = std::format(L"Label: {}    File system: {}    Type: {}    Capacity: {}    Used: {}    Free: {}",
		v.Label.empty() ? L"(none)" : v.Label, v.FileSystem, v.DriveType == DRIVE_REMOVABLE ? L"Removable" : L"Fixed",
		FormatBytes((int64_t)v.TotalBytes), FormatBytes((int64_t)(v.TotalBytes - v.FreeBytes)), FormatBytes((int64_t)v.FreeBytes));
}

void CPerfDiskPage::ApplyTheme() {
	m_Active.SetTheme(m_Theme);
	m_Transfer.SetTheme(m_Theme);
	m_Space.SetTheme(m_Theme);
}

void CPerfDiskPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	for (CGraphControl* graph : { &m_Active, (CGraphControl*)&m_Transfer }) {
		graph->SetTimeSpanText(text);
		graph->SetExpectedInterval(interval);
	}
}

//
// GPU
//

void CPerfGpuPage::CreateGraphs() {
	auto& adapter = WinSys::GpuInfo::GetAdapters()[m_Index];
	m_Engines.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, GCS_GRID | GCS_FILL | GCS_TOOLTIP);
	m_Engines.SetGraphCount(adapter.Engines.size());
	m_Engines.SetRange(0, 100);
	m_Engines.SetValueFormatter(Format::Percent);
	m_Engines.SetHistoryLength(PerfHistoryLength);
	m_Engines.SetSeriesStyle(SeriesStyle{ PerfColors::Gpu, PerfColors::Gpu });
	m_Engines.SetSharedCrosshair(true);
	for (size_t i = 0; i < adapter.Engines.size(); i++)
		m_Engines.GetGraph(i)->SetTitle(adapter.Engines[i]);

	auto memoryGraph = [&](CGraphControl& graph, SeriesId& series, PCWSTR title, COLORREF color, uint64_t limit) {
		graph.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE, GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_TOOLTIP | GCS_VALUEOVERLAY);
		series = graph.AddSeries(L"In use", SeriesStyle{ color, color });
		graph.SetTitle(title);
		graph.SetRange(0, limit ? (float)limit : 1024.0f * 1024 * 1024);
		graph.SetValueFormatter(Format::Bytes);
		graph.SetHistoryLength(PerfHistoryLength);
	};
	memoryGraph(m_Dedicated, m_DedicatedSeries, L"Dedicated GPU Memory", PerfColors::Memory, adapter.DedicatedLimit);
	memoryGraph(m_Shared, m_SharedSeries, L"Shared GPU Memory", PerfColors::GpuShared, adapter.SharedLimit);
	m_Lines.resize(2);
}

void CPerfGpuPage::LayoutGraphs(CRect const& rc, int gap) {
	int height = std::max(0, rc.Height() - gap);
	int enginesHeight = height * 60 / 100, memoryHeight = height - enginesHeight;
	m_Engines.MoveWindow(rc.left, rc.top, rc.Width(), enginesHeight);
	// as the CPU page: square-ish tiles in the strip
	if (auto count = (int)m_Engines.GetGraphCount(); count > 0 && enginesHeight > 0) {
		int columns = std::clamp((int)std::lround(std::sqrt((double)count * rc.Width() / enginesHeight)), 1, count);
		m_Engines.SetLayout(columns, (count + columns - 1) / columns);
	}
	int width = std::max(0, (rc.Width() - gap) / 2), y = rc.top + enginesHeight + gap;
	m_Dedicated.MoveWindow(rc.left, y, width, memoryHeight);
	m_Shared.MoveWindow(rc.left + width + gap, y, rc.Width() - width - gap, memoryHeight);
}

void CPerfGpuPage::Sample(PerfSample const& sample) {
	if (!sample.Gpus || m_Index >= sample.Gpus->size())
		return;
	auto& adapter = WinSys::GpuInfo::GetAdapters()[m_Index];
	auto& gpu = (*sample.Gpus)[m_Index];
	if (sample.HasGpu && gpu.Engines.size() == m_Engines.GetGraphCount())
		m_Engines.PushSamples(gpu.Engines);
	m_Dedicated.PushSample(m_DedicatedSeries, (float)gpu.DedicatedUsed);
	m_Shared.PushSample(m_SharedSeries, (float)gpu.SharedUsed);

	m_Lines[0] = std::format(L"Utilization: {:.1f}%    Dedicated memory: {} of {}    Shared memory: {} of {}", gpu.Usage,
		FormatBytes((int64_t)gpu.DedicatedUsed), FormatBytes((int64_t)adapter.DedicatedLimit),
		FormatBytes((int64_t)gpu.SharedUsed), FormatBytes((int64_t)adapter.SharedLimit));
	m_Lines[1] = std::format(L"{}    Engines: {}    Nodes: {}", adapter.Name, adapter.Engines.size(), adapter.NodeEngine.size());
}

void CPerfGpuPage::ApplyTheme() {
	m_Engines.SetTheme(m_Theme);
	m_Dedicated.SetTheme(m_Theme);
	m_Shared.SetTheme(m_Theme);
}

void CPerfGpuPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	m_Engines.SetTimeSpanText(text);
	for (CGraphControl* graph : { &m_Dedicated, &m_Shared }) {
		graph->SetTimeSpanText(text);
		graph->SetExpectedInterval(interval);
	}
}
