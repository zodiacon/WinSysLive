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

//
// Summary
//

void CPerfSummaryPage::CreateGraphs() {
	m_Cpu.Create(m_hWnd);
	m_Memory.Create(m_hWnd, m_PhysicalTotal);
	m_Network.Create(m_hWnd, L"Network Throughput");
	m_Lines.resize(3);
}

void CPerfSummaryPage::LayoutGraphs(CRect const& rc, int gap) {
	int height = std::max(0, rc.Height() - 2 * gap);
	int graphHeight = height / 3;
	int y = rc.top;
	m_Cpu.MoveWindow(rc.left, y, rc.Width(), graphHeight);
	y += graphHeight + gap;
	m_Memory.MoveWindow(rc.left, y, rc.Width(), graphHeight);
	y += graphHeight + gap;
	m_Network.MoveWindow(rc.left, y, rc.Width(), height - 2 * graphHeight);
}

void CPerfSummaryPage::Sample(PerfSample const& sample) {
	m_Cpu.Push(sample);
	m_Memory.Push(sample);
	m_Network.Push(sample.NetReceive, sample.NetSend, sample.HasNetwork);
	if (sample.HasCpu)
		m_Lines[0] = CpuLine(sample);
	if (sample.HasMemory)
		m_Lines[1] = MemoryLine(sample);
	if (sample.HasNetwork)
		m_Lines[2] = NetworkLine(sample);
}

void CPerfSummaryPage::ApplyTheme() {
	m_Cpu.SetTheme(m_Theme);
	m_Memory.SetTheme(m_Theme);
	m_Network.SetTheme(m_Theme);
}

void CPerfSummaryPage::ApplyTimeSpan(std::wstring const& text, UINT interval) {
	for (CGraphControl* graph : { (CGraphControl*)&m_Cpu, (CGraphControl*)&m_Memory, (CGraphControl*)&m_Network }) {
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

	m_Transfer.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE,
		GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
	m_Transfer.AddSeries(L"Read", SeriesStyle{ PerfColors::DiskRead, PerfColors::DiskRead });
	m_Transfer.AddSeries(L"Write", SeriesStyle{ PerfColors::DiskWrite, PerfColors::DiskWrite });
	m_Transfer.SetTitle(title + L" Transfer Rate");
	m_Transfer.SetRange(0, 1024 * 1024);
	m_Transfer.SetAutoScale(true);
	m_Transfer.SetValueFormatter(Format::BytesPerSec);
	m_Transfer.SetHistoryLength(PerfHistoryLength);

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
		float missing[] = { MissingSample, MissingSample };
		m_Transfer.PushSamples(missing, _countof(missing));
		return;
	}

	auto& v = *it;
	m_Active.PushSample(m_ActiveSeries, (float)v.ActivePercent);
	float transfer[] = { (float)v.ReadRate, (float)v.WriteRate };
	m_Transfer.PushSamples(transfer, _countof(transfer));
	m_Space.SetTotal((float)v.TotalBytes);
	float space[] = { (float)(v.TotalBytes - v.FreeBytes), (float)v.FreeBytes };
	m_Space.SetValues(space, _countof(space));

	wchar_t read[32], write[32];
	Format::BytesPerSec((float)v.ReadRate, read, _countof(read));
	Format::BytesPerSec((float)v.WriteRate, write, _countof(write));
	m_Lines[0] = std::format(L"Active: {:.0f}%    Read: {} ({:.0f}/s)    Write: {} ({:.0f}/s)    Response time: {:.2f} ms    Queue: {}",
		v.ActivePercent, read, v.ReadsPerSecond, write, v.WritesPerSecond, v.ResponseTime, v.QueueDepth);
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
	for (auto graph : { &m_Active, &m_Transfer }) {
		graph->SetTimeSpanText(text);
		graph->SetExpectedInterval(interval);
	}
}