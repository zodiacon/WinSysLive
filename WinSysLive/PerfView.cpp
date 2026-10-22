#include "pch.h"
#include "resource.h"
#include "PerfView.h"
#include "FormatHelper.h"
#include "AppSettings.h"
#include <WinSys/SystemInfo.h>

using namespace GraphCtrl;
using WinSys::SystemInfo;

namespace {
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

	if (auto basic = SystemInfo::GetBasicInformation(); basic) {
		m_PageSize = basic->PageSize;
		m_PhysicalTotal = (int64_t)basic->NumberOfPhysicalPages * basic->PageSize;
	}
	m_HasMemoryLists = SystemInfo::GetMemoryListInformation().has_value();
	m_PrevCpu = GetCpuTimes();

	m_Nav.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL);
	m_Nav.AddItem(L"Summary", PerfColors::User, CPerfNav::GraphType::None);
	m_Nav.AddItem(L"CPU", PerfColors::User, CPerfNav::GraphType::Percent);
	m_Nav.AddItem(L"Memory", PerfColors::Memory, CPerfNav::GraphType::Percent);
	m_Nav.AddItem(L"Network", PerfColors::Receive, CPerfNav::GraphType::AutoScale);
	m_Nav.AddItem(L"I/O", PerfColors::DiskRead, CPerfNav::GraphType::AutoScale);

	m_Pages.push_back(std::make_unique<CPerfSummaryPage>(m_PhysicalTotal));
	m_Pages.push_back(std::make_unique<CPerfCpuPage>(m_PrevCpu.size()));
	m_Pages.push_back(std::make_unique<CPerfMemoryPage>(m_PhysicalTotal, m_HasMemoryLists));
	m_Pages.push_back(std::make_unique<CPerfNetworkPage>());
	m_Pages.push_back(std::make_unique<CPerfIoPage>());
	for (auto& page : m_Pages)
		page->CreatePage(m_hWnd);

	UpdateTimeSpan();
	PerfSample sample;
	auto perf = SystemInfo::GetPerformanceInformation();
	SampleMemory(sample, perf ? &*perf : nullptr);
	SampleIo(sample, perf ? &*perf : nullptr);	// the baseline for the rates
	SampleNetwork(sample);	// the first update sets the baseline: all rates are 0
	SampleDisks(sample);	// creates the drive pages
	// a page per GPU, after the drives; the first sample is the baseline for the utilizations
	for (size_t i = 0; i < WinSys::GpuInfo::GetAdapters().size(); i++) {
		auto page = std::make_unique<CPerfGpuPage>(i);
		page->CreatePage(m_hWnd);
		page->SetTimeSpan(m_TimeSpanText, m_Interval);
		m_Pages.push_back(std::move(page));
		m_Nav.AddItem(std::format(L"GPU {}", i), PerfColors::Gpu, CPerfNav::GraphType::Percent);
	}
	SampleGpus(sample);
	for (auto& page : m_Pages)
		page->Update(sample, false);
	UpdateNav(sample);

	m_Nav.OnSelectionChanged = [this](int index) { ShowPage(index); };
	// the page by its name (an index from before the names, else Summary)
	auto& settings = AppSettings::Get();
	auto key = settings.PerfPageKey();
	int page = key.empty() ? settings.PerfPage() : FindPage(key);
	m_Nav.SelectItem(std::clamp(page, 0, m_Nav.GetItemCount() - 1));

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

void CPerfView::OnActivate(bool active) {
	if (active && m_Nav)
		m_Nav.SetFocus();
}

void CPerfView::OnRefresh(bool active) {
	UpdateTimeSpan();
	PerfSample sample;
	SampleCpu(sample);
	auto perf = SystemInfo::GetPerformanceInformation();
	SampleMemory(sample, perf ? &*perf : nullptr);
	SampleIo(sample, perf ? &*perf : nullptr);
	SampleNetwork(sample);
	SampleDisks(sample);
	SampleGpus(sample);
	for (int i = 0; i < (int)m_Pages.size(); i++)
		m_Pages[i]->Update(sample, active && i == m_Page);
	UpdateNav(sample);
}

void CPerfView::SampleCpu(PerfSample& sample) {
	auto& pm = Frame()->GetProcessManager();
	sample.Processes = pm.GetProcessCount();
	for (auto& p : pm.GetProcesses()) {
		sample.Threads += p->NativeInfo->NumberOfThreads;
		sample.Handles += p->NativeInfo->HandleCount;
	}
	sample.Uptime = ::GetTickCount64() / 1000;

	auto now = GetCpuTimes();
	if (now.size() != m_PrevCpu.size()) {
		// processors came or went (or the query failed); start over
		m_PrevCpu = std::move(now);
		return;
	}

	sample.Cores.resize(now.size());
	int64_t idle = 0, kernel = 0, user = 0;
	for (size_t i = 0; i < now.size(); i++) {
		auto dIdle = now[i].Idle - m_PrevCpu[i].Idle;
		auto dKernel = now[i].Kernel - m_PrevCpu[i].Kernel;
		auto dUser = now[i].User - m_PrevCpu[i].User;
		auto total = dKernel + dUser;
		sample.Cores[i] = total > 0 ? std::clamp(100.0f * (total - dIdle) / total, 0.0f, 100.0f) : 0.0f;
		idle += dIdle;
		kernel += dKernel;
		user += dUser;
	}
	m_PrevCpu = std::move(now);

	auto total = kernel + user;
	sample.KernelCpu = total > 0 ? std::clamp(100.0f * (kernel - idle) / total, 0.0f, 100.0f) : 0.0f;
	sample.UserCpu = total > 0 ? std::clamp(100.0f * user / total, 0.0f, 100.0f - sample.KernelCpu) : 0.0f;
	sample.HasCpu = true;
}

void CPerfView::SampleIo(PerfSample& sample, SYSTEM_PERFORMANCE_INFORMATION const* perf) {
	if (!perf)
		return;
	LARGE_INTEGER now, frequency;
	::QueryPerformanceCounter(&now);
	::QueryPerformanceFrequency(&frequency);
	IoCounters io{ perf->IoReadTransferCount.QuadPart, perf->IoWriteTransferCount.QuadPart, perf->IoOtherTransferCount.QuadPart,
		perf->IoReadOperationCount, perf->IoWriteOperationCount, perf->IoOtherOperationCount };
	if (m_PrevIoTime) {
		auto seconds = double(now.QuadPart - m_PrevIoTime) / frequency.QuadPart;
		if (seconds > 0) {
			auto rate = [&](int64_t current, int64_t previous) { return std::max(0.0, double(current - previous) / seconds); };
			sample.IoRead = rate(io.ReadBytes, m_PrevIo.ReadBytes);
			sample.IoWrite = rate(io.WriteBytes, m_PrevIo.WriteBytes);
			sample.IoOther = rate(io.OtherBytes, m_PrevIo.OtherBytes);
			// unsigned subtraction: right across a wrap-around
			sample.IoReadOps = uint32_t(io.ReadOps - m_PrevIo.ReadOps) / seconds;
			sample.IoWriteOps = uint32_t(io.WriteOps - m_PrevIo.WriteOps) / seconds;
			sample.IoOtherOps = uint32_t(io.OtherOps - m_PrevIo.OtherOps) / seconds;
			sample.HasIo = true;
		}
	}
	m_PrevIo = io;
	m_PrevIoTime = now.QuadPart;
}

void CPerfView::SampleMemory(PerfSample& sample, SYSTEM_PERFORMANCE_INFORMATION const* perf) {
	if (!perf || m_PageSize == 0)
		return;

	auto page = m_PageSize;
	sample.HasMemory = true;
	sample.PhysicalTotal = m_PhysicalTotal;
	sample.Available = (int64_t)perf->AvailablePages * page;
	sample.InUse = m_PhysicalTotal - sample.Available;
	if (m_HasMemoryLists) {
		if (auto lists = SystemInfo::GetMemoryListInformation(); lists) {
			int64_t standby = 0;
			for (auto count : lists->PageCountByPriority)
				standby += count;
			sample.Standby = standby * page;
			sample.Free = (int64_t)(lists->ZeroPageCount + lists->FreePageCount) * page;
			sample.Modified = (int64_t)lists->ModifiedPageCount * page;
			sample.InUse = m_PhysicalTotal - sample.Free - sample.Standby - sample.Modified;
			sample.HasMemoryLists = true;
		}
	}
	sample.Committed = (int64_t)perf->CommittedPages * page;
	sample.CommitLimit = (int64_t)perf->CommitLimit * page;
	sample.Cached = (int64_t)perf->ResidentSystemCachePage * page;
	sample.PagedPool = (int64_t)perf->PagedPoolPages * page;
	sample.NonPagedPool = (int64_t)perf->NonPagedPoolPages * page;
}

void CPerfView::SampleNetwork(PerfSample& sample) {
	sample.HasNetwork = m_Network.Update();
	sample.Adapters = &m_Network.GetInterfaces();
	sample.NetReceive = m_Network.GetTotalReceiveRate();
	sample.NetSend = m_Network.GetTotalSendRate();
}

void CPerfView::SampleDisks(PerfSample& sample) {
	m_Disks.Update();
	sample.Disks = &m_Disks.GetVolumes();
	for (auto& v : *sample.Disks) {
		if (!v.HasPerformance)
			continue;
		sample.HasDisks = true;
		sample.DiskRead += v.ReadRate;
		sample.DiskWrite += v.WriteRate;
	}
	SyncDiskPages();
}

void CPerfView::SampleGpus(PerfSample& sample) {
	sample.HasGpu = m_Gpu.Update();
	sample.Gpus = &m_Gpu.GetSamples();
}

int CPerfView::FirstGpuItem() const {
	return FirstDiskItem + (int)m_DiskLetters.size();
}

void CPerfView::SyncDiskPages() {
	auto& volumes = m_Disks.GetVolumes();
	auto exists = [&](wchar_t letter) {
		return std::ranges::any_of(volumes, [=](auto& v) { return v.Letter == letter; });
	};

	for (int i = (int)m_DiskLetters.size() - 1; i >= 0; i--) {
		if (exists(m_DiskLetters[i]))
			continue;
		int index = FirstDiskItem + i;
		if (m_Page == index)
			m_Nav.SelectItem(SummaryItem);
		::DestroyWindow(m_Pages[index]->GetHwnd());
		m_Pages.erase(m_Pages.begin() + index);
		m_Nav.RemoveItem(index);
		m_DiskLetters.erase(m_DiskLetters.begin() + i);
		if (m_Page > index)
			m_Page--;
	}

	bool added = false;
	for (auto& v : volumes) {
		auto pos = std::ranges::lower_bound(m_DiskLetters, v.Letter);
		if (pos != m_DiskLetters.end() && *pos == v.Letter)
			continue;
		// in drive letter order
		int index = FirstDiskItem + int(pos - m_DiskLetters.begin());
		m_DiskLetters.insert(pos, v.Letter);
		auto page = std::make_unique<CPerfDiskPage>(v.Letter);
		page->CreatePage(m_hWnd);
		page->SetTheme(m_Theme);
		if (m_Font)
			page->SetTextFont(m_Font);
		if (m_Interval)
			page->SetTimeSpan(m_TimeSpanText, m_Interval);
		m_Pages.insert(m_Pages.begin() + index, std::move(page));
		m_Nav.InsertItem(index, std::format(L"Disk {}:", v.Letter), PerfColors::DiskActive, CPerfNav::GraphType::Percent);
		if (m_Page >= index)
			m_Page++;
		added = true;
	}
	if (added)
		Layout();
}

void CPerfView::UpdateNav(PerfSample const& sample) {
	if (sample.HasCpu) {
		m_Nav.PushItemSample(CpuItem, sample.CpuPercent());
		m_Nav.SetItemValue(CpuItem, std::format(L"{:.0f}%", sample.CpuPercent()));
	}
	if (sample.HasMemory) {
		m_Nav.PushItemSample(MemoryItem, sample.MemoryPercent());
		m_Nav.SetItemValue(MemoryItem, std::format(L"{} / {} ({:.0f}%)",
			FormatBytes(sample.InUse), FormatBytes(sample.PhysicalTotal), sample.MemoryPercent()));
	}
	if (sample.HasNetwork) {
		wchar_t receive[32], send[32];
		Format::BitsPerSec((float)(sample.NetReceive * 8), receive, _countof(receive));
		Format::BitsPerSec((float)(sample.NetSend * 8), send, _countof(send));
		m_Nav.PushItemSample(NetworkItem, (float)(sample.NetReceive + sample.NetSend));
		m_Nav.SetItemValue(NetworkItem, std::format(L"R: {}  S: {}", receive, send));
	}
	if (sample.HasIo) {
		wchar_t read[32], write[32];
		Format::BytesPerSec((float)sample.IoRead, read, _countof(read));
		Format::BytesPerSec((float)sample.IoWrite, write, _countof(write));
		m_Nav.PushItemSample(IoItem, (float)(sample.IoRead + sample.IoWrite + sample.IoOther));
		m_Nav.SetItemValue(IoItem, std::format(L"R: {}  W: {}", read, write));
	}
	for (auto& v : *sample.Disks) {
		int index = FirstDiskItem + int(std::ranges::find(m_DiskLetters, v.Letter) - m_DiskLetters.begin());
		if (!v.HasPerformance) {
			m_Nav.SetItemValue(index, v.Label);
			continue;
		}
		m_Nav.PushItemSample(index, (float)v.ActivePercent);
		m_Nav.SetItemValue(index, v.Label.empty() ? std::format(L"{:.0f}%", v.ActivePercent) : std::format(L"{:.0f}%  {}", v.ActivePercent, v.Label));
	}
	if (sample.HasGpu) {
		auto& adapters = WinSys::GpuInfo::GetAdapters();
		for (size_t i = 0; i < sample.Gpus->size() && i < adapters.size(); i++) {
			auto usage = (*sample.Gpus)[i].Usage;
			m_Nav.PushItemSample(FirstGpuItem() + (int)i, usage);
			m_Nav.SetItemValue(FirstGpuItem() + (int)i, std::format(L"{:.0f}%  {}", usage, adapters[i].Name));
		}
	}
	if (sample.HasCpu || sample.HasMemory) {
		m_Nav.SetItemValue(SummaryItem, std::format(L"CPU {:.0f}%    Memory {:.0f}%", sample.CpuPercent(), sample.MemoryPercent()));
	}
}

void CPerfView::UpdateTimeSpan() {
	// the graphs show PerfHistoryLength samples, so the time they span follows the update interval
	auto interval = AppSettings::Get().UpdateInterval();
	if (interval == m_Interval)
		return;
	m_Interval = interval;

	auto seconds = PerfHistoryLength * interval / 1000;
	m_TimeSpanText = seconds % 60 == 0 ? std::format(L"{} minutes", seconds / 60) : std::format(L"{} seconds", seconds);
	for (auto& page : m_Pages)
		page->SetTimeSpan(m_TimeSpanText, interval);
}

void CPerfView::ShowPage(int index) {
	if (index == m_Page)
		return;
	auto old = m_Page;
	m_Page = index;
	::ShowWindow(m_Pages[index]->GetHwnd(), SW_SHOW);
	if (old >= 0)
		::ShowWindow(m_Pages[old]->GetHwnd(), SW_HIDE);
	AppSettings::Get().PerfPageKey(PageKey(index));
}

std::wstring CPerfView::PageKey(int index) const {
	switch (index) {
		case SummaryItem: return L"Summary";
		case CpuItem: return L"CPU";
		case MemoryItem: return L"Memory";
		case NetworkItem: return L"Network";
		case IoItem: return L"IO";
	}
	if (index >= FirstDiskItem && index < FirstGpuItem())
		return std::wstring(L"Disk:") + m_DiskLetters[index - FirstDiskItem];
	return L"GPU:" + std::to_wstring(index - FirstGpuItem());
}

int CPerfView::FindPage(std::wstring const& key) const {
	for (int i = 0; i < (int)m_Pages.size(); i++)
		if (PageKey(i) == key)
			return i;
	return -1;
}

void CPerfView::OnFontChanged(HFONT font) {
	m_Font = font;
	m_Nav.SetTextFont(font);
	for (auto& page : m_Pages)
		page->SetTextFont(font);
	Layout();
}

void CPerfView::OnThemeChanged(bool dark) {
	m_Theme = dark ? GraphTheme::Dark() : GraphTheme::Light();
	m_Nav.SetTheme(m_Theme);
	for (auto& page : m_Pages)
		page->SetTheme(m_Theme);
}

LRESULT CPerfView::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	Layout();
	return 0;
}

LRESULT CPerfView::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_Nav)
		m_Nav.SetFocus();
	return 0;
}

void CPerfView::Layout() {
	if (!m_Nav)
		return;

	CRect rc;
	GetClientRect(&rc);
	int navWidth = std::min(m_Nav.GetIdealWidth(), rc.Width() / 2);
	m_Nav.MoveWindow(rc.left, rc.top, navWidth, rc.Height());
	// all pages are kept sized, so switching pages only shows and hides them (laying out the processor grid is slow)
	for (auto& page : m_Pages)
		::MoveWindow(page->GetHwnd(), rc.left + navWidth, rc.top, rc.Width() - navWidth, rc.Height(), TRUE);
}

LRESULT CPerfView::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// the navigation list and the page cover the client area
}
