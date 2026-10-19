#include "pch.h"
#include "resource.h"
#include "UsersView.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>
#include <unordered_set>

using namespace WinSys;

namespace {
	const struct {
		PCWSTR Name;
		int Width;
		int Format;
	} s_Columns[] = {	// in CUsersView::Column order
		{ L"User / Process", 260, LVCFMT_LEFT },
		{ L"PID", 70, LVCFMT_RIGHT },
		{ L"Session ID", 80, LVCFMT_RIGHT },
		{ L"Session", 110, LVCFMT_LEFT },
		{ L"State", 100, LVCFMT_LEFT },
		{ L"Client", 120, LVCFMT_LEFT },
		{ L"Logon Time", 150, LVCFMT_LEFT },
		{ L"Processes", 80, LVCFMT_RIGHT },
		{ L"CPU (%)", 70, LVCFMT_RIGHT },
		{ L"Private Bytes", 110, LVCFMT_RIGHT },
		{ L"Working Set", 110, LVCFMT_RIGHT },
	};

	PCWSTR SessionStateToString(SessionState state) {
		using enum SessionState;
		switch (state) {
			case Active: return L"Active";
			case Connected: return L"Connected";
			case ConnectQuery: return L"Connecting";
			case Shadow: return L"Shadowing";
			case Disconnected: return L"Disconnected";
			case Idle: return L"Idle";
			case Listen: return L"Listening";
			case Reset: return L"Reset";
			case Down: return L"Down";
			case Init: return L"Initializing";
		}
		return L"";
	}
}

CString CUsersView::GetTitle() const {
	return L"Users";
}

LRESULT CUsersView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_hWndClient = m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// the image list is the frame's, so the list view must not destroy it
	m_Tree.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Tree.SetCallbackMode(true);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());

	for (int i = 0; i < _countof(s_Columns); i++) {
		auto& c = s_Columns[i];
		m_Tree.InsertColumn(i, c.Name, c.Width, c.Format);
		m_Tree.SetColumnSortable(i, true);
	}

	m_Tree.BeginUpdate();
	SyncSessions();
	SyncProcesses();
	m_Tree.EndUpdate();

	// the user's own session starts expanded
	DWORD session;
	if (::ProcessIdToSessionId(::GetCurrentProcessId(), &session))
		if (auto it = m_Sessions.find(session); it != m_Sessions.end())
			m_Tree.Expand(it->second.Item);
	return 0;
}

void CUsersView::OnActivate(bool active) {
	if (active)
		m_Tree.SetFocus();
}

void CUsersView::OnThemeChanged(bool dark) {
	// the TreeListView doesn't follow WTLHelper's mode switch by itself
	m_Tree.SetBkColor(dark ? DarkMode::getViewBackgroundColor() : ::GetSysColor(COLOR_WINDOW));
	m_Tree.SetTextColor(dark ? DarkMode::getViewTextColor() : ::GetSysColor(COLOR_WINDOWTEXT));
	m_Tree.Invalidate();
}

void CUsersView::OnRefresh(bool /*active*/) {
	// the list view keeps the selection by index, which inserts, deletes and sorting shift: restore it by row
	auto selected = GetSelectedRow();
	std::optional<uint32_t> selectedSession;
	std::optional<ProcessOrThreadKey> selectedProcess;
	if (selected) {
		if (selected->IsSession)
			selectedSession = selected->SessionId;
		else
			selectedProcess = selected->Process->Key;
	}

	m_Tree.BeginUpdate();
	SyncSessions();
	SyncProcesses();
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();

	HTLITEM hSelect = nullptr;
	if (selectedSession) {
		if (auto it = m_Sessions.find(*selectedSession); it != m_Sessions.end())
			hSelect = it->second.Item;
	}
	else if (selectedProcess) {
		if (auto it = m_Processes.find(*selectedProcess); it != m_Processes.end())
			hSelect = it->second.Item;
	}
	if (hSelect)
		m_Tree.SelectItem(hSelect, false);
	else
		m_Tree.DeselectAll();
}

void CUsersView::SyncSessions() {
	auto sessions = SessionManager::EnumSessions(false);
	if (sessions.empty())
		return;	// keep what we have if enumeration failed

	for (auto it = m_Sessions.begin(); it != m_Sessions.end(); ) {
		auto id = it->first;
		if (std::ranges::any_of(sessions, [=](auto& s) { return s.Id == id; })) {
			++it;
			continue;
		}
		// the session's processes go with it (DeleteItem takes the subtree)
		std::erase_if(m_Processes, [=](auto& pair) { return pair.second.SessionId == id; });
		m_Tree.DeleteItem(it->second.Item);
		it = m_Sessions.erase(it);
	}

	auto image = Frame()->GetIconIndex(IDI_USERS);
	for (auto& session : sessions) {
		auto [it, inserted] = m_Sessions.try_emplace(session.Id);
		auto& row = it->second;
		row.IsSession = true;
		row.SessionId = session.Id;
		// the details (logon time, client) only when they may have changed: getting them is slow (see GetDetails)
		auto& old = row.Session;
		if (inserted || old.State != session.State || old.UserName != session.UserName || old.Domain != session.Domain)
			SessionManager::GetDetails(session);
		else {
			session.ClientName = std::move(old.ClientName);
			session.LogonTime = old.LogonTime;
			session.ConnectTime = old.ConnectTime;
			session.DisconnectTime = old.DisconnectTime;
		}
		row.Session = std::move(session);
		if (inserted) {
			row.Item = m_Tree.InsertItem(TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, image, reinterpret_cast<LPARAM>(&row));
			m_AddedSessions.push_back(row.Item);
		}
	}
}

void CUsersView::SyncProcesses() {
	for (auto& [id, session] : m_Sessions) {
		session.Processes = 0;
		session.CPU = 0;
		session.PrivateBytes = session.WorkingSet = 0;
	}

	auto& pm = Frame()->GetProcessManager();
	std::unordered_set<ProcessOrThreadKey> live;
	live.reserve(m_Processes.size() + 64);
	for (auto& p : pm.GetProcesses()) {
		// the Idle process isn't a process of any session (and its CPU is the idle time)
		if (p->Id == 0)
			continue;
		auto info = p->NativeInfo;
		auto session = m_Sessions.find(info->SessionId);
		if (session == m_Sessions.end())
			continue;

		live.insert(p->Key);
		auto [it, inserted] = m_Processes.try_emplace(p->Key);
		auto& row = it->second;
		if (inserted) {
			row.SessionId = info->SessionId;
			row.Process = p;
			row.Item = m_Tree.InsertItem(session->second.Item, TLV_LAST, LPSTR_TEXTCALLBACK, Frame()->GetProcessIcon(*p),
				reinterpret_cast<LPARAM>(&row));
		}

		auto& totals = session->second;
		totals.Processes++;
		totals.CPU += p->CPU;
		totals.PrivateBytes += info->PrivatePageCount;
		totals.WorkingSet += info->WorkingSetSize;
	}

	// processes that exited; their NativeInfo points into the snapshot buffer this update overwrote
	std::erase_if(m_Processes, [&](auto& pair) {
		if (live.contains(pair.first))
			return false;
		m_Tree.DeleteItem(pair.second.Item);
		return true;
		});

	// the TreeListView expands a new parent; new sessions start collapsed
	for (auto hItem : m_AddedSessions)
		m_Tree.Expand(hItem, false);
	m_AddedSessions.clear();
}

std::wstring CUsersView::GetSessionTitle(SessionInfo const& session) {
	if (!session.UserName.empty())
		return session.Domain.empty() ? session.UserName : session.Domain + L"\\" + session.UserName;
	// no one logged on: session 0 is "Services"
	return session.Name.empty() ? std::format(L"Session {}", session.Id) : session.Name;
}

LRESULT CUsersView::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto& row = *reinterpret_cast<Row*>(m_Tree.GetItemData(di->hItem));
	if (di->iSubItem < (int)Column::_Count)
		wcsncpy_s(di->pszText, di->cchTextMax, GetColumnText(row, (Column)di->iSubItem).c_str(), _TRUNCATE);
	return 0;
}

std::wstring CUsersView::GetColumnText(Row& row, Column column) const {
	if (row.IsSession) {
		auto& s = row.Session;
		switch (column) {
			case Column::Name: return GetSessionTitle(s);
			case Column::SessionId: return std::to_wstring(s.Id);
			case Column::Session: return s.Name;
			case Column::State: return SessionStateToString(s.State);
			case Column::Client: return s.ClientName;
			case Column::LogonTime: return FormatHelper::FormatDateTime(s.LogonTime);
			case Column::Processes: return std::to_wstring(row.Processes);
			case Column::CPU: return FormatHelper::FormatCpu(row.CPU);
			case Column::PrivateBytes: return FormatHelper::FormatSize(row.PrivateBytes);
			case Column::WorkingSet: return FormatHelper::FormatSize(row.WorkingSet);
		}
		return L"";
	}

	auto& p = *row.Process;
	auto info = p.NativeInfo;
	switch (column) {
		case Column::Name: return p.GetImageName();
		case Column::ProcessId: return std::to_wstring(p.Id);
		case Column::CPU: return FormatHelper::FormatCpu(p.CPU);
		case Column::PrivateBytes: return FormatHelper::FormatSize(info->PrivatePageCount);
		case Column::WorkingSet: return FormatHelper::FormatSize(info->WorkingSetSize);
	}
	return L"";
}

LRESULT CUsersView::OnSort(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ns = reinterpret_cast<NMTLVSORT*>(hdr);
	m_SortColumn = ns->iSubItem;
	m_SortAscending = ns->iDirection > 0;

	auto selected = m_Tree.GetSelectedItem();
	Sort();
	if (selected)
		m_Tree.SelectItem(selected, false);
	return 0;
}

void CUsersView::Sort() {
	// the sessions, and the processes within each
	m_Tree.SortChildren(TLV_ROOT, CompareItems, reinterpret_cast<LPARAM>(this), true);
}

int CALLBACK CUsersView::CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam) {
	auto view = reinterpret_cast<CUsersView*>(lParam);
	auto& r1 = *reinterpret_cast<Row*>(view->m_Tree.GetItemData(h1));
	auto& r2 = *reinterpret_cast<Row*>(view->m_Tree.GetItemData(h2));
	auto result = view->CompareRows(r1, r2);
	return view->m_SortAscending ? result : -result;
}

int CUsersView::CompareRows(Row& r1, Row& r2) const {
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	auto column = (Column)m_SortColumn;
	// siblings are all sessions or all processes
	if (r1.IsSession) {
		switch (column) {
			case Column::SessionId: return compare(r1.SessionId, r2.SessionId);
			case Column::State: return compare(r1.Session.State, r2.Session.State);
			case Column::LogonTime: return compare(r1.Session.LogonTime, r2.Session.LogonTime);
			case Column::Processes: return compare(r1.Processes, r2.Processes);
			case Column::CPU: return compare(r1.CPU, r2.CPU);
			case Column::PrivateBytes: return compare(r1.PrivateBytes, r2.PrivateBytes);
			case Column::WorkingSet: return compare(r1.WorkingSet, r2.WorkingSet);
		}
	}
	else {
		auto i1 = r1.Process->NativeInfo, i2 = r2.Process->NativeInfo;
		switch (column) {
			case Column::ProcessId: return compare(r1.Process->Id, r2.Process->Id);
			case Column::CPU: return compare(r1.Process->CPU, r2.Process->CPU);
			case Column::PrivateBytes: return compare(i1->PrivatePageCount, i2->PrivatePageCount);
			case Column::WorkingSet: return compare(i1->WorkingSetSize, i2->WorkingSetSize);
		}
	}
	return ::_wcsicmp(GetColumnText(r1, column).c_str(), GetColumnText(r2, column).c_str());
}

CUsersView::Row* CUsersView::GetSelectedRow() const {
	auto hItem = m_Tree.GetSelectedItem();
	return hItem ? reinterpret_cast<Row*>(m_Tree.GetItemData(hItem)) : nullptr;
}

LRESULT CUsersView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
	auto row = GetSelectedRow();
	if (ia->iItem < 0 || !row)
		return 0;

	// ptAction is relative to the inner list view
	CPoint pt(ia->ptAction);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowContextMenu(*row, pt);
	return 0;
}

LRESULT CUsersView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	// from the keyboard (Shift+F10, the menu key) the position is -1, -1; a mouse click has its menu from NM_RCLICK
	auto hItem = m_Tree.GetSelectedItem();
	if (GET_X_LPARAM(lParam) != -1 || GET_Y_LPARAM(lParam) != -1 || !hItem) {
		bHandled = FALSE;
		return 0;
	}
	// below the selected row's name
	CRect rc;
	m_Tree.GetItemRect(hItem, &rc);
	CPoint pt(rc.left + 40, rc.bottom);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowContextMenu(*reinterpret_cast<Row*>(m_Tree.GetItemData(hItem)), pt);
	return 0;
}

void CUsersView::ShowContextMenu(Row const& row, POINT pt) {
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CMenuHandle popup = menu.GetSubMenu(3);
	bool session = row.IsSession;
	bool loggedOn = session && !row.Session.UserName.empty();
	popup.EnableMenuItem(ID_USER_DISCONNECT, MF_BYCOMMAND | (loggedOn && row.Session.State == SessionState::Active ? MF_ENABLED : MF_GRAYED));
	popup.EnableMenuItem(ID_USER_LOGOFF, MF_BYCOMMAND | (loggedOn ? MF_ENABLED : MF_GRAYED));
	popup.EnableMenuItem(ID_USER_GOTOPROCESS, MF_BYCOMMAND | (session ? MF_GRAYED : MF_ENABLED));
	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
}

LRESULT CUsersView::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& bHandled) {
	// a session expands (the TreeListView does that); a process is shown in the Processes tab
	auto row = GetSelectedRow();
	if (row && !row->IsSession)
		Frame()->GoToProcess(row->Process->Id);
	else
		bHandled = FALSE;
	return 0;
}

LRESULT CUsersView::OnDisconnect(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row || !row->IsSession)
		return 0;

	auto text = std::format(L"Disconnect {} (session {})? The programs keep running, and the user can connect again.",
		GetSessionTitle(row->Session), row->SessionId);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
		return 0;
	if (!SessionManager::Disconnect(row->SessionId))
		ShowError(L"Disconnecting the session");
	return 0;
}

LRESULT CUsersView::OnLogoff(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row || !row->IsSession)
		return 0;

	DWORD current = 0;
	::ProcessIdToSessionId(::GetCurrentProcessId(), &current);
	auto text = std::format(L"Log off {} (session {})? Unsaved work in their programs will be lost.{}",
		GetSessionTitle(row->Session), row->SessionId, row->SessionId == current ? L"\n\nThis is your own session." : L"");
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;
	if (!SessionManager::Logoff(row->SessionId))
		ShowError(L"Logging off the session");
	return 0;
}

LRESULT CUsersView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (row && !row->IsSession)
		Frame()->GoToProcess(row->Process->Id);
	return 0;
}

LRESULT CUsersView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row)
		return 0;

	// all columns, tab separated
	std::wstring text;
	for (int i = 0; i < (int)Column::_Count; i++) {
		if (i > 0)
			text += L'\t';
		text += GetColumnText(*row, (Column)i);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

void CUsersView::ShowError(PCWSTR operation) const {
	auto error = ::GetLastError();
	AtlMessageBox(m_hWnd, std::format(L"{} failed: {}", operation, Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
}
