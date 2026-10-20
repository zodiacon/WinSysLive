#include "pch.h"
#include "ProcessInfoEx.h"
#include "ThreadsPage.h"
#include "FormatHelper.h"
#include "ViewBase.h"
#include "AppSettings.h"
#include "resource.h"
#include <WTLHelper.h>
#include <IconHelper.h>

using namespace WinSys;

namespace {
	PCWSTR ThreadStateToString(uint32_t state) {
		static PCWSTR const names[] = {
			L"Initialized", L"Ready", L"Running", L"Standby", L"Terminated", L"Waiting", L"Transition",
			L"Deferred Ready", L"Gate Wait", L"Waiting for Swap",
		};
		return state < _countof(names) ? names[state] : L"";
	}

	PCWSTR WaitReasonToString(uint32_t reason) {
		// KWAIT_REASON
		static PCWSTR const names[] = {
			L"Executive", L"Free Page", L"Page In", L"Pool Allocation", L"Delay Execution", L"Suspended", L"User Request",
			L"Executive", L"Free Page", L"Page In", L"Pool Allocation", L"Delay Execution", L"Suspended", L"User Request",
			L"Event Pair", L"Queue", L"LPC Receive", L"LPC Reply", L"Virtual Memory", L"Page Out", L"Rendezvous",
			L"Keyed Event", L"Terminated", L"Process In Swap", L"CPU Rate Control", L"Callout Stack", L"Kernel",
			L"Resource", L"Push Lock", L"Mutex", L"Quantum End", L"Dispatch Int", L"Preempted", L"Yield Execution",
			L"Fast Mutex", L"Guarded Mutex", L"Rundown", L"Alert By Thread ID", L"Deferred Preempt", L"Physical Fault",
			L"I/O Ring", L"Mdl Cache", L"RCU",
		};
		return reason < _countof(names) ? names[reason] : L"";
	}

	// in CThreadsPage::Column order; the keys are stored in the ThreadColumns setting
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"TID", L"Id", 70, LVCFMT_RIGHT, true, true },
		{ L"CPU (%)", L"CPU", 70, LVCFMT_RIGHT, true },
		{ L"CPU Time", L"CpuTime", 100, LVCFMT_RIGHT, true },
		{ L"Kernel Time", L"KernelTime", 100, LVCFMT_RIGHT, true },
		{ L"User Time", L"UserTime", 100, LVCFMT_RIGHT, true },
		{ L"Cycles", L"Cycles", 130, LVCFMT_RIGHT, true },
		{ L"Cycles Delta", L"CyclesDelta", 110, LVCFMT_RIGHT, true },
		{ L"State", L"State", 90, LVCFMT_LEFT, true },
		{ L"Wait Reason", L"WaitReason", 120, LVCFMT_LEFT, true },
		{ L"Priority", L"Priority", 60, LVCFMT_RIGHT, true },
		{ L"Base Priority", L"BasePriority", 80, LVCFMT_RIGHT, true },
		{ L"Ideal CPU", L"IdealCpu", 70, LVCFMT_RIGHT, true },
		{ L"I/O Priority", L"IoPriority", 80, LVCFMT_LEFT, true },
		{ L"Memory Priority", L"MemoryPriority", 100, LVCFMT_RIGHT, true },
		{ L"Context Switches", L"ContextSwitches", 110, LVCFMT_RIGHT, true },
		{ L"Context Switch Delta", L"ContextSwitchDelta", 120, LVCFMT_RIGHT, true },
		{ L"Service", L"Service", 140, LVCFMT_LEFT, true },
		{ L"Start Address", L"StartAddress", 140, LVCFMT_RIGHT, true },
		{ L"Created", L"Created", 150, LVCFMT_LEFT, true },
	};

	// highlighting of created threads lasts this many refreshes
	const int NewThreadTicks = 2;

	std::unique_ptr<Thread> OpenThread(uint32_t tid) {
		// limited access is all a protected process's threads allow; the I/O and memory priorities need more
		if (auto thread = Thread::OpenById(tid, ThreadAccessMask::QueryInformation))
			return thread;
		return Thread::OpenById(tid, ThreadAccessMask::QueryLimitedInformation);
	}
}

void CThreadsPage::UseColumns(std::wstring layout) {
	m_Columns = std::move(layout);
}

LRESULT CThreadsPage::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	// the state icons, in the leftmost column: an image list of the page's own, as the frame's belongs to the main
	// thread (the property sheet's page runs on another)
	static const UINT icons[] = { IDI_THREADS, IDI_CONTINUE, IDI_RUN, IDI_KILL, IDI_PAUSE, IDI_MEMORY };
	m_Images.Create(16, 16, ILC_COLOR32 | ILC_MASK, _countof(icons), 0);
	for (auto id : icons)
		m_IconIndices[id] = m_Images.AddIcon(IconHelper::LoadCached(id, 16));
	m_List.SetImageList(m_Images, LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	SetColumns(ColumnLayout::Parse(m_Columns ? *m_Columns : AppSettings::Get().ThreadColumns(), s_Columns));
	return 0;
}

LRESULT CThreadsPage::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CThreadsPage::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CThreadsPage::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CThreadsPage::SaveColumns() const {
	if (m_Columns)
		return;
	auto& settings = AppSettings::Get();
	settings.ThreadColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CThreadsPage::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	auto column = ColumnLayout::HeaderColumnAt(m_List, GetColumnManager(m_List), pt);
	if (!column) {
		bHandled = FALSE;
		return 0;
	}
	m_MenuColumn = *column;
	CMenu menu;
	// the commands come to this page (OnHideColumn, OnSelectColumns): through the frame, Select Columns would go to the
	// active view (the process list). They're posted, not returned (TPM_RETURNCMD), so the dialog doesn't run inside the
	// header's right-button handling, where it didn't paint
	::TrackPopupMenuEx(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return 0;
}

LRESULT CThreadsPage::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CThreadsPage::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
	}
	return 0;
}

LRESULT CThreadsPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_List)
		m_List.MoveWindow(0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

LRESULT CThreadsPage::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.SetFocus();
	return 0;
}

LRESULT CThreadsPage::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// the list covers the page
}

void CThreadsPage::SetProcess(std::shared_ptr<ProcessInfoEx> process) {
	if (process == m_Process)
		return;
	m_Process = std::move(process);
	// the handles and the values to compute deltas from belong to the previous process
	m_Threads.clear();
	m_Exited.clear();
	m_PrevTime = {};
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	Refresh();
	if (m_List.GetItemCount() > 0)
		m_List.EnsureVisible(0, FALSE);
}

void CThreadsPage::Refresh() {
	Update(m_Process ? m_Process->NativeInfo : nullptr);
}

void CThreadsPage::Update(SYSTEM_PROCESS_INFORMATION const* info, int64_t time) {
	if (time && time == m_PrevTime.QuadPart)
		return;
	int selected = m_List.GetSelectedIndex();
	uint32_t selectedTid = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Key.Id : 0;

	m_Rows.clear();
	LARGE_INTEGER now;
	if (time)
		now.QuadPart = time;
	else
		::QueryPerformanceCounter(&now);
	if (info) {
		static const int64_t frequency = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f.QuadPart; }();
		static const int processors = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		// in 100 ns units, like the CPU times
		int64_t elapsed = m_PrevTime.QuadPart ? (now.QuadPart - m_PrevTime.QuadPart) * 10000000 / frequency : 0;

		// the snapshot is SystemExtendedProcessInformation (or Full): the threads follow the process entry
		auto pid = HandleToULong(info->UniqueProcessId);
		auto threads = reinterpret_cast<SYSTEM_EXTENDED_THREAD_INFORMATION const*>(info->Threads);
		decltype(m_Threads) current;
		current.reserve(info->NumberOfThreads);
		m_Rows.reserve(info->NumberOfThreads);
		for (ULONG i = 0; i < info->NumberOfThreads; i++) {
			auto& ext = threads[i];
			auto& t = ext.ThreadInfo;
			ThreadRow row;
			row.Key = { t.CreateTime.QuadPart, HandleToULong(t.ClientId.UniqueThread) };
			row.KernelTime = t.KernelTime.QuadPart;
			row.UserTime = t.UserTime.QuadPart;
			row.State = t.ThreadState;
			row.WaitReason = t.WaitReason;
			row.Priority = t.Priority;
			row.BasePriority = t.BasePriority;
			row.ContextSwitches = t.ContextSwitches;
			// the address the thread was created with; StartAddress is the common user-mode thunk
			row.StartAddress = ext.Win32StartAddress ? ext.Win32StartAddress : t.StartAddress;

			// a thread's state moves to the new map; the ones left in the old map exited
			ThreadState state;
			if (auto it = m_Threads.find(row.Key); it != m_Threads.end()) {
				state = std::move(it->second);
				m_Threads.erase(it);
			}
			else {
				state.Handle = OpenThread(row.Key.Id);
				if (state.Handle && pid > 4)
					state.Service = state.Handle->GetServiceNameByTag(pid);
				// on the first refresh of a process, all its threads are new to the page, not created
				if (m_PrevTime.QuadPart)
					state.NewTicks = NewThreadTicks;
			}
			if (state.NewTicks > 0) {
				row.New = true;
				state.NewTicks--;
			}

			auto cpuTime = row.KernelTime + row.UserTime;
			if (state.Handle) {
				auto& thread = *state.Handle;
				row.HasHandle = true;
				row.IdealCpu = thread.GetIdealProcessor();
				row.IoPriority = thread.GetIoPriority();
				row.MemoryPriority = thread.GetMemoryPriority();
				row.Cycles = thread.GetCycleTime();
			}
			if (state.Known && elapsed > 0) {
				row.CPU = (int32_t)((cpuTime - state.CpuTime) * 1000000 / elapsed / processors);
				row.ContextSwitchDelta = row.ContextSwitches - state.ContextSwitches;
				if (row.HasHandle)
					row.CyclesDelta = row.Cycles >= state.Cycles ? int64_t(row.Cycles - state.Cycles) : 0;
			}
			state.CpuTime = cpuTime;
			state.ContextSwitches = row.ContextSwitches;
			state.Cycles = row.Cycles;
			state.Known = true;
			auto& kept = current.insert({ row.Key, std::move(state) }).first->second;
			row.Service = &kept.Service;
			kept.Last = row;
			m_Rows.push_back(std::move(row));
		}

		// the threads that exited since the last refresh: shown once more, as they were; their handles close with
		// the next refresh
		m_Exited = std::move(m_Threads);
		for (auto& [key, state] : m_Exited) {
			auto row = state.Last;
			row.New = false;
			row.Exited = true;
			row.Service = &state.Service;
			m_Rows.push_back(std::move(row));
		}
		m_Threads = std::move(current);
	}
	m_PrevTime = now;

	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectThread(selectedTid);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

void CThreadsPage::SelectThread(uint32_t tid) {
	// the list view keeps the selection by index, which the new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (tid == 0)
		return;
	auto it = std::ranges::find_if(m_Rows, [=](auto& r) { return r.Key.Id == tid; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

CString CThreadsPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

std::wstring CThreadsPage::GetText(ThreadRow const& row, Column column) const {
	static const bool groups = ::GetActiveProcessorGroupCount() > 1;
	switch (column) {
		case Column::Id: return std::to_wstring(row.Key.Id);
		case Column::CPU: return row.CPU < 0 ? L"" : FormatHelper::FormatCpu(row.CPU);
		case Column::CpuTime: return FormatHelper::FormatTimeSpan(row.KernelTime + row.UserTime);
		case Column::KernelTime: return FormatHelper::FormatTimeSpan(row.KernelTime);
		case Column::UserTime: return FormatHelper::FormatTimeSpan(row.UserTime);
		case Column::Cycles: return row.HasHandle ? FormatHelper::FormatNumber(row.Cycles) : L"";
		case Column::CyclesDelta: return row.CyclesDelta > 0 ? FormatHelper::FormatNumber(row.CyclesDelta) : L"";
		case Column::State: return ThreadStateToString(row.State);
		case Column::WaitReason: return row.State == 5 ? WaitReasonToString(row.WaitReason) : L"";	// Waiting
		case Column::Priority: return std::to_wstring(row.Priority);
		case Column::BasePriority: return std::to_wstring(row.BasePriority);
		case Column::IdealCpu:
			if (!row.HasHandle || row.IdealCpu.Number == 0xff)
				return L"";
			// with more than one processor group, the group too
			return groups ? std::format(L"{}:{}", row.IdealCpu.Group, row.IdealCpu.Number) : std::to_wstring(row.IdealCpu.Number);
		case Column::IoPriority: return FormatHelper::IoPriorityToString(row.IoPriority);
		case Column::MemoryPriority: return row.MemoryPriority >= 0 ? std::to_wstring(row.MemoryPriority) : L"";
		case Column::ContextSwitches: return FormatHelper::FormatNumber(row.ContextSwitches);
		case Column::ContextSwitchDelta: return row.ContextSwitchDelta > 0 ? FormatHelper::FormatNumber(row.ContextSwitchDelta) : L"";
		case Column::Service: return row.Service ? *row.Service : L"";
		case Column::StartAddress: return std::format(L"0x{:X}", (ULONG_PTR)row.StartAddress);
		case Column::Created: return FormatHelper::FormatDateTime(row.Key.Created);
	}
	return L"";
}

int CThreadsPage::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || !ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	auto& r = m_Rows[row];
	if (r.Exited)
		return m_IconIndices.at(IDI_KILL);
	UINT icon;
	switch (r.State) {
		case 1: case 3: case 7: icon = IDI_CONTINUE; break;	// Ready, Standby, Deferred Ready
		case 2: icon = IDI_RUN; break;	// Running
		case 4: icon = IDI_KILL; break;	// Terminated
		case 5: case 8: icon = IDI_PAUSE; break;	// Waiting, Gate Wait
		case 6: case 9: icon = IDI_MEMORY; break;	// Transition, Waiting for Swap: the kernel stack isn't resident
		default: icon = IDI_THREADS; break;	// Initialized
	}
	return m_IconIndices.at(icon);
}

int CThreadsPage::GetSaveColumnRange(HWND, int&) const {
	// Refresh restores the selection (by thread ID)
	return -1;
}

void CThreadsPage::DoSort(SortInfo const* si) {
	int selected = m_List.GetSelectedIndex();
	uint32_t selectedTid = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Key.Id : 0;

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](ThreadRow const& r1, ThreadRow const& r2) {
		int result = 0;
		switch (column) {
			case Column::Id: result = compare(r1.Key.Id, r2.Key.Id); break;
			case Column::CPU: result = compare(r1.CPU, r2.CPU); break;
			case Column::CpuTime: result = compare(r1.KernelTime + r1.UserTime, r2.KernelTime + r2.UserTime); break;
			case Column::KernelTime: result = compare(r1.KernelTime, r2.KernelTime); break;
			case Column::UserTime: result = compare(r1.UserTime, r2.UserTime); break;
			case Column::Cycles: result = compare(r1.Cycles, r2.Cycles); break;
			case Column::CyclesDelta: result = compare(r1.CyclesDelta, r2.CyclesDelta); break;
			case Column::State: result = compare(r1.State, r2.State); break;
			case Column::Priority: result = compare(r1.Priority, r2.Priority); break;
			case Column::BasePriority: result = compare(r1.BasePriority, r2.BasePriority); break;
			case Column::IdealCpu: result = compare(std::pair(r1.IdealCpu.Group, r1.IdealCpu.Number), std::pair(r2.IdealCpu.Group, r2.IdealCpu.Number)); break;
			case Column::IoPriority: result = compare(r1.IoPriority, r2.IoPriority); break;
			case Column::MemoryPriority: result = compare(r1.MemoryPriority, r2.MemoryPriority); break;
			case Column::ContextSwitches: result = compare(r1.ContextSwitches, r2.ContextSwitches); break;
			case Column::ContextSwitchDelta: result = compare(r1.ContextSwitchDelta, r2.ContextSwitchDelta); break;
			case Column::StartAddress: result = compare((ULONG_PTR)r1.StartAddress, (ULONG_PTR)r2.StartAddress); break;
			case Column::Created: result = compare(r1.Key.Created, r2.Key.Created); break;
			default: result = ::_wcsicmp(GetText(r1, column).c_str(), GetText(r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectThread(selectedTid);
}

LRESULT CThreadsPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_List) {
		bHandled = FALSE;
		return 0;
	}
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
		{
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto& row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and exited processes in the process list
			if (row.Exited) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.New) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}
