#include "pch.h"
#include "ProcessInfoEx.h"
#include "ThreadsPage.h"
#include "FormatHelper.h"
#include "ViewBase.h"

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

	const struct {
		PCWSTR Name;
		int Width;
		int Format;
	} s_Columns[] = {	// in CThreadsPage::Column order
		{ L"TID", 70, LVCFMT_RIGHT },
		{ L"CPU (%)", 70, LVCFMT_RIGHT },
		{ L"CPU Time", 100, LVCFMT_RIGHT },
		{ L"Kernel Time", 100, LVCFMT_RIGHT },
		{ L"User Time", 100, LVCFMT_RIGHT },
		{ L"Cycles", 130, LVCFMT_RIGHT },
		{ L"Cycles Delta", 110, LVCFMT_RIGHT },
		{ L"State", 90, LVCFMT_LEFT },
		{ L"Wait Reason", 120, LVCFMT_LEFT },
		{ L"Priority", 60, LVCFMT_RIGHT },
		{ L"Base Priority", 80, LVCFMT_RIGHT },
		{ L"Ideal CPU", 70, LVCFMT_RIGHT },
		{ L"I/O Priority", 80, LVCFMT_LEFT },
		{ L"Memory Priority", 100, LVCFMT_RIGHT },
		{ L"Context Switches", 110, LVCFMT_RIGHT },
		{ L"Context Switch Delta", 120, LVCFMT_RIGHT },
		{ L"Service", 140, LVCFMT_LEFT },
		{ L"Start Address", 140, LVCFMT_RIGHT },
		{ L"Created", 150, LVCFMT_LEFT },
	};

	std::unique_ptr<Thread> OpenThread(uint32_t tid) {
		// limited access is all a protected process's threads allow; the I/O and memory priorities need more
		if (auto thread = Thread::OpenById(tid, ThreadAccessMask::QueryInformation))
			return thread;
		return Thread::OpenById(tid, ThreadAccessMask::QueryLimitedInformation);
	}
}

LRESULT CThreadsPage::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	SuspendHookWhilePainting(m_List);

	auto cm = GetColumnManager(m_List);
	for (int i = 0; i < _countof(s_Columns); i++) {
		auto& c = s_Columns[i];
		cm->AddColumn(c.Name, c.Format, c.Width, (Column)i);
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
	m_PrevTime = {};
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	Refresh();
	if (m_List.GetItemCount() > 0)
		m_List.EnsureVisible(0, FALSE);
}

void CThreadsPage::Refresh() {
	int selected = m_List.GetSelectedIndex();
	uint32_t selectedTid = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Key.Id : 0;

	m_Rows.clear();
	LARGE_INTEGER now;
	::QueryPerformanceCounter(&now);
	if (m_Process) {
		static const int64_t frequency = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f.QuadPart; }();
		static const int processors = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		// in 100 ns units, like the CPU times
		int64_t elapsed = m_PrevTime.QuadPart ? (now.QuadPart - m_PrevTime.QuadPart) * 10000000 / frequency : 0;

		// the snapshot is SystemExtendedProcessInformation (or Full): the threads follow the process entry
		auto info = m_Process->NativeInfo;
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

			// a thread's state moves to the new map, so a thread that exited takes its handle with it
			ThreadState state;
			if (auto it = m_Threads.find(row.Key); it != m_Threads.end())
				state = std::move(it->second);
			else {
				state.Handle = OpenThread(row.Key.Id);
				if (state.Handle && m_Process->Id > 4)
					state.Service = state.Handle->GetServiceNameByTag(m_Process->Id);
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
