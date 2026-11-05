#include "pch.h"
#include "ThreadsView.h"
#include "ProcessInfoEx.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include "Highlight.h"
#include "ListExport.h"
#include <ClipboardHelper.h>
#include <DpiHelper.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

namespace {
	// in CThreadsView::Column order; the keys are stored in the ThreadsViewColumns setting (widths at 96 DPI). Shown at
	// first: the snapshot's values, and the service and start address (read for the rows painted)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Process", L"Process", 170, LVCFMT_LEFT, true },
		{ L"PID", L"Pid", 70, LVCFMT_RIGHT, true },
		{ L"TID", L"Id", 70, LVCFMT_RIGHT, true, true },
		{ L"CPU (%)", L"CPU", 70, LVCFMT_RIGHT, true },
		{ L"CPU Time", L"CpuTime", 100, LVCFMT_RIGHT, true },
		{ L"Kernel Time", L"KernelTime", 100, LVCFMT_RIGHT, false },
		{ L"User Time", L"UserTime", 100, LVCFMT_RIGHT, false },
		{ L"Cycles", L"Cycles", 130, LVCFMT_RIGHT, false },
		{ L"Cycles Delta", L"CyclesDelta", 110, LVCFMT_RIGHT, false },
		{ L"State", L"State", 90, LVCFMT_LEFT, true },
		{ L"Wait Reason", L"WaitReason", 120, LVCFMT_LEFT, true },
		{ L"Priority", L"Priority", 60, LVCFMT_RIGHT, true },
		{ L"Base Pri", L"BasePriority", 80, LVCFMT_RIGHT, true },
		{ L"Ideal CPU", L"IdealCpu", 70, LVCFMT_RIGHT, false },
		{ L"I/O Pri", L"IoPriority", 80, LVCFMT_LEFT, false },
		{ L"Memory Pri", L"MemoryPriority", 100, LVCFMT_RIGHT, false },
		{ L"Ctx Switches", L"ContextSwitches", 110, LVCFMT_RIGHT, false },
		{ L"Ctx Switch Delta", L"ContextSwitchDelta", 120, LVCFMT_RIGHT, true },
		{ L"Service", L"Service", 140, LVCFMT_LEFT, true },
		{ L"Start Address", L"StartAddress", 260, LVCFMT_LEFT, true },	// its symbol, once resolved
		{ L"Created", L"Created", 150, LVCFMT_LEFT, true },
		{ L"COM Apartment", L"ComApartment", 110, LVCFMT_LEFT, false },
	};

	std::unique_ptr<Thread> OpenThread(uint32_t tid) {
		// limited access is all a protected process's threads allow; the I/O and memory priorities need more
		if (auto thread = Thread::OpenById(tid, ThreadAccessMask::QueryInformation))
			return thread;
		return Thread::OpenById(tid, ThreadAccessMask::QueryLimitedInformation);
	}
}

LRESULT CThreadsView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	// subitem images: the state's in the leftmost column, the process's icon in its column
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().ThreadsViewColumns(), s_Columns));

	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 280, L"Process, PID, TID, state or wait reason (Ctrl+F)", m_List, [this](auto&) {
		ShowRows(GetSelectedKey());
		UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

LRESULT CThreadsView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

void CThreadsView::OnActivate(bool active) {
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		// taken only while active: the rates start again
		m_PrevTime = {};
		for (auto& [key, state] : m_States)
			state->Known = false;
		Update();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CThreadsView::OnRefresh(bool active) {
	if (active)
		Update();
}

void CThreadsView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CThreadsView::OnFrameIdle() {
	auto row = GetSelected();
	Frame()->GetUI().UIEnable(ID_KILL, m_Active && row && !row->Exited && ThreadCommands::CanControl(row->State->Pid));
}

bool CThreadsView::IsShown(Column column) const {
	return std::ranges::any_of(GetShownColumns(), [&](auto& c) { return (Column)c.Id == column; });
}

void CThreadsView::Update() {
	auto selected = GetSelectedKey();
	auto& pm = Frame()->GetProcessManager();
	LARGE_INTEGER now;
	::QueryPerformanceCounter(&now);
	static const int64_t frequency = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f.QuadPart; }();
	static const int processors = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	// in 100 ns units, like the CPU times
	int64_t elapsed = m_PrevTime.QuadPart ? (now.QuadPart - m_PrevTime.QuadPart) * 10000000 / frequency : 0;
	m_Refresh++;
	m_ShowCom = IsShown(Column::ComApartment);
	auto newTicks = Highlight::NewTicks();

	m_Rows.clear();
	m_All.clear();
	m_ByProcess.clear();
	decltype(m_States) current;
	current.reserve(m_States.size() + 256);
	auto processes = pm.GetProcesses();
	m_ProcessCount = processes.size();
	size_t threadCount = 0;
	for (auto& p : processes)
		if (p->NativeInfo)
			threadCount += p->NativeInfo->NumberOfThreads;
	m_All.reserve(threadCount + m_Exited.size());

	for (auto& p : processes) {
		auto info = p->NativeInfo;
		if (!info)
			continue;
		// the snapshot is SystemExtendedProcessInformation (or Full): the threads follow the process entry
		auto threads = reinterpret_cast<SYSTEM_EXTENDED_THREAD_INFORMATION const*>(info->Threads);
		for (ULONG i = 0; i < info->NumberOfThreads; i++) {
			auto& ext = threads[i];
			auto& t = ext.ThreadInfo;
			Row row;
			row.Key = { t.CreateTime.QuadPart, HandleToULong(t.ClientId.UniqueThread) };
			// the Idle process's threads, one a processor, all have ID 0 and no creation time: told apart by the processor
			if (p->Id == 0)
				row.Key.Created = -(int64_t)(i + 1);
			row.KernelTime = t.KernelTime.QuadPart;
			row.UserTime = t.UserTime.QuadPart;
			row.RunState = t.ThreadState;
			row.WaitReason = t.WaitReason;
			row.Priority = t.Priority;
			row.BasePriority = t.BasePriority;
			row.ContextSwitches = t.ContextSwitches;
			// the address the thread was created with; StartAddress is the common user-mode thunk
			row.StartAddress = ext.Win32StartAddress ? ext.Win32StartAddress : t.StartAddress;

			std::unique_ptr<ThreadState> state;
			if (auto it = m_States.find(row.Key); it != m_States.end()) {
				state = std::move(it->second);
				m_States.erase(it);
			}
			else {
				state = std::make_unique<ThreadState>();
				state->Pid = p->Id;
				state->ProcessCreated = p->Key.Created;
				state->ProcessName = p->GetImageName();
				state->ImagePath = p->GetImagePath();
				// the first refresh's threads are all new to the tab, not created
				if (m_PrevTime.QuadPart)
					state->NewTicks = newTicks;
			}
			if (state->NewTicks > 0) {
				row.New = true;
				state->NewTicks--;
			}
			auto cpuTime = row.KernelTime + row.UserTime;
			if (state->Known && elapsed > 0) {
				row.CPU = (int32_t)((cpuTime - state->CpuTime) * 1000000 / elapsed / processors);
				row.ContextSwitchDelta = row.ContextSwitches - state->ContextSwitches;
			}
			state->CpuTime = cpuTime;
			state->ContextSwitches = row.ContextSwitches;
			state->Known = true;
			auto [it, added] = current.emplace(row.Key, std::move(state));
			if (!added)
				continue;	// the same key twice (not seen): the first is kept
			row.State = it->second.get();
			row.State->Last = row;
			m_All.push_back(row);
		}
	}

	// the threads that exited since the last refresh join those still shown, as they were; their handles close when they go
	auto exitedTicks = Highlight::ExitedTicks();
	for (auto& [key, state] : m_States) {
		state->ExitedTicks = exitedTicks;
		m_Exited.insert_or_assign(key, std::move(state));
	}
	std::erase_if(m_Exited, [](auto const& e) { return e.second->ExitedTicks <= 0; });
	for (auto& [key, state] : m_Exited) {
		auto row = state->Last;
		row.New = false;
		row.Exited = true;
		row.State = state.get();
		m_All.push_back(row);
		state->ExitedTicks--;
	}
	m_States = std::move(current);

	// the handles not needed for a while close (an open handle keeps an exited thread's object alive), and the processes
	// that are gone go
	auto tick = ::GetTickCount64();
	for (auto& [key, state] : m_States)
		if (state->Handle && tick - state->LastUsed > HandleIdle) {
			state->Handle.reset();
			state->HandleTried = false;
		}
	for (auto& p : processes)
		if (auto it = m_Processes.find(p->Key); it != m_Processes.end())
			it->second.SeenRefresh = m_Refresh;
	std::erase_if(m_Processes, [&](auto const& e) { return e.second.SeenRefresh != m_Refresh; });
	if (m_ShowCom)
		for (auto& row : m_All)
			if (!row.Exited)
				m_ByProcess[{ row.State->ProcessCreated, row.State->Pid }].push_back(&row);
	m_PrevTime = now;

	ShowRows(selected);
	UpdateStatus();
}

bool CThreadsView::NeedsHandle(Column column) {
	switch (column) {
		case Column::Cycles: case Column::CyclesDelta: case Column::IdealCpu: case Column::IoPriority: case Column::MemoryPriority:
		case Column::Service: case Column::StartAddress: case Column::ComApartment:
			return true;
	}
	return false;
}

CThreadsView::ProcessState* CThreadsView::GetProcess(ThreadState const& state) const {
	WinSys::ProcessOrThreadKey key{ state.ProcessCreated, state.Pid };
	auto [it, added] = m_Processes.try_emplace(key);
	auto& process = it->second;
	if (added) {
		process.SeenRefresh = m_Refresh;
		process.Handle.reset(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, state.Pid));
		// the same ID may be another process by now
		FILETIME created, exited, kernel, user;
		if (process.Handle && (!::GetProcessTimes(process.Handle.get(), &created, &exited, &kernel, &user)
			|| (int64_t)ULARGE_INTEGER{ created.dwLowDateTime, created.dwHighDateTime }.QuadPart != state.ProcessCreated))
			process.Handle.reset();
		process.Wow64 = process.Handle && Thread::IsWow64(process.Handle.get());
	}
	return process.Handle ? &process : nullptr;
}

void CThreadsView::Read(Row const& row) const {
	auto& state = *row.State;
	if (row.Exited || state.ReadRefresh == m_Refresh)
		return;
	bool consecutive = state.ReadRefresh && state.ReadRefresh == m_Refresh - 1;
	state.ReadRefresh = m_Refresh;
	state.LastUsed = ::GetTickCount64();
	if (!state.HandleTried) {
		state.HandleTried = true;
		state.Handle = OpenThread(row.Key.Id);
		// the ID may be another thread's by now
		if (state.Handle) {
			auto created = state.Handle->GetCreateTime();
			if (created && created != row.Key.Created)
				state.Handle.reset();
		}
	}
	if (!state.Handle)
		return;
	auto& thread = *state.Handle;
	state.IdealCpu = thread.GetIdealProcessor();
	state.IoPriority = thread.GetIoPriority();
	state.MemoryPriority = thread.GetMemoryPriority();
	auto cycles = thread.GetCycleTime();
	state.CyclesDelta = consecutive && cycles >= state.Cycles ? int64_t(cycles - state.Cycles) : -1;
	state.Cycles = cycles;
	if (!state.Service)
		state.Service = state.Pid > 4 ? thread.GetServiceNameByTag(state.Pid) : std::wstring();
	if (!row.StartAddress && !state.StartAddress)
		state.StartAddress = thread.GetStartAddress();
	if (m_ShowCom && state.Pid > 4) {
		if (auto process = GetProcess(state)) {
			if (!state.Teb)
				state.Teb = Thread::GetTebAddress(thread.Handle());
			state.Com = Thread::GetComFlags(process->Handle.get(), state.Teb, process->Wow64);
		}
	}
}

bool CThreadsView::HasMta(ThreadState const& state) const {
	auto process = GetProcess(state);
	if (!process)
		return false;
	if (process->MtaRefresh != m_Refresh) {
		process->MtaRefresh = m_Refresh;
		process->HasMta = false;
		if (auto it = m_ByProcess.find({ state.ProcessCreated, state.Pid }); it != m_ByProcess.end())
			for (auto row : it->second) {
				Read(*row);
				auto com = row->State->Com;
				if (com != ComFlags::Error && (com & ComFlags::MTA) == ComFlags::MTA) {
					process->HasMta = true;
					break;
				}
			}
	}
	return process->HasMta;
}

std::wstring CThreadsView::GetText(Row const& row, Column column) const {
	static const bool groups = ::GetActiveProcessorGroupCount() > 1;
	auto& state = *row.State;
	if (NeedsHandle(column))
		Read(row);
	bool handle = state.Handle != nullptr && !row.Exited;
	switch (column) {
		case Column::Process: return state.ProcessName;
		case Column::Pid: return std::to_wstring(state.Pid);
		case Column::Id: return std::to_wstring(row.Key.Id);
		case Column::CPU: return row.CPU < 0 ? L"" : FormatHelper::FormatCpu(row.CPU);
		case Column::CpuTime: return FormatHelper::FormatTimeSpan(row.KernelTime + row.UserTime);
		case Column::KernelTime: return FormatHelper::FormatTimeSpan(row.KernelTime);
		case Column::UserTime: return FormatHelper::FormatTimeSpan(row.UserTime);
		case Column::Cycles: return handle ? FormatHelper::FormatNumber(state.Cycles) : L"";
		case Column::CyclesDelta: return handle && state.CyclesDelta > 0 ? FormatHelper::FormatNumber(state.CyclesDelta) : L"";
		case Column::State: return ThreadCommands::StateText(row.RunState);
		case Column::WaitReason: return row.RunState == 5 ? ThreadCommands::WaitReasonText(row.WaitReason) : L"";	// Waiting
		case Column::Priority: return std::to_wstring(row.Priority);
		case Column::BasePriority: return std::to_wstring(row.BasePriority);
		case Column::IdealCpu:
			if (!handle || state.IdealCpu.Number == 0xff)
				return L"";
			return groups ? std::format(L"{}:{}", state.IdealCpu.Group, state.IdealCpu.Number) : std::to_wstring(state.IdealCpu.Number);
		case Column::IoPriority: return handle ? FormatHelper::IoPriorityToString(state.IoPriority) : L"";
		case Column::MemoryPriority: return handle && state.MemoryPriority >= 0 ? std::to_wstring(state.MemoryPriority) : L"";
		case Column::ContextSwitches: return FormatHelper::FormatNumber(row.ContextSwitches);
		case Column::ContextSwitchDelta: return row.ContextSwitchDelta > 0 ? FormatHelper::FormatNumber(row.ContextSwitchDelta) : L"";
		case Column::Service: return state.Service.value_or(L"");
		case Column::StartAddress:
		{
			auto address = (ULONG_PTR)(row.StartAddress ? row.StartAddress : state.StartAddress.value_or(nullptr));
			if (!address)
				return L"";
			// the symbol once it's resolved (WM_SYMBOLS_RESOLVED repaints), else the address
			auto symbol = SymbolEngine::Get().Lookup(state.Pid, state.ProcessCreated, address, m_hWnd);
			return symbol && !symbol->empty() ? *symbol : FormatAddress(address);
		}
		case Column::Created: return row.Key.Created > 0 ? FormatHelper::FormatDateTime(row.Key.Created) : L"";
		case Column::ComApartment:
			if (row.Exited || state.Com == ComFlags::Error)
				return L"";
			return Thread::ComApartmentText(state.Com, HasMta(state));
	}
	return L"";
}

bool CThreadsView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	// what's known without a query: a symbol lookup for every thread would load every process's symbols
	for (auto column : { Column::Process, Column::Pid, Column::Id, Column::State, Column::WaitReason })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	if (row.State->Service && CFilterBox::Contains(*row.State->Service, filter))
		return true;
	return false;
}

void CThreadsView::ShowRows(ProcessOrThreadKey const& selected) {
	m_Rows.clear();
	for (auto& row : m_All)
		if (Matches(row))
			m_Rows.push_back(&row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectRow(selected);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

CThreadsView::Row const* CThreadsView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

ProcessOrThreadKey CThreadsView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? row->Key : ProcessOrThreadKey{};
}

void CThreadsView::SelectRow(ProcessOrThreadKey const& key) {
	// the list view keeps the selection by index, which the new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (key.Id == 0 && key.Created == 0)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Key == key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CThreadsView::UpdateStatus() {
	if (!m_Active)
		return;
	auto text = std::format(L"{} threads in {} processes", FormatHelper::FormatNumber((int64_t)m_States.size()), m_ProcessCount);
	if (!m_Filter.GetText().empty())
		text += std::format(L"; {} match the filter", FormatHelper::FormatNumber((int64_t)m_Rows.size()));
	if (!SecurityHelper::IsRunningElevated())
		text += L"; not elevated: other users' threads show the snapshot's values only";
	Frame()->SetStatusText(0, text.c_str());
}

CString CThreadsView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CThreadsView::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return -1;
	auto& r = *m_Rows[row];
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(col);
	if (column == Column::Process)
		return Frame()->GetProcessIcon(r.State->ImagePath);
	if (!ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	return Frame()->GetIconIndex(r.Exited ? IDI_KILL : ThreadCommands::StateIcon(r.RunState));
}

int CThreadsView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by thread key (ShowRows, DoSort)
	return -1;
}

void CThreadsView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	// by a handle's value: every row's read (once this refresh)
	if (NeedsHandle(column) && column != Column::StartAddress && column != Column::ComApartment)
		for (auto row : m_Rows)
			Read(*row);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	auto numeric = [&](Row const& r1, Row const& r2, int& result) {
		auto& s1 = *r1.State;
		auto& s2 = *r2.State;
		switch (column) {
			case Column::Pid: result = compare(s1.Pid, s2.Pid); return true;
			case Column::Id: result = compare(r1.Key.Id, r2.Key.Id); return true;
			case Column::CPU: result = compare(r1.CPU, r2.CPU); return true;
			case Column::CpuTime: result = compare(r1.KernelTime + r1.UserTime, r2.KernelTime + r2.UserTime); return true;
			case Column::KernelTime: result = compare(r1.KernelTime, r2.KernelTime); return true;
			case Column::UserTime: result = compare(r1.UserTime, r2.UserTime); return true;
			case Column::Cycles: result = compare(s1.Cycles, s2.Cycles); return true;
			case Column::CyclesDelta: result = compare(s1.CyclesDelta, s2.CyclesDelta); return true;
			case Column::State: result = compare(r1.RunState, r2.RunState); return true;
			case Column::Priority: result = compare(r1.Priority, r2.Priority); return true;
			case Column::BasePriority: result = compare(r1.BasePriority, r2.BasePriority); return true;
			case Column::IdealCpu: result = compare(std::pair(s1.IdealCpu.Group, s1.IdealCpu.Number), std::pair(s2.IdealCpu.Group, s2.IdealCpu.Number)); return true;
			case Column::IoPriority: result = compare(s1.IoPriority, s2.IoPriority); return true;
			case Column::MemoryPriority: result = compare(s1.MemoryPriority, s2.MemoryPriority); return true;
			case Column::ContextSwitches: result = compare(r1.ContextSwitches, r2.ContextSwitches); return true;
			case Column::ContextSwitchDelta: result = compare(r1.ContextSwitchDelta, r2.ContextSwitchDelta); return true;
			// by the address, not the symbol (which would be looked up for every thread)
			case Column::StartAddress: result = compare((ULONG_PTR)r1.StartAddress, (ULONG_PTR)r2.StartAddress); return true;
			case Column::Created: result = compare(r1.Key.Created, r2.Key.Created); return true;
		}
		return false;
	};
	int probe;
	if (!m_Rows.empty() && numeric(*m_Rows[0], *m_Rows[0], probe))
		std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
			int result = 0;
			numeric(*r1, *r2, result);
			// ties by process, then thread ID, so equal values don't jump around
			if (result == 0)
				result = compare(std::pair(r1->State->Pid, r1->Key.Id), std::pair(r2->State->Pid, r2->Key.Id));
			return si->SortAscending ? result < 0 : result > 0;
			});
	else {
		// a text column: each row's text once
		std::vector<std::pair<std::wstring, Row const*>> texts;
		texts.reserve(m_Rows.size());
		for (auto row : m_Rows)
			texts.push_back({ GetText(*row, column), row });
		std::ranges::stable_sort(texts, [&](auto const& t1, auto const& t2) {
			int result = ::_wcsicmp(t1.first.c_str(), t2.first.c_str());
			if (result == 0)
				result = compare(std::pair(t1.second->State->Pid, t1.second->Key.Id), std::pair(t2.second->State->Pid, t2.second->Key.Id));
			return si->SortAscending ? result < 0 : result > 0;
			});
		for (size_t i = 0; i < texts.size(); i++)
			m_Rows[i] = texts[i].second;
	}
	SelectRow(selected);
}

ThreadCommands::Target CThreadsView::GetTarget(Row const& row) const {
	auto& state = *row.State;
	return { row.Key, state.Pid, state.ProcessCreated, state.ProcessName, ThreadCommands::IsSuspended(row.RunState, row.WaitReason) };
}

bool CThreadsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size() || m_Rows[row]->Exited)
		return false;
	auto& r = *m_Rows[row];
	Read(r);
	std::optional<ThreadPriorityLevel> current;
	if (r.State->Handle)
		current = r.State->Handle->GetPriority();
	CMenu menu;
	ThreadCommands::BuildMenu(menu, GetTarget(r), current, ID_EDIT_COPY, DpiHelper::SmallIconSize(m_hWnd));
	menu.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_THREADS_GOTOPROCESS, L"&Go to Process");
	menu.InsertMenu(1, MF_BYPOSITION | MF_SEPARATOR);
	menu.SetMenuDefaultItem(ID_THREAD_STACK);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CThreadsView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size() || m_Rows[row]->Exited)
		return false;
	// posted, so the dialog doesn't run inside the list's mouse handling
	PostMessage(WM_COMMAND, ID_THREAD_STACK);
	return true;
}

LRESULT CThreadsView::OnThreadCommand(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && !row->Exited)
		ThreadCommands::Execute(m_hWnd, wID, GetTarget(*row));
	return 0;
}

LRESULT CThreadsView::OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && !row->Exited)
		ThreadCommands::Execute(m_hWnd, ID_THREAD_TERMINATE, GetTarget(*row));
	return 0;
}

LRESULT CThreadsView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		Frame()->GoToProcess(row->State->Pid);
	return 0;
}

LRESULT CThreadsView::OnSymbolsResolved(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	return 0;
}

LRESULT CThreadsView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			auto& row = *m_Rows[index];
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

LRESULT CThreadsView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CThreadsView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (int selected = m_List.GetSelectedIndex(); selected >= 0)
		ClipboardHelper::CopyText(m_hWnd, ListExport::RowText(m_List, selected).c_str());
	return 0;
}

LRESULT CThreadsView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CThreadsView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CThreadsView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CThreadsView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ThreadsViewColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CThreadsView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		Update();
	}
	return 0;
}

LRESULT CThreadsView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	// the header's WM_CONTEXTMENU, told from a row's by the position; the rows have their own menu (OnRightClickList)
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	auto column = ColumnLayout::HeaderColumnAt(m_List, GetColumnManager(m_List), pt);
	if (!column) {
		bHandled = FALSE;
		return 0;
	}
	m_MenuColumn = *column;
	CMenu menu;
	Frame()->ShowContextMenu(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), pt, TPM_RIGHTBUTTON);
	return 0;
}

LRESULT CThreadsView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	Update();
	return 0;
}

LRESULT CThreadsView::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	Update();
	return 0;
}
