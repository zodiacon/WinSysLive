#include "pch.h"
#include "resource.h"
#include <ListViewhelper.h>
#include <DpiHelper.h>
#include "WslView.h"
#include "FormatHelper.h"
#include "Highlight.h"
#include "AppSettings.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>
#include <unordered_set>

using namespace WinSys;

namespace {
	// in CWslView::Column order; the keys are stored in the WslColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Distribution / Process", L"Name", 240, LVCFMT_LEFT, true, true },
		{ L"PID", L"Pid", 70, LVCFMT_RIGHT, true },
		{ L"Parent PID", L"ParentPid", 75, LVCFMT_RIGHT, false },
		{ L"User", L"User", 100, LVCFMT_LEFT, true },
		{ L"State", L"State", 90, LVCFMT_LEFT, true },
		{ L"CPU (%)", L"Cpu", 65, LVCFMT_RIGHT, true },
		{ L"CPU Time", L"CpuTime", 90, LVCFMT_RIGHT, true },
		{ L"Threads", L"Threads", 60, LVCFMT_RIGHT, true },
		{ L"Memory", L"Resident", 90, LVCFMT_RIGHT, true },
		{ L"Virtual Size", L"Virtual", 100, LVCFMT_RIGHT, false },
		{ L"Start Time", L"StartTime", 150, LVCFMT_LEFT, true },
		{ L"Command Line", L"CommandLine", 400, LVCFMT_LEFT, true },
		{ L"Processes", L"Processes", 75, LVCFMT_RIGHT, false },
	};

	const auto PollInterval = std::chrono::seconds(5);
}

CString CWslView::GetTitle() const {
	return L"WSL";
}

LRESULT CWslView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Tree.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Tree.SetCallbackMode(true);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());
	ListViewHelper::EnableColumnDpiScaling(m_Tree.GetListCtrl());
	SetColumns(ColumnLayout::Parse(AppSettings::Get().WslColumns(), s_Columns));
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 360, L"Filter: process, PID, user, command line or distribution (Ctrl+F)",
		m_Tree.GetListCtrl(), [this](auto&) {
			ArrangeTree();
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	// which distributions run: wsl.exe takes about 0.1 s, so on a thread, every few seconds
	m_Poller = std::jthread([this, hWnd = m_hWnd](std::stop_token stop) {
		while (!stop.stop_requested()) {
			auto state = std::make_unique<RunningState>();
			state->Distros = WslDistros::Enum();
			state->Running = WslDistros::GetRunning();
			if (stop.stop_requested())
				break;
			if (::PostMessage(hWnd, WM_WSL_RUNNING, 0, reinterpret_cast<LPARAM>(state.get())))
				state.release();
			std::unique_lock lock(m_PollLock);
			m_PollWake.wait_for(lock, stop, PollInterval, [] { return false; });
		}
		});
	return 0;
}

LRESULT CWslView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	SaveColumns();
	m_Poller.request_stop();
	m_Poller.join();
	// an answer posted before the thread stopped
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_WSL_RUNNING, WM_WSL_RUNNING, PM_REMOVE)) {
		// PeekMessage returns WM_QUIT whatever the filter, and the frame posts it before its views are destroyed: it
		// goes back, or the app's message loop never ends
		if (msg.message == WM_QUIT) {
			::PostQuitMessage((int)msg.wParam);
			break;
		}
		delete reinterpret_cast<RunningState*>(msg.lParam);
	}
	// the helpers' inputs close: they end, and their distributions may idle out
	m_Monitors.clear();
	bHandled = FALSE;
	return 0;
}

void CWslView::OnActivate(bool active) {
	m_Active = active;
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	if (active) {
		UpdateStatus();
		m_Tree.SetFocus();
	}
}

void CWslView::OnFontChanged(HFONT font) {
	m_Tree.SetFont(font);
	m_Filter.SetFont(font);
}

void CWslView::OnThemeChanged(bool dark) {
	m_Tree.SetBkColor(dark ? DarkMode::getViewBackgroundColor() : ::GetSysColor(COLOR_WINDOW));
	m_Tree.SetTextColor(dark ? DarkMode::getViewTextColor() : ::GetSysColor(COLOR_WINDOWTEXT));
	m_Tree.Invalidate();
}

LRESULT CWslView::OnRunning(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	std::unique_ptr<RunningState> state(reinterpret_cast<RunningState*>(lParam));
	m_Running = state->Running;
	SyncDistros(*state);

	// a helper for each running WSL 2 distribution (WSL 1's processes are Windows processes)
	std::unordered_set<std::wstring> running;
	if (m_Running)
		for (auto& name : *m_Running)
			if (auto it = m_Distros.find(name); it != m_Distros.end() && it->second.Info.Version >= 2)
				running.insert(name);
	for (auto it = m_Monitors.begin(); it != m_Monitors.end(); ) {
		if (running.contains(it->first) && it->second.Helper->IsRunning()) {
			++it;
			continue;
		}
		// stopped: its processes are gone
		for (auto& [key, row] : m_Processes)
			if (row.Distro == it->first && !row.Exited) {
				row.Exited = true;
				row.ExitTicks = Highlight::ExitedTicks();
			}
		it = m_Monitors.erase(it);
	}
	for (auto& name : running) {
		if (m_Monitors.contains(name))
			continue;
		auto helper = std::make_unique<WslProcessMonitor>();
		if (helper->Start(name))
			m_Monitors[name].Helper = std::move(helper);
	}
	for (auto& [name, row] : m_Distros)
		row.Running = running.contains(name);

	m_Tree.BeginUpdate();
	ArrangeTree();
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();
	UpdateStatus();
	return 0;
}

void CWslView::SyncDistros(RunningState const& state) {
	// the installed ones; a distribution that's gone takes its rows with it
	for (auto it = m_Distros.begin(); it != m_Distros.end(); ) {
		auto name = it->first;
		if (std::ranges::any_of(state.Distros, [&](auto& d) { return d.Name == name; })) {
			++it;
			continue;
		}
		std::erase_if(m_Processes, [&](auto& pair) { return pair.second.Distro == name; });
		if (it->second.Item)
			m_Tree.DeleteItem(it->second.Item);
		m_Monitors.erase(name);
		it = m_Distros.erase(it);
	}
	for (auto& d : state.Distros) {
		auto [it, inserted] = m_Distros.try_emplace(d.Name);
		auto& row = it->second;
		row.IsDistro = true;
		row.Distro = d.Name;
		row.Info = d;
		if (inserted) {
			row.Item = m_Tree.InsertItem(TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, Frame()->GetIconIndex(IDI_WSL),
				reinterpret_cast<LPARAM>(&row));
			row.ParentItem = TLV_ROOT;
		}
	}
}

void CWslView::OnRefresh(bool /*active*/) {
	auto selected = GetSelectedRow();
	std::optional<Key> selectedProcess;
	std::wstring selectedDistro;
	if (selected) {
		if (selected->IsDistro)
			selectedDistro = selected->Distro;
		else
			selectedProcess = Key{ selected->Distro, selected->Process.Pid, selected->Process.StartTicks };
	}

	// the exited ones count down, and go
	for (auto it = m_Processes.begin(); it != m_Processes.end(); ) {
		auto& row = it->second;
		if (row.NewTicks > 0)
			row.NewTicks--;
		if (row.Exited && --row.ExitTicks <= 0) {
			DeleteItem(row);
			it = m_Processes.erase(it);
			continue;
		}
		++it;
	}
	// each helper's frame (from the previous refresh's request), and the next request
	for (auto& [name, monitor] : m_Monitors) {
		if (!monitor.Helper->IsRunning())
			continue;	// the next poll drops it
		if (auto frame = monitor.Helper->TakeFrame()) {
			monitor.Last = std::move(*frame);
			SyncProcesses(name, monitor);
		}
		if (monitor.Helper->GetSystem())
			monitor.Helper->Request();
	}

	m_Tree.BeginUpdate();
	ArrangeTree();
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();

	HTLITEM hSelect = nullptr;
	if (selectedProcess) {
		if (auto it = m_Processes.find(*selectedProcess); it != m_Processes.end())
			hSelect = it->second.Item;
	}
	else if (!selectedDistro.empty()) {
		if (auto it = m_Distros.find(selectedDistro); it != m_Distros.end())
			hSelect = it->second.Item;
	}
	if (hSelect)
		m_Tree.SelectItem(hSelect, false);
	UpdateStatus();
}

void CWslView::SyncProcesses(std::wstring const& distro, Monitor& monitor) {
	if (auto system = monitor.Helper->GetSystem())
		monitor.System = *system;
	auto& frame = monitor.Last;
	// CPU: the share of all of the VM's processors' ticks since the last frame
	uint64_t totalDelta = monitor.PrevTotalTicks && frame.CpuTotalTicks > monitor.PrevTotalTicks ? frame.CpuTotalTicks - monitor.PrevTotalTicks : 0;
	monitor.PrevTotalTicks = frame.CpuTotalTicks;

	std::unordered_map<uint32_t, uint64_t> starts;	// pid to start, for the parents' keys
	for (auto& p : frame.Processes)
		starts[p.Pid] = p.StartTicks;

	auto& distroRow = m_Distros[distro];
	distroRow.Processes = 0;
	distroRow.Cpu = 0;
	distroRow.Resident = 0;
	std::unordered_set<Key, KeyHash> live;
	for (auto& p : frame.Processes) {
		Key key{ distro, p.Pid, p.StartTicks };
		live.insert(key);
		auto [it, inserted] = m_Processes.try_emplace(key);
		auto& row = it->second;
		if (inserted) {
			row.Distro = distro;
			row.NewTicks = monitor.Loaded ? Highlight::NewTicks() : 0;
			row.StartTime = p.StartTime(frame.UptimeSeconds, frame.ReadTime, monitor.System.ClockTicks);
		}
		auto ticks = p.UserTicks + p.SystemTicks;
		row.Cpu = row.HasPrev && totalDelta && ticks >= row.PrevTicks ? 100.0f * float(ticks - row.PrevTicks) / totalDelta : 0;
		row.PrevTicks = ticks;
		row.HasPrev = true;
		row.Process = p;
		if (auto parent = starts.find(p.ParentPid); parent != starts.end() && p.ParentPid != p.Pid)
			row.Parent = Key{ distro, p.ParentPid, parent->second };
		else
			row.Parent.reset();
		distroRow.Processes++;
		distroRow.Cpu += row.Cpu;
		distroRow.Resident += p.ResidentBytes;
	}
	for (auto& [key, row] : m_Processes)
		if (row.Distro == distro && !row.Exited && !live.contains(key)) {
			row.Exited = true;
			row.ExitTicks = Highlight::ExitedTicks();
			row.NewTicks = 0;
		}
	monitor.Loaded = true;
}

bool CWslView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	auto& p = row.Process;
	return CFilterBox::Contains(p.Name, filter) || CFilterBox::Contains(std::to_wstring(p.Pid), filter) || CFilterBox::Contains(p.User, filter)
		|| CFilterBox::Contains(p.CommandLine, filter) || CFilterBox::Contains(row.Distro, filter);
}

void CWslView::DeleteItem(Row& row) {
	if (!row.Item)
		return;
	// the subtree goes with the item: its rows' items are gone too
	std::vector<HTLITEM> gone{ row.Item };
	for (size_t i = 0; i < gone.size(); i++)
		for (auto& [key, child] : m_Processes)
			if (child.Item && child.ParentItem == gone[i]) {
				gone.push_back(child.Item);
				child.Item = nullptr;
				child.ParentItem = nullptr;
			}
	m_Tree.DeleteItem(row.Item);
	row.Item = nullptr;
	row.ParentItem = nullptr;
}

void CWslView::ArrangeTree() {
	// where each process row belongs: under its parent while that's shown, else its distribution
	auto shown = [&](Row const& row) {
		return m_Distros.contains(row.Distro) && Matches(row);
	};
	auto placeOf = [&](Row const& row) -> Row* {
		if (row.Parent)
			if (auto parent = m_Processes.find(*row.Parent); parent != m_Processes.end() && shown(parent->second))
				return &parent->second;
		auto distro = m_Distros.find(row.Distro);
		return distro == m_Distros.end() ? nullptr : &distro->second;
	};
	// rows that shouldn't be where they are (or at all) go, with their subtrees
	for (auto& [key, row] : m_Processes) {
		if (!row.Item)
			continue;
		auto place = placeOf(row);
		if (!shown(row) || !place || !place->Item || place->Item != row.ParentItem)
			DeleteItem(row);
	}
	// then come back, parents before their children
	for (bool added = true; added; ) {
		added = false;
		for (auto& [key, row] : m_Processes) {
			if (row.Item || !shown(row))
				continue;
			auto place = placeOf(row);
			if (!place || !place->Item)
				continue;
			row.Item = m_Tree.InsertItem(place->Item, TLV_LAST, LPSTR_TEXTCALLBACK, Frame()->GetIconIndex(IDI_PROCESS),
				reinterpret_cast<LPARAM>(&row));
			row.ParentItem = place->Item;
			added = true;
		}
	}
}

void CWslView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (!m_Running)
		text = m_Distros.empty() ? L"WSL isn't installed (wsl.exe didn't run)" : L"wsl.exe didn't run";
	else {
		int running = 0, processes = 0;
		for (auto& [name, row] : m_Distros)
			if (row.Running) {
				running++;
				processes += row.Processes;
			}
		text = std::format(L"{} distributions, {} running; {} processes", m_Distros.size(), running, processes);
		// the VM's memory, shared by its distributions
		for (auto& [name, monitor] : m_Monitors)
			if (monitor.Last.MemTotal) {
				text += std::format(L"; WSL VM memory: {} of {} in use", FormatHelper::FormatBytes(monitor.Last.MemTotal - monitor.Last.MemAvailable),
					FormatHelper::FormatBytes(monitor.Last.MemTotal));
				break;
			}
	}
	Frame()->SetStatusText(0, text.c_str());
}

LRESULT CWslView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CWslView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CWslView::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto& row = *reinterpret_cast<Row*>(m_Tree.GetItemData(di->hItem));
	if (di->iSubItem < (int)m_Columns.size())
		wcsncpy_s(di->pszText, di->cchTextMax, GetColumnText(row, m_Columns[di->iSubItem]).c_str(), _TRUNCATE);
	return 0;
}

std::wstring CWslView::GetColumnText(Row const& row, Column column) const {
	if (row.IsDistro) {
		switch (column) {
			case Column::Name: return row.Distro + (row.Info.Default ? L" (default)" : L"");
			case Column::State:
				if (row.Info.Version < 2)
					return row.Running ? L"Running (WSL 1)" : L"Stopped (WSL 1)";
				return row.Running ? L"Running" : L"Stopped";
			case Column::Cpu: return row.Running ? std::format(L"{:.2f}", row.Cpu) : L"";
			case Column::Resident: return row.Running ? FormatHelper::FormatSize(row.Resident) : L"";
			case Column::Processes: return row.Running ? std::to_wstring(row.Processes) : L"";
			case Column::CommandLine: return row.Info.BasePath;
		}
		return L"";
	}
	auto& p = row.Process;
	auto monitor = m_Monitors.find(row.Distro);
	uint32_t clock = monitor != m_Monitors.end() ? monitor->second.System.ClockTicks : 100;
	switch (column) {
		case Column::Name: return p.Name;
		case Column::Pid: return std::to_wstring(p.Pid);
		case Column::ParentPid: return std::to_wstring(p.ParentPid);
		case Column::User: return p.User;
		case Column::State: return row.Exited ? L"Exited" : WslProtocol::StateName(p.State);
		case Column::Cpu: return row.Cpu >= 0.005f ? std::format(L"{:.2f}", row.Cpu) : L"";
		case Column::CpuTime: return FormatHelper::FormatTimeSpan(int64_t((p.UserTicks + p.SystemTicks) * 10'000'000 / std::max(1u, clock)));
		case Column::Threads: return std::to_wstring(p.Threads);
		case Column::Resident: return FormatHelper::FormatSize(p.ResidentBytes);
		case Column::Virtual: return FormatHelper::FormatSize(p.VirtualBytes);
		case Column::StartTime: return row.StartTime ? FormatHelper::FormatDateTime(row.StartTime) : L"";
		case Column::CommandLine: return p.CommandLine;
	}
	return L"";
}

LRESULT CWslView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}
	auto cd = reinterpret_cast<NMTLVCUSTOMDRAW*>(hdr);
	if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
		return CDRF_NOTIFYITEMDRAW;
	if ((cd->nmcd.dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT && cd->hItem) {
		auto& row = *reinterpret_cast<Row*>(m_Tree.GetItemData(cd->hItem));
		bool dark = WTLHelper::IsDarkMode();
		if (row.IsDistro) {
			if (!row.Running)
				cd->clrText = dark ? RGB(128, 128, 128) : RGB(140, 140, 140);
		}
		else if (row.Exited) {
			cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
		}
		else if (row.NewTicks > 0) {
			cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
		}
		return CDRF_NEWFONT;
	}
	return CDRF_DODEFAULT;
}

LRESULT CWslView::OnSort(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ns = reinterpret_cast<NMTLVSORT*>(hdr);
	m_SortColumn = (int)m_Columns[ns->iSubItem];
	m_SortAscending = ns->iDirection > 0;
	auto selected = m_Tree.GetSelectedItem();
	Sort();
	if (selected)
		m_Tree.SelectItem(selected, false);
	return 0;
}

void CWslView::Sort() {
	m_Tree.SortChildren(TLV_ROOT, CompareItems, reinterpret_cast<LPARAM>(this), true);
}

int CALLBACK CWslView::CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam) {
	auto view = reinterpret_cast<CWslView*>(lParam);
	auto& r1 = *reinterpret_cast<Row*>(view->m_Tree.GetItemData(h1));
	auto& r2 = *reinterpret_cast<Row*>(view->m_Tree.GetItemData(h2));
	auto result = view->CompareRows(r1, r2);
	return view->m_SortAscending ? result : -result;
}

int CWslView::CompareRows(Row& r1, Row& r2) const {
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	auto column = (Column)m_SortColumn;
	if (r1.IsDistro && r2.IsDistro) {
		switch (column) {
			case Column::Cpu: return compare(r1.Cpu, r2.Cpu);
			case Column::Resident: return compare(r1.Resident, r2.Resident);
			case Column::Processes: return compare(r1.Processes, r2.Processes);
		}
	}
	else if (!r1.IsDistro && !r2.IsDistro) {
		auto& p1 = r1.Process;
		auto& p2 = r2.Process;
		switch (column) {
			case Column::Pid: return compare(p1.Pid, p2.Pid);
			case Column::ParentPid: return compare(p1.ParentPid, p2.ParentPid);
			case Column::Cpu: return compare(r1.Cpu, r2.Cpu);
			case Column::CpuTime: return compare(p1.UserTicks + p1.SystemTicks, p2.UserTicks + p2.SystemTicks);
			case Column::Threads: return compare(p1.Threads, p2.Threads);
			case Column::Resident: return compare(p1.ResidentBytes, p2.ResidentBytes);
			case Column::Virtual: return compare(p1.VirtualBytes, p2.VirtualBytes);
			case Column::StartTime: return compare(r1.StartTime, r2.StartTime);
		}
	}
	return ::_wcsicmp(GetColumnText(r1, column).c_str(), GetColumnText(r2, column).c_str());
}

CWslView::Row* CWslView::GetSelectedRow() const {
	auto hItem = m_Tree.GetSelectedItem();
	return hItem ? reinterpret_cast<Row*>(m_Tree.GetItemData(hItem)) : nullptr;
}

LRESULT CWslView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
	auto row = GetSelectedRow();
	if (ia->iItem < 0 || !row)
		return 0;
	CPoint pt(ia->ptAction);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowContextMenu(*row, pt);
	return 0;
}

LRESULT CWslView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	// the header's
	CPoint ptHeader(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	CRect rcHeader;
	m_Tree.GetHeader().GetWindowRect(&rcHeader);
	if (rcHeader.PtInRect(ptHeader)) {
		HDHITTESTINFO hit{};
		hit.pt = ptHeader;
		m_Tree.GetHeader().ScreenToClient(&hit.pt);
		m_Tree.GetHeader().HitTest(&hit);
		m_MenuColumn = hit.iItem >= 0 && hit.iItem < (int)m_Columns.size() ? (int)m_Columns[hit.iItem] : -1;
		CMenu menu;
		Frame()->ShowContextMenu(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), ptHeader, TPM_RIGHTBUTTON);
		return 0;
	}
	// from the keyboard (-1, -1); a mouse click has its menu from NM_RCLICK
	auto hItem = m_Tree.GetSelectedItem();
	if (GET_X_LPARAM(lParam) != -1 || GET_Y_LPARAM(lParam) != -1 || !hItem) {
		bHandled = FALSE;
		return 0;
	}
	CRect rc;
	m_Tree.GetItemRect(hItem, &rc);
	CPoint pt(rc.left + 40, rc.bottom);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowContextMenu(*reinterpret_cast<Row*>(m_Tree.GetItemData(hItem)), pt);
	return 0;
}

void CWslView::ShowContextMenu(Row const& row, POINT pt) {
	bool process = !row.IsDistro && !row.Exited;
	enum { Terminate = 1, Kill, Copy, CopyCommandLine };
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (process ? 0 : MF_GRAYED), Terminate, L"&Terminate (SIGTERM)");
	menu.AppendMenu(MF_STRING | (process ? 0 : MF_GRAYED), Kill, L"&Kill (SIGKILL)");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, Copy, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING | (!row.IsDistro && !row.Process.CommandLine.empty() ? 0 : MF_GRAYED), CopyCommandLine, L"Copy Command &Line");
	// the row may be gone after the menu (refreshes go on): copied first
	Row copy = row;
	switch (::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd, nullptr)) {
		case Terminate: Signal(copy, L"TERM", L"Terminate"); break;
		case Kill: Signal(copy, L"KILL", L"Kill"); break;
		case Copy:
		{
			BOOL handled;
			OnCopy(0, 0, nullptr, handled);
			break;
		}
		case CopyCommandLine: ClipboardHelper::CopyText(m_hWnd, copy.Process.CommandLine.c_str()); break;
	}
}

void CWslView::Signal(Row const& row, PCWSTR signal, PCWSTR verb) {
	auto& p = row.Process;
	auto text = std::format(L"{} {} (PID {}) in {}?{}", verb, p.Name, p.Pid, row.Distro,
		p.Pid == 1 ? L"\n\nIt's the distribution's init: the distribution stops." : L"");
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return;
	CWaitCursor wait;
	std::wstring error;
	if (!WslDistros::Signal(row.Distro, p.Pid, signal, error))
		AtlMessageBox(m_hWnd, std::format(L"{} {} (PID {}) failed: {}", verb, p.Name, p.Pid, error).c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

LRESULT CWslView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row)
		return 0;
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetColumnText(*row, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CWslView::GetShownColumns() const {
	int count = (int)m_Columns.size();
	std::vector<int> order(count);
	m_Tree.GetHeader().GetOrderArray(count, order.data());
	std::vector<ColumnLayout::ShownColumn> columns;
	auto dpi = DpiHelper::GetDpi(m_Tree);
	for (int index : order)
		columns.push_back({ (int)m_Columns[index], DpiHelper::Scale(m_Tree.GetColumnWidth(index), USER_DEFAULT_SCREEN_DPI, dpi) });
	return columns;
}

void CWslView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	m_Tree.SetSortColumn(-1, 0);
	while (m_Tree.GetColumnCount() > 1)
		m_Tree.DeleteColumn(m_Tree.GetColumnCount() - 1);
	m_Columns = { Column::Name };
	for (auto& c : columns)
		if ((Column)c.Id != Column::Name)
			m_Columns.push_back((Column)c.Id);
	for (int i = 0; i < (int)m_Columns.size(); i++) {
		auto& def = s_Columns[(int)m_Columns[i]];
		auto shown = std::ranges::find(columns, (int)m_Columns[i], &ColumnLayout::ShownColumn::Id);
		int width = DpiHelper::Scale(shown != columns.end() ? shown->Width : def.Width, m_Tree.m_hWnd);
		if (i == 0 && m_Tree.GetColumnCount() == 1)
			m_Tree.SetColumnWidth(0, width);
		else
			m_Tree.InsertColumn(i, def.Name, width, def.Format);
		m_Tree.SetColumnSortable(i, true);
	}
	std::vector<int> order;
	for (auto& c : columns)
		order.push_back((int)(std::ranges::find(m_Columns, (Column)c.Id) - m_Columns.begin()));
	m_Tree.GetHeader().SetOrderArray((int)order.size(), order.data());
	auto sorted = std::ranges::find(m_Columns, (Column)m_SortColumn);
	if (m_SortColumn >= 0 && sorted != m_Columns.end())
		m_Tree.SetSortColumn((int)(sorted - m_Columns.begin()), m_SortAscending ? 1 : -1);
	else
		m_SortColumn = -1;
	m_Tree.Invalidate();
}

void CWslView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.WslColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CWslView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
	}
	return 0;
}

LRESULT CWslView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CWslView::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}
