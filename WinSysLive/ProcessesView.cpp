#include "pch.h"
#include "resource.h"
#include "ProcessesView.h"
#include "ProcessSheet.h"
#include "FormatHelper.h"
#include "AppSettings.h"
#include "NetworkTracking.h"
#include <WinSys/Processes.h>
#include <WinSys/SystemInfo.h>
#include <WinSys/Helpers.h>
#include <WinSys/ServiceManager.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <DpiHelper.h>
#include "ColumnLayout.h"
#include "AffinityDlg.h"
#include "WaitChainDlg.h"
#include <WinSys/DotNet.h>
#include <ranges>

using namespace WinSys;

namespace {
	// in CProcessesView::Column order; the keys are stored in the ProcessColumns setting
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Name", L"Name", 220, LVCFMT_LEFT, true, true },
		{ L"PID", L"Id", 70, LVCFMT_RIGHT, true, true },
		{ L"Status", L"Status", 100, LVCFMT_LEFT, false },
		{ L"CPU (%)", L"CPU", 70, LVCFMT_RIGHT, true },
		{ L"CPU Time", L"CpuTime", 100, LVCFMT_RIGHT, false },
		{ L"Kernel Time", L"KernelTime", 100, LVCFMT_RIGHT, false },
		{ L"User Time", L"UserTime", 100, LVCFMT_RIGHT, false },
		{ L"CPU Cycles", L"CpuCycles", 170, LVCFMT_RIGHT, false },
		{ L"Ctx Switches", L"ContextSwitches", 110, LVCFMT_RIGHT, false },
		{ L"Ctx Switch Delta", L"ContextSwitchDelta", 110, LVCFMT_RIGHT, false },
		{ L"Private Bytes", L"PrivateBytes", 100, LVCFMT_RIGHT, true },
		{ L"Private Bytes Delta", L"PrivateBytesDelta", 100, LVCFMT_RIGHT, false },
		{ L"Peak Private Bytes", L"PeakPrivateBytes", 110, LVCFMT_RIGHT, false },
		{ L"Working Set", L"WorkingSet", 100, LVCFMT_RIGHT, true },
		{ L"Peak Working Set", L"PeakWorkingSet", 110, LVCFMT_RIGHT, false },
		{ L"Virtual Size", L"VirtualSize", 120, LVCFMT_RIGHT, false },
		{ L"Page Faults", L"PageFaults", 100, LVCFMT_RIGHT, false },
		{ L"Threads", L"Threads", 60, LVCFMT_RIGHT, true },
		{ L"Peak Threads", L"PeakThreads", 80, LVCFMT_RIGHT, false },
		{ L"Handles", L"Handles", 70, LVCFMT_RIGHT, true },
		{ L"GDI Objects", L"GdiObjects", 80, LVCFMT_RIGHT, false },
		{ L"User Objects", L"UserObjects", 80, LVCFMT_RIGHT, false },
		{ L"Session", L"Session", 60, LVCFMT_RIGHT, true },
		{ L"User Name", L"User", 180, LVCFMT_LEFT, true },
		{ L"Priority", L"Priority", 90, LVCFMT_LEFT, true },
		{ L"Memory Priority", L"MemoryPriority", 100, LVCFMT_RIGHT, false },
		{ L"I/O Pri", L"IoPriority", 80, LVCFMT_LEFT, false },
		{ L"Start Time", L"StartTime", 150, LVCFMT_LEFT, true },
		{ L"Architecture", L"Architecture", 80, LVCFMT_LEFT, false },
		{ L"Integrity", L"IntegrityLevel", 90, LVCFMT_LEFT, false },
		{ L"Protection", L"Protection", 130, LVCFMT_LEFT, false },
		{ L"UAC Virtualization", L"Virtualization", 110, LVCFMT_LEFT, false },
		{ L"Job ID", L"JobId", 60, LVCFMT_RIGHT, false },
		{ L"I/O Read Bytes", L"IoRead", 100, LVCFMT_RIGHT, false },
		{ L"I/O Write Bytes", L"IoWrite", 100, LVCFMT_RIGHT, false },
		{ L"I/O Other Bytes", L"IoOther", 100, LVCFMT_RIGHT, false },
		{ L"Network Send Bytes", L"NetworkSend", 140, LVCFMT_RIGHT, false },
		{ L"Network Recv Bytes", L"NetworkReceive", 140, LVCFMT_RIGHT, false },
		{ L"GPU (%)", L"Gpu", 70, LVCFMT_RIGHT, false },
		{ L"Dedicated GPU Memory", L"GpuDedicated", 140, LVCFMT_RIGHT, false },
		{ L"Shared GPU Memory", L"GpuShared", 130, LVCFMT_RIGHT, false },
		{ L"Description", L"Description", 220, LVCFMT_LEFT, true },
		{ L"Image Path", L"ImagePath", 300, LVCFMT_LEFT, true },
		{ L"Command Line", L"CommandLine", 400, LVCFMT_LEFT, true },
	};

	// ID_PRIORITY_* order
	const struct {
		WinSys::ProcessPriorityClass PriorityClass;
		int BasePriority;
	} s_Priorities[] = {
		{ WinSys::ProcessPriorityClass::Realtime, 24 },
		{ WinSys::ProcessPriorityClass::High, 13 },
		{ WinSys::ProcessPriorityClass::AboveNormal, 10 },
		{ WinSys::ProcessPriorityClass::Normal, 8 },
		{ WinSys::ProcessPriorityClass::BelowNormal, 6 },
		{ WinSys::ProcessPriorityClass::Idle, 4 },
	};

	// highlighting of new processes lasts this many updates
	const int NewProcessTicks = 2;
	// the CLR starts after the process, so a process is checked for it again on every update until it's this old
	const int64_t DotNetCheckPeriod = 15 * 10000000LL;	// 100 ns units
}

CString CProcessesView::GetTitle() const {
	return L"Processes";
}

LRESULT CProcessesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	// the Own kind: the app's own entry in the snapshot gives the user as GetUser formats it
	if (auto self = Frame()->GetProcessManager().GetProcessById(::GetCurrentProcessId()))
		m_CurrentUser = self->GetUser();

	// the filter box (below, once the list exists) goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);

	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// the splitter forwards the tree's notifications (WM_NOTIFY) to this view
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// the image list is the frame's, so the list view must not destroy it
	m_Tree.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Tree.SetCallbackMode(true);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());
	// the TreeListView doesn't scale its column widths to a new DPI
	ListViewHelper::EnableColumnDpiScaling(m_Tree.GetListCtrl());

	LoadSort();
	SetColumns(LoadColumns());

	int width = 360;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, width, L"Filter: name, PID, user, description or command line (Ctrl+F)",
		m_Tree, [this](auto&) {
			RebuildTree();
			UpdateFilterStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	m_Colors = ProcessColors::Load();

	m_TreeMode = AppSettings::Get().ProcessTreeMode();
	BuildItems();

	m_Details.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Details.Init(Frame());
	m_Splitter.SetSplitterPanes(m_Tree, m_Details);
	auto& settings = AppSettings::Get();
	m_Splitter.SetSplitterPosPct(std::clamp(settings.ProcessSplitterPos(), 10, 90), false);
	m_LowerPane = settings.ProcessLowerPane();
	m_KeepSelectionVisible = settings.ProcessKeepSelectionVisible();
	m_Splitter.SetSinglePaneMode(m_LowerPane ? SPLIT_PANE_NONE : SPLIT_PANE_TOP);

	// enabled while active (see OnActivate)
	Frame()->GetUI().UIEnable(ID_VIEW_PROCESSTREE, false);
	Frame()->GetUI().UIEnable(ID_VIEW_LOWERPANE, false);
	Frame()->GetUI().UIEnable(ID_VIEW_KEEPSELECTIONVISIBLE, false);
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, false);
	Frame()->GetUI().UIEnable(ID_PROCESS_KILL, false);
	Frame()->GetUI().UIEnable(ID_KILL, false);

	return 0;
}

void CProcessesView::SaveSettings() {
	SaveLayout();
	m_Details.SaveColumns();
}

void CProcessesView::SaveLayout() const {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	// the splitter keeps its position while the lower pane is hidden
	auto& settings = AppSettings::Get();
	settings.ProcessSplitterPos(m_Splitter.GetSplitterPosPct());
	settings.Save();
}

LRESULT CProcessesView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the lower pane's pages save their columns as they're destroyed
	SaveLayout();
	bHandled = FALSE;
	return 0;
}

std::vector<CProcessesView::ShownColumn> CProcessesView::GetShownColumns() const {
	int count = (int)m_Columns.size();
	std::vector<int> order(count);
	m_Tree.GetHeader().GetOrderArray(count, order.data());
	std::vector<ShownColumn> columns;
	// at 96 DPI, as the setting keeps them
	auto dpi = DpiHelper::GetDpi(m_Tree);
	for (int index : order)
		columns.push_back({ m_Columns[index], DpiHelper::Scale(m_Tree.GetColumnWidth(index), USER_DEFAULT_SCREEN_DPI, dpi) });
	return columns;
}

void CProcessesView::SetColumns(std::vector<ShownColumn> const& columns) {
	ATLASSERT(std::ranges::count(columns, Column::Name, &ShownColumn::Id) == 1);
	ATLASSERT(std::ranges::count(columns, Column::Id, &ShownColumn::Id) == 1);
	m_Tree.SetSortColumn(-1, 0);
	// Name stays list column 0, which draws the tree; the others are inserted after it,
	// and the header's order array puts them all in the requested order
	while (m_Tree.GetColumnCount() > 1)
		m_Tree.DeleteColumn(m_Tree.GetColumnCount() - 1);
	m_Columns = { Column::Name };
	for (auto& c : columns)
		if (c.Id != Column::Name)
			m_Columns.push_back(c.Id);

	for (int i = 0; i < (int)m_Columns.size(); i++) {
		auto& info = s_Columns[(int)m_Columns[i]];
		int width = DpiHelper::Scale(std::ranges::find(columns, m_Columns[i], &ShownColumn::Id)->Width, m_Tree.m_hWnd);
		if (i == 0 && m_Tree.GetColumnCount() == 1)
			m_Tree.SetColumnWidth(0, width);
		else
			m_Tree.InsertColumn(i, info.Name, width, info.Format);
		m_Tree.SetColumnSortable(i, true);
	}

	std::vector<int> order;
	for (auto& c : columns)
		order.push_back((int)(std::ranges::find(m_Columns, c.Id) - m_Columns.begin()));
	m_Tree.GetHeader().SetOrderArray((int)order.size(), order.data());

	// the sort stays if its column is still shown
	auto sorted = std::ranges::find(m_Columns, (Column)m_SortColumn);
	if (m_SortColumn >= 0 && sorted != m_Columns.end())
		m_Tree.SetSortColumn((int)(sorted - m_Columns.begin()), m_SortAscending ? 1 : -1);
	else
		m_SortColumn = -1;

	// the network trace costs a little all the time, so it only runs while it's needed
	if (ShowsNetwork()) {
		if (!m_Network && !(m_Network = NetworkTracking::Acquire()))
			m_NetworkError = ::GetLastError();
	}
	else
		m_Network.reset();
	UpdateHungProcesses();
	m_Tree.Invalidate();
}

void CProcessesView::UpdateHungProcesses() {
	if (std::ranges::contains(m_Columns, Column::Status))
		m_HungPids = Process::GetHungProcesses();
	else
		m_HungPids.clear();
}

CProcessesView::ProcessStatus CProcessesView::GetStatus(ProcessItem const& item) const {
	if ((item.Kinds & ProcessKind::Suspended) != ProcessKind::None)
		return ProcessStatus::Suspended;
	if (std::ranges::binary_search(m_HungPids, item.Process->Id))
		return ProcessStatus::NotResponding;
	return ProcessStatus::Running;
}

WinSys::ProcessNetworkTracker::Counters CProcessesView::NetworkCounters(uint32_t pid) const {
	return m_Network ? m_Network->GetCounters(pid) : WinSys::ProcessNetworkTracker::Counters{};
}

bool CProcessesView::ShowsGpu() const {
	return std::ranges::any_of(m_Columns, [](auto c) { return c == Column::GpuUsage || c == Column::GpuDedicated || c == Column::GpuShared; });
}

bool CProcessesView::ShowsNetwork() const {
	return std::ranges::contains(m_Columns, Column::NetworkSend) || std::ranges::contains(m_Columns, Column::NetworkReceive);
}

uint64_t CProcessesView::GetContextSwitches(ProcessInfoEx const& process) {
	if (process.ExtendedInfo)
		return process.ExtendedInfo->ContextSwitches;
	// the snapshot is SystemExtendedProcessInformation: the threads are SYSTEM_EXTENDED_THREAD_INFORMATION
	auto info = process.NativeInfo;
	auto threads = reinterpret_cast<SYSTEM_EXTENDED_THREAD_INFORMATION const*>(info->Threads);
	uint64_t total = 0;
	for (ULONG i = 0; i < info->NumberOfThreads; i++)
		total += threads[i].ThreadInfo.ContextSwitches;
	return total;
}

void CProcessesView::UpdateDeltas(ProcessItem& item) {
	auto& p = *item.Process;
	auto switches = GetContextSwitches(p);
	auto privateBytes = (int64_t)p.NativeInfo->PrivatePageCount;
	if (item.PrevPrivateBytes >= 0) {
		// without the process total, threads that exited take their switches with them
		item.ContextSwitchDelta = switches >= item.PrevContextSwitches ? int64_t(switches - item.PrevContextSwitches) : 0;
		item.PrivateBytesDelta = privateBytes - item.PrevPrivateBytes;
	}
	item.PrevContextSwitches = switches;
	item.PrevPrivateBytes = privateBytes;
}

uint32_t CProcessesView::GetJobId(ProcessInfoEx& process) {
	if (process.ExtendedInfo)
		return process.ExtendedInfo->JobObjectId;
	return process.IsInJob() ? 1 : 0;
}

std::vector<CProcessesView::ShownColumn> CProcessesView::LoadColumns() {
	std::vector<ShownColumn> columns;
	for (auto& c : ColumnLayout::Parse(AppSettings::Get().ProcessColumns(), s_Columns))
		columns.push_back({ (Column)c.Id, c.Width });
	return columns;
}

std::vector<ColumnLayout::ShownColumn> CProcessesView::ToLayout(std::vector<ShownColumn> const& columns) {
	std::vector<ColumnLayout::ShownColumn> layout;
	for (auto& c : columns)
		layout.push_back({ (int)c.Id, c.Width });
	return layout;
}

void CProcessesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ProcessColumns(ColumnLayout::Format(ToLayout(GetShownColumns()), s_Columns));
	settings.Save();
}

void CProcessesView::OnActivate(bool active) {
	m_Active = active;
	// the Services view handles it too, so it's set here rather than on every refresh (UpdateUI)
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	if (active && m_SortPending) {
		auto selected = GetSelectedProcess();
		auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;
		Sort();
		RestoreSelection(selectedKey ? &*selectedKey : nullptr, m_KeepSelectionVisible);
	}
	UpdateUI();
	// the status line's count is this view's
	if (!m_Filter.GetText().empty())
		active ? UpdateFilterStatus() : Frame()->SetStatusText(0, L"");
	if (active)
		m_Tree.SetFocus();
}

void CProcessesView::OnFontChanged(HFONT font) {
	m_Tree.SetFont(font);
	m_Filter.SetFont(font);
	m_Details.SetListFont(font);
}

void CProcessesView::OnDpiChanged(UINT /*dpi*/) {
	// the frame's image list has the new DPI's size: the rows take its height
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Details.ResetImageList();
}

void CProcessesView::OnProcessColorsChanged() {
	m_Colors = ProcessColors::Load();
	m_Tree.Invalidate();
}

void CProcessesView::OnThemeChanged(bool dark) {
	// the TreeListView doesn't follow WTLHelper's mode switch by itself
	m_Tree.SetBkColor(dark ? DarkMode::getViewBackgroundColor() : ::GetSysColor(COLOR_WINDOW));
	m_Tree.SetTextColor(dark ? DarkMode::getViewTextColor() : ::GetSysColor(COLOR_WINDOWTEXT));
	m_Tree.Invalidate();
}

void CProcessesView::ShowProcess(uint32_t pid) {
	auto process = Frame()->GetProcessManager().GetProcessById(pid);
	if (!process)
		return;
	// a process the filter hides is shown by clearing the filter
	if (auto it = m_Items.find(process->Key); it != m_Items.end() && !it->second.Item && !m_Filter.GetText().empty())
		m_Filter.Clear();
	if (auto it = m_Items.find(process->Key); it != m_Items.end() && it->second.Item) {
		m_Tree.SelectItem(it->second.Item);
		m_Tree.SetFocus();
	}
}

void CProcessesView::SetFixedKinds(ProcessItem& item) const {
	auto& p = *item.Process;
	// the kinds that are checked on refreshes, not only here
	auto kinds = item.Kinds & (ProcessKind::Service | ProcessKind::DotNet | ProcessKind::Suspended);
	if (p.IsSecure())
		kinds |= ProcessKind::Secure;
	if (p.IsProtected())
		kinds |= ProcessKind::Protected;
	if (p.IsImmersive())
		kinds |= ProcessKind::Immersive;
	if (GetJobId(p))
		kinds |= ProcessKind::Job;
	if (!m_CurrentUser.empty() && ::_wcsicmp(p.GetUser().c_str(), m_CurrentUser.c_str()) == 0)
		kinds |= ProcessKind::Own;
	item.Kinds = kinds;
}

bool CProcessesView::IsSuspended(ProcessInfoEx const& process) {
	// every thread waiting because it's suspended (a suspended process, a frozen Store app); the threads are
	// SYSTEM_EXTENDED_THREAD_INFORMATION (as in GetContextSwitches)
	auto info = process.NativeInfo;
	if (info->NumberOfThreads == 0 || process.Id <= 4)
		return false;
	auto threads = reinterpret_cast<SYSTEM_EXTENDED_THREAD_INFORMATION const*>(info->Threads);
	for (ULONG i = 0; i < info->NumberOfThreads; i++) {
		auto& t = threads[i].ThreadInfo;
		if (t.ThreadState != KTHREAD_STATE::Waiting || (t.WaitReason != KWAIT_REASON::Suspended && t.WaitReason != KWAIT_REASON::WrSuspended))
			return false;
	}
	return true;
}

void CProcessesView::UpdateSuspended(ProcessItem& item) {
	if (IsSuspended(*item.Process))
		item.Kinds |= ProcessKind::Suspended;
	else
		item.Kinds &= ~ProcessKind::Suspended;
}

bool CProcessesView::UpdateServicePids() {
	std::unordered_set<uint32_t> pids;
	for (auto& svc : ServiceManager::EnumServices(ServiceEnumType::AllServices, ServiceEnumState::Active))
		if (auto pid = svc.GetStatusProcess().ProcessId)
			pids.insert(pid);
	if (pids == m_ServicePids)
		return false;
	m_ServicePids = std::move(pids);
	return true;
}

void CProcessesView::UpdateDotNet(bool all) {
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);
	auto now = (int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart;
	std::vector<ProcessItem*> candidates;
	for (auto& [key, item] : m_Items)
		if (!item.Terminated && (item.Kinds & ProcessKind::DotNet) == ProcessKind::None && item.Process->Id != 0
			&& (all || now - key.Created < DotNetCheckPeriod))
			candidates.push_back(&item);
	if (candidates.empty())
		return;

	auto corePids = DotNetInfo::GetCoreRuntimeProcesses();
	for (auto item : candidates) {
		auto& p = *item->Process;
		if (corePids.contains(p.Id) || DotNetInfo::HasFrameworkRuntime(p.Id, p.NativeInfo->SessionId))
			item->Kinds |= ProcessKind::DotNet;
	}
}

bool CProcessesView::Matches(ProcessItem& item) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	auto& p = *item.Process;
	auto contains = [&](std::wstring const& text) { return CFilterBox::Contains(text, filter); };
	// the cheap ones first; the others are cached in ProcessInfoEx once queried
	return contains(p.GetImageName()) || contains(std::to_wstring(p.Id)) || contains(p.GetUser())
		|| contains(p.GetDescription()) || contains(p.GetCommandLine());
}

void CProcessesView::UpdateFilterStatus() {
	if (!m_Active)
		return;
	if (m_Filter.GetText().empty()) {
		Frame()->SetStatusText(0, L"");
		return;
	}
	auto matches = std::ranges::count_if(m_Items, [](auto& i) { return i.second.Match && !i.second.Terminated; });
	auto total = std::ranges::count_if(m_Items, [](auto& i) { return !i.second.Terminated; });
	Frame()->SetStatusText(0, std::format(L"{} of {} processes match the filter", matches, total).c_str());
}

LRESULT CProcessesView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CProcessesView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a lower pane's page with the focus has a filter of its own
	auto focus = ::GetFocus();
	if (m_LowerPane && focus && m_Details.IsChild(focus)) {
		auto in = [&](CWindow page) { return focus == page.m_hWnd || page.IsChild(focus); };
		if (in(m_Details.GetHandlesPage())) {
			m_Details.GetHandlesPage().FocusFilter();
			return 0;
		}
		if (in(m_Details.GetModulesPage())) {
			m_Details.GetModulesPage().FocusFilter();
			return 0;
		}
		if (in(m_Details.GetThreadsPage())) {
			m_Details.GetThreadsPage().FocusFilter();
			return 0;
		}
	}
	m_Filter.Focus();
	return 0;
}

void CProcessesView::BuildItems() {
	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;

	m_Tree.BeginUpdate();
	m_Tree.DeleteAllItems();
	m_Items.clear();

	auto& processes = Frame()->GetProcessManager().GetProcesses();
	m_Items.reserve(processes.size() + 64);
	for (auto& p : processes)
		m_Items.insert({ p->Key, ProcessItem{ p } });
	InsertItems();

	UpdateServicePids();
	for (auto& [key, item] : m_Items) {
		SetFixedKinds(item);
		UpdateSuspended(item);
		if (m_ServicePids.contains(key.Id))
			item.Kinds |= ProcessKind::Service;
	}
	UpdateDotNet(true);

	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();

	// the list was rebuilt, so the selection is shown wherever it ended up
	RestoreSelection(selectedKey ? &*selectedKey : nullptr, true);
}

void CProcessesView::RebuildTree() {
	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;

	m_Tree.BeginUpdate();
	m_Tree.DeleteAllItems();
	for (auto& [key, item] : m_Items)
		item.Item = nullptr;
	InsertItems();
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();

	RestoreSelection(selectedKey ? &*selectedKey : nullptr, true);
}

void CProcessesView::InsertItems() {
	m_Tree.SetTreeLines(m_TreeMode);
	// in snapshot order (not the map's), so unsorted siblings keep the system's order; parents are inserted first anyway.
	// With a filter, only the matching processes, which bring their ancestors along (EnsureInserted)
	for (auto& p : Frame()->GetProcessManager().GetProcesses()) {
		auto it = m_Items.find(p->Key);
		if (it == m_Items.end())
			continue;
		auto& item = it->second;
		item.Match = Matches(item);
		if (item.Match)
			EnsureInserted(item);
	}
	// exited processes, still shown for this refresh
	for (auto& [key, item] : m_Items)
		if (item.Terminated) {
			item.Match = Matches(item);
			if (item.Match)
				EnsureInserted(item);
		}
}

void CProcessesView::OnRefresh(bool /*active*/) {
	auto& pm = Frame()->GetProcessManager();
	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;

	m_Tree.BeginUpdate();

	// processes marked terminated on the previous update go away now; their NativeInfo points into
	// the snapshot buffer that this update just overwrote, so they must not be painted again
	std::vector<ProcessOrThreadKey> terminated;
	for (auto& [key, item] : m_Items) {
		if (item.Terminated)
			terminated.push_back(key);
		else if (item.NewTicks > 0)
			item.NewTicks--;
	}
	for (auto& key : terminated) {
		if (auto it = m_Items.find(key); it != m_Items.end())
			RemoveItem(it->second);
	}

	for (auto& p : pm.GetTerminatedProcesses()) {
		if (auto it = m_Items.find(p->Key); it != m_Items.end()) {
			it->second.Terminated = true;
			it->second.NewTicks = 0;
		}
	}

	// add all new processes before inserting, so a new parent and child are linked regardless of order
	std::vector<ProcessItem*> added;
	for (auto& p : pm.GetNewProcesses()) {
		auto [it, inserted] = m_Items.insert({ p->Key, ProcessItem{ p } });
		if (inserted) {
			it->second.NewTicks = NewProcessTicks;
			added.push_back(&it->second);
		}
	}
	for (auto item : added) {
		item->Match = Matches(*item);
		if (item->Match)
			EnsureInserted(*item);
		SetFixedKinds(*item);
	}

	// the SCM may record a new service's PID after the snapshot, so the next update looks again too
	if (!added.empty())
		m_ServiceRechecks = 2;
	if (m_ServiceRechecks > 0) {
		m_ServiceRechecks--;
		UpdateServicePids();
		for (auto& [key, item] : m_Items)
			if (!item.Terminated) {
				if (m_ServicePids.contains(key.Id))
					item.Kinds |= ProcessKind::Service;
				else
					item.Kinds &= ~ProcessKind::Service;
			}
	}
	UpdateDotNet(false);

	UpdateHungProcesses();
	bool gpu = ShowsGpu();
	for (auto& [key, item] : m_Items)
		if (!item.Terminated) {
			UpdateDeltas(item);
			UpdateSuspended(item);
			if (gpu)
				item.Gpu = WinSys::GpuInfo::QueryProcess(item.Process->GetQueryInformationHandle(), item.GpuState);
		}

	// values change on every update, so the order may too; a hidden list is sorted when it's shown
	if (m_SortColumn >= 0) {
		if (m_Active)
			Sort();
		else
			m_SortPending = true;
	}
	m_Tree.EndUpdate();

	RestoreSelection(selectedKey ? &*selectedKey : nullptr, m_KeepSelectionVisible);
	UpdateUI();
	if (!m_Filter.GetText().empty())
		UpdateFilterStatus();
	UpdateDetails();
	if (m_LowerPane)
		m_Details.Refresh();
}

HTLITEM CProcessesView::EnsureInserted(ProcessItem& item) {
	if (item.Item)
		return item.Item;

	auto parent = FindParent(item);
	auto hParent = parent ? EnsureInserted(*parent) : TLV_ROOT;
	if (item.Image < 0)
		item.Image = Frame()->GetProcessIcon(*item.Process);
	item.Item = m_Tree.InsertItem(hParent, TLV_LAST, LPSTR_TEXTCALLBACK, item.Image, reinterpret_cast<LPARAM>(&item));
	return item.Item;
}

CProcessesView::ProcessItem* CProcessesView::FindParent(ProcessItem const& item) {
	if (!m_TreeMode)
		return nullptr;

	// a parent must be created before its child; otherwise the parent ID was reused by a newer process
	auto& process = *item.Process;
	auto parent = Frame()->GetProcessManager().GetProcessById(process.ParentId);
	if (!parent || parent->Id == process.Id || parent->Key.Created >= process.Key.Created)
		return nullptr;

	auto it = m_Items.find(parent->Key);
	return it == m_Items.end() ? nullptr : &it->second;
}

void CProcessesView::RemoveItem(ProcessItem& item) {
	if (!item.Item) {
		// filtered out: not in the tree
		if (m_Network)
			m_Network->RemoveProcess(item.Process->Id);
		m_Items.erase(item.Process->Key);
		return;
	}
	// DeleteItem takes the whole subtree; the children move to the top level, since their parent is gone
	auto hParent = m_Tree.GetParentItem(item.Item);
	std::vector<ProcessItem*> descendants;
	CollectDescendants(item.Item, descendants);
	m_Tree.DeleteItem(item.Item);
	for (auto d : descendants)
		d->Item = nullptr;

	if (m_Network)
		m_Network->RemoveProcess(item.Process->Id);
	m_Items.erase(item.Process->Key);
	for (auto d : descendants)
		EnsureInserted(*d);
	RemoveUnneededAncestors(hParent);
}

void CProcessesView::RemoveUnneededAncestors(HTLITEM hItem) {
	// with a filter, an ancestor that doesn't match is in the tree only for its matching descendants: once it has no
	// children left, it goes, and so may its own parent
	while (hItem && hItem != TLV_ROOT && !m_Tree.GetChildItem(hItem)) {
		auto ancestor = reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(hItem));
		if (!ancestor || ancestor->Match)
			break;
		auto hParent = m_Tree.GetParentItem(hItem);
		m_Tree.DeleteItem(hItem);
		ancestor->Item = nullptr;
		hItem = hParent;
	}
}

void CProcessesView::CollectDescendants(HTLITEM hItem, std::vector<ProcessItem*>& items) const {
	for (auto hChild = m_Tree.GetChildItem(hItem); hChild; hChild = m_Tree.GetNextSiblingItem(hChild)) {
		items.push_back(reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(hChild)));
		CollectDescendants(hChild, items);
	}
}

CProcessesView::ProcessItem* CProcessesView::GetSelectedProcess() const {
	auto hItem = m_Tree.GetSelectedItem();
	return hItem ? reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(hItem)) : nullptr;
}

void CProcessesView::RestoreSelection(ProcessOrThreadKey const* key, bool ensureVisible) {
	// the list view keeps the selection by index, which inserts, deletes and sorting shift
	if (key) {
		if (auto it = m_Items.find(*key); it != m_Items.end() && it->second.Item) {
			m_Tree.SelectItem(it->second.Item, ensureVisible);
			return;
		}
	}
	m_Tree.DeselectAll();
}

LRESULT CProcessesView::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto& item = *reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(di->hItem));
	if (di->iSubItem < (int)m_Columns.size())
		wcsncpy_s(di->pszText, di->cchTextMax, GetColumnText(item, m_Columns[di->iSubItem]).c_str(), _TRUNCATE);
	return 0;
}

std::wstring CProcessesView::GetColumnText(ProcessItem& item, Column column) const {
	auto& p = *item.Process;
	auto info = p.NativeInfo;

	switch (column) {
		case Column::Name: return p.GetImageName();
		case Column::Id: return std::to_wstring(p.Id);
		case Column::Status:
			if (item.Terminated)
				return L"";
			switch (GetStatus(item)) {
				case ProcessStatus::Suspended: return L"Suspended";
				case ProcessStatus::NotResponding: return L"Not responding";
			}
			return L"Running";
		case Column::CPU: return p.Id == 0 || !item.Terminated ? FormatHelper::FormatCpu(p.CPU) : L"";
		case Column::CpuTime: return FormatHelper::FormatTimeSpan(info->KernelTime.QuadPart + info->UserTime.QuadPart);
		case Column::KernelTime: return FormatHelper::FormatTimeSpan(info->KernelTime.QuadPart);
		case Column::UserTime: return FormatHelper::FormatTimeSpan(info->UserTime.QuadPart);
		case Column::CpuCycles: return FormatHelper::FormatNumber(info->CycleTime);
		case Column::ContextSwitches: return FormatHelper::FormatNumber(GetContextSwitches(p));
		case Column::ContextSwitchDelta: return item.ContextSwitchDelta ? FormatHelper::FormatNumber(item.ContextSwitchDelta) : L"";
		case Column::PrivateBytesDelta:
			// in KB, like Private Bytes; rounded towards zero, so changes under 1 KB show as nothing
			return item.PrivateBytesDelta / 1024 ? FormatHelper::FormatNumber(item.PrivateBytesDelta / 1024) + L" K" : L"";
		case Column::PeakPrivateBytes: return FormatHelper::FormatSize(info->PeakPagefileUsage);
		case Column::PeakWorkingSet: return FormatHelper::FormatSize(info->PeakWorkingSetSize);
		case Column::VirtualSize: return FormatHelper::FormatSize(info->VirtualSize);
		case Column::PageFaults: return FormatHelper::FormatNumber(info->PageFaultCount);
		case Column::PeakThreads: return FormatHelper::FormatNumber(info->NumberOfThreadsHighWatermark);
		case Column::GdiObjects: {
			auto count = p.GetGdiObjects();
			return count > 0 ? FormatHelper::FormatNumber(count) : L"";
		}
		case Column::UserObjects: {
			auto count = p.GetUserObjects();
			return count > 0 ? FormatHelper::FormatNumber(count) : L"";
		}
		case Column::MemoryPriority: {
			auto priority = p.GetMemoryPriority();
			return priority >= 0 ? std::to_wstring(priority) : L"";
		}
		case Column::IoPriority: return FormatHelper::IoPriorityToString(p.GetIoPriority());
		case Column::IntegrityLevel: return FormatHelper::IntegrityLevelToString(p.GetIntegrityLevel());
		case Column::Protection: return FormatHelper::ProtectionToString(p.GetProtection());
		case Column::PrivateBytes: return FormatHelper::FormatSize(info->PrivatePageCount);
		case Column::WorkingSet: return FormatHelper::FormatSize(info->WorkingSetSize);
		case Column::Threads: return FormatHelper::FormatNumber(info->NumberOfThreads);
		case Column::Handles: return FormatHelper::FormatNumber(info->HandleCount);
		case Column::Session: return p.Id == 0 ? L"" : std::to_wstring(info->SessionId);
		case Column::User: return p.GetUser();
		case Column::Priority: return FormatHelper::FormatPriority(info->BasePriority);
		case Column::StartTime: return FormatHelper::FormatDateTime(p.Key.Created);
		case Column::Architecture: return p.Id == 0 ? L"" : p.GetArchitecture();
		case Column::Virtualization:
			switch (p.GetVirtualization()) {
				case VirtualizationState::NotAllowed: return L"Not allowed";
				case VirtualizationState::Enabled: return L"Enabled";
				case VirtualizationState::Disabled: return L"Disabled";
			}
			return L"";
		case Column::JobId: {
			auto job = GetJobId(p);
			// without the snapshot's job ID, only membership is known
			return job == 0 ? L"" : p.ExtendedInfo ? std::to_wstring(job) : L"Yes";
		}
		case Column::IoRead: return FormatHelper::FormatBytes(info->ReadTransferCount.QuadPart);
		case Column::IoWrite: return FormatHelper::FormatBytes(info->WriteTransferCount.QuadPart);
		case Column::IoOther: return FormatHelper::FormatBytes(info->OtherTransferCount.QuadPart);
		case Column::NetworkSend: return FormatHelper::FormatBytes(NetworkCounters(p.Id).Sent);
		case Column::NetworkReceive: return FormatHelper::FormatBytes(NetworkCounters(p.Id).Received);
		// blank when not known (the process can't be queried) or idle, as the CPU column
		case Column::GpuUsage: return item.Gpu.Known && item.Gpu.Usage >= 0.005f ? std::format(L"{:.2f}", item.Gpu.Usage) : L"";
		case Column::GpuDedicated: return item.Gpu.Known && item.Gpu.Dedicated ? FormatHelper::FormatSize((int64_t)item.Gpu.Dedicated) : L"";
		case Column::GpuShared: return item.Gpu.Known && item.Gpu.Shared ? FormatHelper::FormatSize((int64_t)item.Gpu.Shared) : L"";
		case Column::Description: return p.GetDescription();
		case Column::ImagePath: return p.GetImagePath();
		case Column::CommandLine: return p.GetCommandLine();
	}
	return L"";
}

LRESULT CProcessesView::OnSort(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ns = reinterpret_cast<NMTLVSORT*>(hdr);
	m_SortColumn = (int)m_Columns[ns->iSubItem];
	m_SortAscending = ns->iDirection > 0;
	SaveSort();

	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;
	Sort();
	// the user re-sorted: follow the selection to its new place
	RestoreSelection(selectedKey ? &*selectedKey : nullptr, true);
	return 0;
}

void CProcessesView::LoadSort() {
	// "Key:asc" or "Key:desc", the key from s_Columns
	std::wstring text = AppSettings::Get().ProcessSort();
	auto colon = text.find(L':');
	if (colon == std::wstring::npos)
		return;
	auto key = text.substr(0, colon);
	auto it = std::ranges::find_if(s_Columns, [&](auto& c) { return key == c.Key; });
	if (it == std::end(s_Columns))
		return;
	m_SortColumn = (int)(it - std::begin(s_Columns));
	m_SortAscending = text.substr(colon + 1) != L"desc";
}

void CProcessesView::SaveSort() const {
	auto& settings = AppSettings::Get();
	settings.ProcessSort(m_SortColumn < 0 ? L"" : std::format(L"{}:{}", s_Columns[m_SortColumn].Key, m_SortAscending ? L"asc" : L"desc"));
	settings.Save();
}

void CProcessesView::Sort() {
	m_SortPending = false;
	// a queried value once per item, not twice per comparison
	if (auto column = (Column)m_SortColumn; m_SortColumn >= 0 && IsQueriedColumn(column))
		for (auto& [key, item] : m_Items)
			if (item.Item)
				item.SortValue = GetQueriedValue(item, column);
	// in tree mode this sorts siblings; in flat mode everything is top level, so it's a full sort
	m_Tree.SortChildren(TLV_ROOT, CompareItems, reinterpret_cast<LPARAM>(this), true);
}

bool CProcessesView::IsQueriedColumn(Column column) {
	switch (column) {
		case Column::ContextSwitches: case Column::GdiObjects: case Column::UserObjects: case Column::MemoryPriority:
		case Column::IoPriority: case Column::NetworkSend: case Column::NetworkReceive:
			return true;
	}
	return false;
}

int64_t CProcessesView::GetQueriedValue(ProcessItem& item, Column column) const {
	auto& p = *item.Process;
	switch (column) {
		case Column::ContextSwitches: return (int64_t)GetContextSwitches(p);
		case Column::GdiObjects: return p.GetGdiObjects();
		case Column::UserObjects: return p.GetUserObjects();
		case Column::MemoryPriority: return p.GetMemoryPriority();
		case Column::IoPriority: return (int64_t)p.GetIoPriority();
		case Column::NetworkSend: return (int64_t)NetworkCounters(p.Id).Sent;
		case Column::NetworkReceive: return (int64_t)NetworkCounters(p.Id).Received;
	}
	return 0;
}

int CALLBACK CProcessesView::CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam) {
	auto view = reinterpret_cast<CProcessesView*>(lParam);
	auto& item1 = *reinterpret_cast<ProcessItem*>(view->m_Tree.GetItemData(h1));
	auto& item2 = *reinterpret_cast<ProcessItem*>(view->m_Tree.GetItemData(h2));
	auto result = view->CompareProcesses(item1, item2);
	return view->m_SortAscending ? result : -result;
}

int CProcessesView::CompareProcesses(ProcessItem& item1, ProcessItem& item2) const {
	auto& p1 = *item1.Process;
	auto& p2 = *item2.Process;
	auto i1 = p1.NativeInfo, i2 = p2.NativeInfo;

	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	auto compareText = [](std::wstring const& a, std::wstring const& b) { return ::_wcsicmp(a.c_str(), b.c_str()); };

	if (IsQueriedColumn((Column)m_SortColumn))
		return compare(item1.SortValue, item2.SortValue);
	switch ((Column)m_SortColumn) {
		case Column::Name: return compareText(p1.GetImageName(), p2.GetImageName());
		case Column::Id: return compare(p1.Id, p2.Id);
		case Column::Status: return compare(GetStatus(item1), GetStatus(item2));
		case Column::CPU: return compare(p1.CPU, p2.CPU);
		case Column::CpuTime: return compare(i1->KernelTime.QuadPart + i1->UserTime.QuadPart, i2->KernelTime.QuadPart + i2->UserTime.QuadPart);
		case Column::KernelTime: return compare(i1->KernelTime.QuadPart, i2->KernelTime.QuadPart);
		case Column::UserTime: return compare(i1->UserTime.QuadPart, i2->UserTime.QuadPart);
		case Column::CpuCycles: return compare(i1->CycleTime, i2->CycleTime);
		case Column::ContextSwitchDelta: return compare(item1.ContextSwitchDelta, item2.ContextSwitchDelta);
		case Column::PrivateBytesDelta: return compare(item1.PrivateBytesDelta, item2.PrivateBytesDelta);
		case Column::PeakPrivateBytes: return compare(i1->PeakPagefileUsage, i2->PeakPagefileUsage);
		case Column::PeakWorkingSet: return compare(i1->PeakWorkingSetSize, i2->PeakWorkingSetSize);
		case Column::VirtualSize: return compare(i1->VirtualSize, i2->VirtualSize);
		case Column::PageFaults: return compare(i1->PageFaultCount, i2->PageFaultCount);
		case Column::PeakThreads: return compare(i1->NumberOfThreadsHighWatermark, i2->NumberOfThreadsHighWatermark);
		case Column::IntegrityLevel: return compare(p1.GetIntegrityLevel(), p2.GetIntegrityLevel());
		case Column::Protection: return compare(p1.GetProtection().Level, p2.GetProtection().Level);
		case Column::PrivateBytes: return compare(i1->PrivatePageCount, i2->PrivatePageCount);
		case Column::WorkingSet: return compare(i1->WorkingSetSize, i2->WorkingSetSize);
		case Column::Threads: return compare(i1->NumberOfThreads, i2->NumberOfThreads);
		case Column::Handles: return compare(i1->HandleCount, i2->HandleCount);
		case Column::Session: return compare(i1->SessionId, i2->SessionId);
		case Column::User: return compareText(p1.GetUser(), p2.GetUser());
		case Column::Priority: return compare(i1->BasePriority, i2->BasePriority);
		case Column::StartTime: return compare(p1.Key.Created, p2.Key.Created);
		case Column::Architecture: return ::_wcsicmp(p1.GetArchitecture(), p2.GetArchitecture());
		case Column::Virtualization: return compare(p1.GetVirtualization(), p2.GetVirtualization());
		case Column::JobId: return compare(GetJobId(p1), GetJobId(p2));
		case Column::IoRead: return compare(i1->ReadTransferCount.QuadPart, i2->ReadTransferCount.QuadPart);
		case Column::IoWrite: return compare(i1->WriteTransferCount.QuadPart, i2->WriteTransferCount.QuadPart);
		case Column::IoOther: return compare(i1->OtherTransferCount.QuadPart, i2->OtherTransferCount.QuadPart);
		case Column::GpuUsage: return compare(item1.Gpu.Usage, item2.Gpu.Usage);
		case Column::GpuDedicated: return compare(item1.Gpu.Dedicated, item2.Gpu.Dedicated);
		case Column::GpuShared: return compare(item1.Gpu.Shared, item2.Gpu.Shared);
		case Column::Description: return compareText(p1.GetDescription(), p2.GetDescription());
		case Column::ImagePath: return compareText(p1.GetImagePath(), p2.GetImagePath());
		case Column::CommandLine: return compareText(p1.GetCommandLine(), p2.GetCommandLine());
	}
	return 0;
}

LRESULT CProcessesView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}

	auto cd = reinterpret_cast<NMTLVCUSTOMDRAW*>(hdr);
	if ((cd->nmcd.dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT && cd->hItem) {
		auto& item = *reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(cd->hItem));
		bool dark = WTLHelper::IsDarkMode();
		auto kinds = item.Kinds;
		if (item.Terminated)
			kinds |= ProcessKind::Exited;
		else if (item.NewTicks > 0)
			kinds |= ProcessKind::New;
		if (auto color = ProcessColors::Find(m_Colors, kinds)) {
			cd->clrTextBk = dark ? color->Dark : color->Light;
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
		}
		else if (!item.Match)
			// shown only as the ancestor of a process that matches the filter
			cd->clrText = dark ? RGB(128, 128, 128) : RGB(140, 140, 140);
	}
	return CDRF_DODEFAULT;
}

LRESULT CProcessesView::OnSelectionChanged(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	UpdateUI();
	if (m_LowerPane)
		SetTimer(DetailsTimerId, DetailsDelay);
	return 0;
}

LRESULT CProcessesView::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != DetailsTimerId) {
		bHandled = FALSE;
		return 0;
	}
	KillTimer(DetailsTimerId);
	UpdateDetails();
	return 0;
}

void CProcessesView::UpdateDetails() {
	// nothing to keep up to date while the pane is hidden; it catches up when shown (OnToggleLowerPane)
	if (!m_LowerPane)
		return;
	auto item = GetSelectedProcess();
	m_Details.SetProcess(item && !item->Terminated ? item->Process : nullptr);
}

LRESULT CProcessesView::OnToggleLowerPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_LowerPane = !m_LowerPane;
	m_Splitter.SetSinglePaneMode(m_LowerPane ? SPLIT_PANE_NONE : SPLIT_PANE_TOP);
	if (m_LowerPane) {
		UpdateDetails();
		m_Details.Refresh();
	}
	auto& settings = AppSettings::Get();
	settings.ProcessLowerPane(m_LowerPane);
	settings.Save();
	UpdateUI();
	return 0;
}

bool CProcessesView::CanControl(ProcessItem const* item) {
	// not Idle and System, and not a process that's gone
	return item && !item->Terminated && item->Process->Id > 4;
}

void CProcessesView::UpdateUI() {
	// the frame's commands (menu, toolbar) that this view handles; the context menu has its own state (OnRightClick)
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_VIEW_PROCESSTREE, m_Active);
	auto item = GetSelectedProcess();
	bool live = m_Active && CanControl(item);
	for (UINT id : { ID_PROCESS_KILL, ID_PROCESS_KILLALL, ID_PROCESS_SUSPEND, ID_PROCESS_RESUME, ID_PROCESS_MINIDUMP, ID_PROCESS_FULLDUMP, ID_PROCESS_WAITCHAIN })
		ui.UIEnable(id, live);
	ui.UIEnable(ID_PROCESS_GOTOFILELOCATION, m_Active && item && !item->Process->GetImagePath().empty());
	// the Services view has Properties too (its OnFrameIdle)
	if (m_Active)
		ui.UIEnable(ID_VIEW_PROPERTIES, item && !item->Terminated);
	for (int i = 0; i < _countof(s_Priorities); i++) {
		ui.UIEnable(ID_PRIORITY_REALTIME + i, live);
		ui.UISetRadio(ID_PRIORITY_REALTIME + i, false);
		ui.UISetCheck(ID_PRIORITY_REALTIME + i, false);
	}
	// the radio check of a menu item goes with its range
	if (live)
		for (int i = 0; i < _countof(s_Priorities); i++)
			if (s_Priorities[i].BasePriority == item->Process->NativeInfo->BasePriority)
				ui.UISetRadioMenuItem(ID_PRIORITY_REALTIME + i, ID_PRIORITY_REALTIME, ID_PRIORITY_IDLE);
	// the I/O and memory priorities and the efficiency mode are queried (through ProcessInfoEx's handle)
	int io = live ? (int)item->Process->GetIoPriority() : -1, memory = live ? item->Process->GetMemoryPriority() : -1;
	for (UINT id = ID_IOPRIORITY_VERYLOW; id <= ID_MEMPRIORITY_NORMAL; id++) {
		ui.UIEnable(id, live);
		ui.UISetRadio(id, false);
		ui.UISetCheck(id, false);
	}
	if (io >= 0 && io <= ID_IOPRIORITY_HIGH - ID_IOPRIORITY_VERYLOW)
		ui.UISetRadioMenuItem(ID_IOPRIORITY_VERYLOW + io, ID_IOPRIORITY_VERYLOW, ID_IOPRIORITY_HIGH);
	if (memory >= 1 && memory <= ID_MEMPRIORITY_NORMAL - ID_MEMPRIORITY_VERYLOW + 1)
		ui.UISetRadioMenuItem(ID_MEMPRIORITY_VERYLOW + memory - 1, ID_MEMPRIORITY_VERYLOW, ID_MEMPRIORITY_NORMAL);
	ui.UIEnable(ID_PROCESS_AFFINITY, live);
	ui.UIEnable(ID_PROCESS_EFFICIENCY, live);
	ui.UISetCheck(ID_PROCESS_EFFICIENCY, live && item->Process->GetEfficiencyMode().value_or(false));
	ui.UIEnable(ID_VIEW_LOWERPANE, m_Active);
	ui.UIEnable(ID_VIEW_KEEPSELECTIONVISIBLE, m_Active);
	ui.UIEnable(ID_KILL, m_Active && CanKill());
	if (m_Active) {
		ui.UISetCheck(ID_VIEW_KEEPSELECTIONVISIBLE, m_KeepSelectionVisible);
		ui.UISetCheck(ID_VIEW_PROCESSTREE, m_TreeMode);
		ui.UISetCheck(ID_VIEW_LOWERPANE, m_LowerPane);
	}
}

LRESULT CProcessesView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
	if (ia->iItem < 0)
		return 0;
	// ptAction is relative to the inner list view
	CPoint pt(ia->ptAction);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowRowMenu(pt);
	return 0;
}

void CProcessesView::ShowRowMenu(CPoint pt) {
	auto item = GetSelectedProcess();
	if (!item)
		return;

	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CMenuHandle popup = menu.GetSubMenu(0);
	// as in the Process menu and the thread context menu
	MenuItemData const icons[] = {
		{ ID_PROCESS_KILL, IDI_DELETE },
		{ ID_PROCESS_SUSPEND, IDI_PAUSE },
		{ ID_PROCESS_RESUME, IDI_RUN },
	};
	WTLHelper::InitMenu(popup, icons, _countof(icons), DpiHelper::SmallIconSize(m_hWnd));

	bool live = CanControl(item);
	for (UINT id : { ID_PROCESS_KILL, ID_PROCESS_KILLALL, ID_PROCESS_SUSPEND, ID_PROCESS_RESUME, ID_PROCESS_MINIDUMP, ID_PROCESS_FULLDUMP, ID_PROCESS_WAITCHAIN })
		popup.EnableMenuItem(id, MF_BYCOMMAND | (live ? MF_ENABLED : MF_GRAYED));
	popup.EnableMenuItem(ID_PROCESS_GOTOFILELOCATION, MF_BYCOMMAND | (item->Process->GetImagePath().empty() ? MF_GRAYED : MF_ENABLED));
	popup.EnableMenuItem(ID_VIEW_PROPERTIES, MF_BYCOMMAND | (item->Terminated ? MF_GRAYED : MF_ENABLED));
	popup.SetMenuDefaultItem(ID_VIEW_PROPERTIES);
	for (int i = 0; i < _countof(s_Priorities); i++) {
		popup.EnableMenuItem(ID_PRIORITY_REALTIME + i, MF_BYCOMMAND | (live ? MF_ENABLED : MF_GRAYED));
		if (s_Priorities[i].BasePriority == item->Process->NativeInfo->BasePriority)
			popup.CheckMenuRadioItem(ID_PRIORITY_REALTIME, ID_PRIORITY_IDLE, ID_PRIORITY_REALTIME + i, MF_BYCOMMAND);
	}
	for (UINT id = ID_IOPRIORITY_VERYLOW; id <= ID_PROCESS_EFFICIENCY; id++)
		popup.EnableMenuItem(id, MF_BYCOMMAND | (live ? MF_ENABLED : MF_GRAYED));
	if (live) {
		int io = (int)item->Process->GetIoPriority(), memory = item->Process->GetMemoryPriority();
		if (io >= 0 && io <= ID_IOPRIORITY_HIGH - ID_IOPRIORITY_VERYLOW)
			popup.CheckMenuRadioItem(ID_IOPRIORITY_VERYLOW, ID_IOPRIORITY_HIGH, ID_IOPRIORITY_VERYLOW + io, MF_BYCOMMAND);
		if (memory >= 1 && memory <= ID_MEMPRIORITY_NORMAL - ID_MEMPRIORITY_VERYLOW + 1)
			popup.CheckMenuRadioItem(ID_MEMPRIORITY_VERYLOW, ID_MEMPRIORITY_NORMAL, ID_MEMPRIORITY_VERYLOW + memory - 1, MF_BYCOMMAND);
		popup.CheckMenuItem(ID_PROCESS_EFFICIENCY, MF_BYCOMMAND | (item->Process->GetEfficiencyMode().value_or(false) ? MF_CHECKED : MF_UNCHECKED));
	}

	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
}

void CProcessesView::ShowError(PCWSTR operation) const {
	auto error = ::GetLastError();
	AtlMessageBox(m_hWnd, std::format(L"{} failed: {}", operation, Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

LRESULT CProcessesView::OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	// held: refreshes go on while the box is open, and may remove the item
	auto info = item->Process;
	auto& p = *info;
	auto text = std::format(L"Terminate process {} ({})?", p.GetImageName(), p.Id);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;

	auto process = Process::OpenById(p.Id, p.Key.Created, ProcessAccessMask::Terminate);
	if (!process) {
		ShowError(L"Opening the process");
		return 0;
	}
	if (!process->Terminate())
		ShowError(L"Terminating the process");
	return 0;
}

CProcessesView::KillTarget CProcessesView::GetKillTarget() const {
	// the lower pane's pages, while it's shown; anything else in the view (the tree) is the process
	auto focus = ::GetFocus();
	if (m_LowerPane && focus && (focus == m_Details.m_hWnd || m_Details.IsChild(focus))) {
		auto& details = const_cast<CProcessDetails&>(m_Details);
		auto& threads = details.GetThreadsPage();
		if (focus == threads.m_hWnd || threads.IsChild(focus))
			return KillTarget::Thread;
		auto& handles = details.GetHandlesPage();
		if (focus == handles.m_hWnd || handles.IsChild(focus))
			return KillTarget::Handle;
		// the Modules page, or the tabs
		return KillTarget::None;
	}
	return KillTarget::Process;
}

bool CProcessesView::CanKill() const {
	auto& details = const_cast<CProcessDetails&>(m_Details);
	switch (GetKillTarget()) {
		case KillTarget::Process: return CanControl(GetSelectedProcess());
		case KillTarget::Thread: return details.GetThreadsPage().CanTerminateSelected();
		case KillTarget::Handle: return details.GetHandlesPage().CanCloseSelected();
	}
	return false;
}

std::wstring CProcessesView::GetCommandTip(UINT id) const {
	if (id != ID_KILL)
		return {};
	switch (GetKillTarget()) {
		case KillTarget::Process: return L"Kill Process (Del)";
		case KillTarget::Thread: return L"Terminate Thread (Del)";
		case KillTarget::Handle: return L"Close Handle (Del)";
	}
	return L"Kill (Del)";
}

void CProcessesView::OnFrameIdle() {
	Frame()->GetUI().UIEnable(ID_KILL, m_Active && CanKill());
}

LRESULT CProcessesView::OnKillFocused(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& bHandled) {
	if (!CanKill())
		return 0;
	switch (GetKillTarget()) {
		case KillTarget::Process: return OnKill(0, ID_PROCESS_KILL, nullptr, bHandled);
		case KillTarget::Thread: m_Details.GetThreadsPage().TerminateSelected(); break;
		case KillTarget::Handle: m_Details.GetHandlesPage().CloseSelected(); break;
	}
	return 0;
}

LRESULT CProcessesView::OnKillAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!CanControl(item))
		return 0;

	// image names compare case-insensitively
	auto name = item->Process->GetImageName();
	std::vector<ProcessOrThreadKey> pids;
	for (auto& [key, i] : m_Items)
		if (CanControl(&i) && ::_wcsicmp(i.Process->GetImageName().c_str(), name.c_str()) == 0)
			pids.push_back(i.Process->Key);

	auto text = std::format(L"Terminate {} process{} named {}?", pids.size(), pids.size() == 1 ? L"" : L"es", name);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;

	int failed = 0;
	DWORD error = ERROR_SUCCESS;
	for (auto pid : pids) {
		auto process = Process::OpenById(pid.Id, pid.Created, ProcessAccessMask::Terminate);
		if (!process || !process->Terminate()) {
			if (!failed++)
				error = ::GetLastError();
		}
	}
	if (failed)
		AtlMessageBox(m_hWnd, std::format(L"Failed to terminate {} of {} processes: {}", failed, pids.size(), Helpers::GetErrorText(error)).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
	return 0;
}

LRESULT CProcessesView::OnSuspend(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	auto process = Process::OpenById(item->Process->Id, item->Process->Key.Created, ProcessAccessMask::SuspendResume);
	if (!process)
		ShowError(L"Opening the process");
	else if (!process->Suspend())
		ShowError(L"Suspending the process");
	return 0;
}

LRESULT CProcessesView::OnResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	auto process = Process::OpenById(item->Process->Id, item->Process->Key.Created, ProcessAccessMask::SuspendResume);
	if (!process)
		ShowError(L"Opening the process");
	else if (!process->Resume())
		ShowError(L"Resuming the process");
	return 0;
}

LRESULT CProcessesView::OnSetPriority(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	auto process = Process::OpenById(item->Process->Id, item->Process->Key.Created, ProcessAccessMask::SetInformation);
	if (!process)
		ShowError(L"Opening the process");
	else if (!process->SetPriorityClass(s_Priorities[id - ID_PRIORITY_REALTIME].PriorityClass))
		ShowError(L"Setting the priority");
	return 0;
}

std::unique_ptr<Process> CProcessesView::OpenSelected(ProcessAccessMask access) const {
	auto item = GetSelectedProcess();
	if (!CanControl(item))
		return {};
	auto process = Process::OpenById(item->Process->Id, item->Process->Key.Created, access);
	if (!process)
		ShowError(L"Opening the process");
	return process;
}

LRESULT CProcessesView::OnSetIoPriority(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto process = OpenSelected(ProcessAccessMask::SetInformation);
	if (!process)
		return 0;
	// the commands are in IoPriority order, from very low
	if (!process->SetIoPriority(IoPriority(wID - ID_IOPRIORITY_VERYLOW)))
		ShowError(L"Setting the I/O priority");
	UpdateUI();
	return 0;
}

LRESULT CProcessesView::OnSetMemoryPriority(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto process = OpenSelected(ProcessAccessMask::SetInformation);
	if (!process)
		return 0;
	// MEMORY_PRIORITY_VERY_LOW (1) to MEMORY_PRIORITY_NORMAL (5)
	if (!process->SetMemoryPriority(wID - ID_MEMPRIORITY_VERYLOW + 1))
		ShowError(L"Setting the memory priority");
	UpdateUI();
	return 0;
}

LRESULT CProcessesView::OnSetAffinity(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto process = OpenSelected(ProcessAccessMask::SetInformation);
	if (!process)
		return 0;
	if (auto groups = WinSys::SystemInfo::GetProcessorGroups(); groups.size() > 1) {
		SetGroupsAffinity(*process, groups);
		return 0;
	}
	DWORD_PTR mask, systemMask;
	if (!process->GetAffinity(mask, systemMask)) {
		if (::GetLastError() == ERROR_NOT_SUPPORTED)
			// its threads are in more than one processor group: there's no single mask
			AtlMessageBox(m_hWnd, L"The process runs in more than one processor group; its affinity can't be set as one mask.",
				IDR_MAINFRAME, MB_ICONINFORMATION);
		else
			ShowError(L"Getting the affinity");
		return 0;
	}
	auto& p = *GetSelectedProcess()->Process;
	CAffinityDlg dlg(std::format(L"{} ({})", p.GetImageName(), p.Id), mask, systemMask);
	if (dlg.DoModal(m_hWnd) == IDOK && dlg.GetMask() != mask && !process->SetAffinity(dlg.GetMask()))
		ShowError(L"Setting the affinity");
	return 0;
}

void CProcessesView::SetGroupsAffinity(WinSys::Process& process, std::vector<WinSys::SystemInfo::ProcessorGroup> const& groups) {
	// a process with an affinity mask is held to that mask's group; else it may run in every group (Windows 11 on),
	// and its default CPU sets restrict it
	DWORD_PTR mask = 0, systemMask = 0;
	bool held = process.GetAffinity(mask, systemMask);
	uint16_t heldGroup = 0;
	if (held) {
		auto in = process.GetGroups();
		heldGroup = in.empty() ? 0 : in[0];
	}
	auto cpuSets = held ? std::nullopt : process.GetDefaultCpuSetMasks();
	if (!held && !cpuSets) {
		ShowError(L"Getting the default CPU sets");
		return;
	}
	std::vector<CAffinityDlg::Group> choices;
	int first = 0;
	for (auto& g : groups) {
		CAffinityDlg::Group choice{ g.Group, g.ActiveMask, g.ActiveMask, !held || g.Group == heldGroup, first };
		first += g.ActiveCount;
		if (held)
			choice.Selected = g.Group == heldGroup ? mask & g.ActiveMask : 0;
		else if (!cpuSets->empty()) {
			// none set for a group: none of its processors
			auto it = std::ranges::find(*cpuSets, g.Group, &GROUP_AFFINITY::Group);
			choice.Selected = it == cpuSets->end() ? 0 : it->Mask & g.ActiveMask;
		}
		choices.push_back(choice);
	}
	auto note = held ? std::format(L"The process is held to group {} by its affinity mask, which is what OK sets.", heldGroup)
		: std::wstring(L"The process may run in every group; OK sets the processors as its default CPU sets.");
	auto& p = *GetSelectedProcess()->Process;
	CAffinityDlg dlg(std::format(L"{} ({})", p.GetImageName(), p.Id), choices, false, note);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return;
	auto& chosen = dlg.GetGroups();
	if (held) {
		auto it = std::ranges::find(chosen, heldGroup, &CAffinityDlg::Group::Number);
		if (it != chosen.end() && it->Selected != mask && !process.SetAffinity(it->Selected))
			ShowError(L"Setting the affinity");
		return;
	}
	// every processor: none set
	std::vector<GROUP_AFFINITY> masks;
	bool all = true;
	for (auto& g : chosen) {
		all &= g.Selected == g.Available;
		if (g.Selected)
			masks.push_back({ g.Selected, g.Number });
	}
	if (all)
		masks.clear();
	if (!process.SetDefaultCpuSetMasks(masks))
		ShowError(L"Setting the default CPU sets");
}

LRESULT CProcessesView::OnEfficiencyMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	auto process = OpenSelected(ProcessAccessMask::SetInformation);
	if (!process)
		return 0;
	// as Task Manager does it (WinSys): EcoQoS and the idle priority class; off, the system decides, at normal priority
	if (!process->SetEfficiencyMode(!item->Process->GetEfficiencyMode().value_or(false)))
		ShowError(L"Setting the efficiency mode");
	UpdateUI();
	return 0;
}

LRESULT CProcessesView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// with the lower pane's Handles page focused: the handle's
	if (GetKillTarget() == KillTarget::Handle) {
		m_Details.GetHandlesPage().ShowProperties();
		return 0;
	}
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	// the sheet runs on its own thread: it gets copies, not the snapshot
	auto& p = *item->Process;
	ProcessProperties props;
	props.Id = p.Id;
	props.CreateTime = p.Key.Created;
	props.ParentId = p.ParentId;
	props.SessionId = p.NativeInfo->SessionId;
	props.ImageName = p.GetImageName();
	props.ImagePath = p.GetImagePath();
	props.CommandLine = p.GetCommandLine();
	props.User = p.GetUser();
	props.Description = p.GetDescription();
	props.ThreadColumns = AppSettings::Get().ThreadColumns();
	props.ModuleColumns = AppSettings::Get().ModuleColumns();
	props.HandleColumns = AppSettings::Get().HandleColumns();
	props.UpdateInterval = AppSettings::Get().UpdateInterval();
	// the parent, if it's still the process that created this one
	if (auto parent = Frame()->GetProcessManager().GetProcessById(p.ParentId); parent && parent->Key.Created <= p.Key.Created)
		props.ParentName = parent->GetImageName();
	ShowProcessProperties(std::move(props));
	return 0;
}

LRESULT CProcessesView::OnDoubleClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// the commands are in ALT_MSG_MAP(1), which only the frame routes to
	BOOL handled;
	if (reinterpret_cast<NMITEMACTIVATE*>(hdr)->iItem >= 0)
		OnProperties(0, ID_VIEW_PROPERTIES, nullptr, handled);
	return 0;
}

LRESULT CProcessesView::OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Process->GetImagePath().empty())
		return 0;

	auto args = std::format(L"/select,\"{}\"", item->Process->GetImagePath());
	::ShellExecute(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CProcessesView::OnWaitChain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!CanControl(item))
		return 0;
	// the threads from the snapshot (SYSTEM_EXTENDED_THREAD_INFORMATION, as in GetContextSwitches)
	auto& p = *item->Process;
	auto threads = reinterpret_cast<SYSTEM_EXTENDED_THREAD_INFORMATION const*>(p.NativeInfo->Threads);
	std::vector<uint32_t> tids;
	for (ULONG i = 0; i < p.NativeInfo->NumberOfThreads; i++)
		tids.push_back(HandleToULong(threads[i].ThreadInfo.ClientId.UniqueThread));
	CWaitChainDlg dlg(p.Id, p.GetImageName(), std::move(tids));
	dlg.DoModal(m_hWnd);
	return 0;
}

LRESULT CProcessesView::OnCreateDump(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!CanControl(item))
		return 0;
	// held: refreshes go on while the file dialog is open, and may remove the item
	auto info = item->Process;
	auto& p = *info;
	bool full = wID == ID_PROCESS_FULLDUMP;

	// name_pid_date_time.dmp
	SYSTEMTIME now;
	::GetLocalTime(&now);
	auto name = p.GetImageName();
	if (auto dot = name.rfind(L'.'); dot != std::wstring::npos)
		name.erase(dot);
	auto fileName = std::format(L"{}_{}_{:04}{:02}{:02}_{:02}{:02}{:02}{}.dmp", name, p.Id, now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond, full ? L"_full" : L"");
	COMDLG_FILTERSPEC const types[] = { { L"Dump Files", L"*.dmp" }, { L"All Files", L"*.*" } };
	SuspendResumeHook sr;
	CShellFileSaveDialog dlg(fileName.c_str(), FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST, L"dmp", types, _countof(types));
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	CString path;
	if (FAILED(dlg.GetFilePath(path)))
		return 0;

	// written on the symbol engine's thread (DbgHelp's): a full dump of a large process takes a while
	Frame()->SetStatusText(0, std::format(L"Writing a {} of {} ({})...", full ? L"full dump" : L"minidump", p.GetImageName(), p.Id).c_str());
	SymbolEngine::Get().WriteDump(p.Id, p.Key.Created, (PCWSTR)path, full, m_hWnd);
	return 0;
}

LRESULT CProcessesView::OnDumpWritten(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	std::unique_ptr<DumpResult> result(reinterpret_cast<DumpResult*>(lParam));
	// the status line is the filter's otherwise
	UpdateFilterStatus();
	if (!result->Error.empty()) {
		AtlMessageBox(m_hWnd, result->Error.c_str(), IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	auto text = std::format(L"The dump was written to {} ({}).\n\nOpen its folder?", result->Path, FormatHelper::FormatBytes(result->Size));
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
		::ShellExecute(nullptr, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", result->Path).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CProcessesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item)
		return 0;

	// the shown columns in display order, tab separated
	std::wstring text;
	for (auto& c : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetColumnText(*item, c.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CProcessesView::OnToggleTree(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_TreeMode = !m_TreeMode;
	AppSettings::Get().ProcessTreeMode(m_TreeMode);
	// flat mode puts every process at the top level, so sorting covers all of them
	RebuildTree();
	UpdateUI();
	return 0;
}

LRESULT CProcessesView::OnToggleKeepSelectionVisible(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_KeepSelectionVisible = !m_KeepSelectionVisible;
	auto& settings = AppSettings::Get();
	settings.ProcessKeepSelectionVisible(m_KeepSelectionVisible);
	settings.Save();
	if (m_KeepSelectionVisible)
		if (auto item = GetSelectedProcess())
			m_Tree.EnsureVisible(item->Item);
	UpdateUI();
	return 0;
}

LRESULT CProcessesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto selected = ColumnLayout::Select(m_hWnd, ToLayout(GetShownColumns()), s_Columns);
	if (!selected)
		return 0;

	std::vector<ShownColumn> columns;
	for (auto& c : *selected)
		columns.push_back({ (Column)c.Id, c.Width });
	SetColumns(columns);
	SaveColumns();
	if (ShowsNetwork() && !m_Network) {
		if (m_NetworkError == ERROR_ACCESS_DENIED)
			AtlMessageBox(m_hWnd, L"The network columns need administrator rights, and stay empty until WinSysLive runs elevated (File > Run as Administrator).",
				IDR_MAINFRAME, MB_ICONINFORMATION);
		else {
			::SetLastError(m_NetworkError);
			ShowError(L"Starting network monitoring");
		}
	}
	return 0;
}

LRESULT CProcessesView::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// the keyboard's (Shift+F10, the menu key): the selected row's menu, below the row (no mouse position to use)
	auto hWnd = (HWND)wParam;
	if (lParam == -1 && (hWnd == m_Tree.GetListCtrl() || hWnd == m_Tree)) {
		auto item = GetSelectedProcess();
		if (!item || !item->Item)
			return 0;
		CRect rc;
		auto list = m_Tree.GetListCtrl();
		list.GetClientRect(&rc);
		CRect rcItem;
		CPoint pt(rc.left, rc.top);
		if (m_Tree.GetItemRect(item->Item, &rcItem) && rcItem.bottom > rc.top && rcItem.top < rc.bottom)
			pt = CPoint(rcItem.left + ::MulDiv(24, ::GetDpiForWindow(list), 96), rcItem.bottom);
		list.ClientToScreen(&pt);
		ShowRowMenu(pt);
		return 0;
	}

	// the header's WM_CONTEXTMENU comes up through the list view, the TreeListView and the splitter, and arrives
	// with one of those as the window, so the header is found by the position; the rows have their own menu (NM_RCLICK)
	auto header = m_Tree.GetHeader();
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	CRect rcHeader;
	header.GetWindowRect(&rcHeader);
	if (!rcHeader.PtInRect(pt)) {
		// a row's (NM_RCLICK), or the keyboard's from another window
		bHandled = FALSE;
		return 0;
	}

	HDHITTESTINFO hit{};
	hit.pt = pt;
	header.ScreenToClient(&hit.pt);
	header.HitTest(&hit);
	m_MenuColumn = hit.iItem >= 0 && hit.iItem < (int)m_Columns.size() ? (int)m_Columns[hit.iItem] : -1;

	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CMenuHandle popup = menu.GetSubMenu(2);
	if (m_MenuColumn >= 0)
		popup.ModifyMenu(ID_COLUMN_HIDE, MF_BYCOMMAND | MF_STRING, ID_COLUMN_HIDE,
			std::format(L"&Hide \"{}\"", s_Columns[m_MenuColumn].Name).c_str());
	bool canHide = m_MenuColumn >= 0 && !s_Columns[m_MenuColumn].Mandatory;
	popup.EnableMenuItem(ID_COLUMN_HIDE, MF_BYCOMMAND | (canHide ? MF_ENABLED : MF_GRAYED));
	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
	return 0;
}

LRESULT CProcessesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;

	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return (int)c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}
