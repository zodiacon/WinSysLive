#include "pch.h"
#include "resource.h"
#include <ListViewhelper.h>
#include <DpiHelper.h>
#include "UsersView.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <ToolbarHelper.h>
#include <WTLHelper.h>
#include <unordered_set>
#include <set>
#include "AppSettings.h"

using namespace WinSys;

namespace {
	// in CUsersView::Column order; the keys are stored in the UserColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"User / Process", L"Name", 260, LVCFMT_LEFT, true, true },
		{ L"PID", L"ProcessId", 70, LVCFMT_RIGHT, true },
		{ L"Session ID", L"SessionId", 80, LVCFMT_RIGHT, true },
		{ L"Session", L"Session", 110, LVCFMT_LEFT, true },
		{ L"State", L"State", 100, LVCFMT_LEFT, true },
		{ L"Client", L"Client", 120, LVCFMT_LEFT, true },
		{ L"Logon Time", L"LogonTime", 150, LVCFMT_LEFT, true },
		{ L"Processes", L"Processes", 80, LVCFMT_RIGHT, true },
		{ L"CPU (%)", L"CPU", 70, LVCFMT_RIGHT, true },
		{ L"Private Bytes", L"PrivateBytes", 110, LVCFMT_RIGHT, true },
		{ L"Working Set", L"WorkingSet", 110, LVCFMT_RIGHT, true },
		// version 1
		{ L"User", L"User", 160, LVCFMT_LEFT, true, false, nullptr, 1 },
		{ L"Logon ID", L"LogonId", 100, LVCFMT_LEFT, false, false, nullptr, 1 },
		{ L"Logon Type", L"LogonType", 110, LVCFMT_LEFT, false, false, nullptr, 1 },
	};

	std::wstring FormatLogonId(int64_t id) {
		auto high = (ULONG)(id >> 32), low = (ULONG)id;
		return high ? std::format(L"0x{:X}:{:08X}", high, low) : std::format(L"0x{:X}", low);
	}

	// LSA's FILETIME: none, never (the largest value), else the local time
	std::wstring FormatLogonTime(LARGE_INTEGER const& time) {
		if (time.QuadPart == 0 || time.QuadPart == MAXLONGLONG)
			return L"";
		return FormatHelper::FormatDateTime(time.QuadPart);
	}

	// accounts of Windows itself and of services, which run in users' sessions too (csrss, dwm, fontdrvhost...)
	bool IsSystemAccount(std::wstring const& account) {
		static const std::wstring_view domains[] = { L"NT AUTHORITY\\", L"NT SERVICE\\", L"Window Manager\\",
			L"Font Driver Host\\", L"IIS APPPOOL\\", L"NT VIRTUAL MACHINE\\" };
		return std::ranges::any_of(domains, [&](auto d) {
			return account.size() > d.size() && ::_wcsnicmp(account.c_str(), d.data(), d.size()) == 0;
		});
	}

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

	// the mode button beside the filter, checked while grouping by account
	const ToolBarButtonInfo s_ModeButton[] = {
		{ ID_USERS_GROUPBYACCOUNT, IDI_USER, BTNS_CHECK, L"Group by Account" },
	};
	const int ModeButtonImageSize = 16;	// at 96 DPI
}

CString CUsersView::GetTitle() const {
	return L"Users";
}

LRESULT CUsersView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	// the filter box goes in a rebar of the view's own, above the tree
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// the image list is the frame's, so the list view must not destroy it
	m_Tree.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Tree.SetCallbackMode(true);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());

	// the widths are at 96 DPI; the TreeListView doesn't scale them to a new DPI by itself
	ListViewHelper::EnableColumnDpiScaling(m_Tree.GetListCtrl());
	SetColumns(ColumnLayout::Parse(AppSettings::Get().UserColumns(), s_Columns));
	// a change refreshes as a tick does: only the matching processes are in the tree, and the selection is kept
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 280, L"Process, PID, user, session or client (Ctrl+F)",
		m_Tree.GetListCtrl(), [this](auto&) { OnRefresh(true); });
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	// Group by Account, beside the filter (as the Processes view's Hide button)
	m_ModeBar = ToolbarHelper::CreateAndInitToolBar(m_hWndToolBar, s_ModeButton, _countof(s_ModeButton), ModeButtonImageSize);
	m_ModeBarFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_ModeBar)));
	m_ModeBar.SetFont(m_ModeBarFont);
	m_ModeBar.AutoSize();
	CRect rcButton;
	m_ModeBar.GetItemRect(0, &rcButton);
	m_ModeButtonWidth = DpiHelper::Scale(rcButton.Width(), USER_DEFAULT_SCREEN_DPI, DpiHelper::GetDpi(m_ModeBar));
	SetModeButtonWidth();
	AddSimpleReBarBand(m_ModeBar, nullptr, FALSE);
	// two bands would get grippers; the filter keeps its width, the button's band takes the rest of the row
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE };
		rebar.GetBandInfo(i, &band);
		band.fStyle |= RBBS_NOGRIPPER;
		band.fMask = RBBIM_STYLE;
		if (band.hwndChild == m_Filter.GetHwnd()) {
			band.fMask |= RBBIM_CHILDSIZE;
			band.cxMinChild = m_Filter.GetWidth();
		}
		rebar.SetBandInfo(i, &band);
	}

	m_ByAccount = AppSettings::Get().UsersGroupByAccount();
	m_ModeBar.CheckButton(ID_USERS_GROUPBYACCOUNT, m_ByAccount);
	Rebuild();
	return 0;
}

void CUsersView::OnDpiChanged(UINT /*dpi*/) {
	// the frame's image list has the new DPI's size: the rows take its height
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	LayoutModeBar();
}

void CUsersView::UpdateLayout(BOOL resizeBars) {
	CViewBase::UpdateLayout(resizeBars);
	// the rebar gives a row's extra width to its first band: the button's band takes it, the filter keeps its width
	if (m_ModeBar) {
		CReBarCtrl rebar(m_hWndToolBar);
		rebar.MaximizeBand(rebar.GetBandCount() - 1, FALSE);
	}
}

void CUsersView::LayoutModeBar() {
	// the icon in the new size
	ToolbarHelper::UpdateToolBarImages(m_ModeBar, s_ModeButton, _countof(s_ModeButton), ModeButtonImageSize);
	m_ModeBarFont.DeleteObject();
	m_ModeBarFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_ModeBar)));
	m_ModeBar.SetFont(m_ModeBarFont);
	SetModeButtonWidth();
	// its band, as AddSimpleReBarBand sets it: the button's height, the width to its end
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_CHILD };
		if (rebar.GetBandInfo(i, &band) && band.hwndChild == m_ModeBar) {
			CRect rc;
			m_ModeBar.GetItemRect(0, &rc);
			band.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
			band.cxMinChild = band.cxIdeal = rc.right;
			band.cyMinChild = rc.Height();
			rebar.SetBandInfo(i, &band);
			break;
		}
	}
}

void CUsersView::SetModeButtonWidth() {
	TBBUTTONINFO info{ sizeof(info), TBIF_SIZE };
	info.cx = (WORD)DpiHelper::Scale(m_ModeButtonWidth, m_ModeBar.m_hWnd);
	m_ModeBar.SetButtonInfo(ID_USERS_GROUPBYACCOUNT, &info);
	m_ModeBar.AutoSize();
}

LRESULT CUsersView::OnGroupByAccount(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the button checks itself
	m_ByAccount = m_ModeBar.IsButtonChecked(ID_USERS_GROUPBYACCOUNT);
	auto& settings = AppSettings::Get();
	settings.UsersGroupByAccount(m_ByAccount);
	settings.Save();
	Rebuild();
	return 0;
}

void CUsersView::Rebuild() {
	// the sessions keep their details (slow to get, see SyncSessions); the rows lose their items
	m_Tree.BeginUpdate();
	m_Tree.DeleteAllItems();
	for (auto& [id, row] : m_Sessions)
		row.Item = nullptr;
	m_Processes.clear();
	m_Accounts.clear();
	m_Logons.clear();
	m_AddedGroups.clear();
	SyncSessions();
	SyncProcesses();
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();
	ExpandOwnGroup();
}

void CUsersView::ExpandOwnGroup() {
	HTLITEM hItem = nullptr;
	if (m_ByAccount) {
		// this process's account (and its logon, in case the user collapsed it)
		auto self = std::ranges::find_if(m_Processes, [](auto& pair) { return pair.second.Process->Id == ::GetCurrentProcessId(); });
		if (self != m_Processes.end()) {
			if (auto it = m_Logons.find({ self->second.Account, self->second.LogonId }); it != m_Logons.end())
				m_Tree.Expand(it->second.Item);
			if (auto it = m_Accounts.find(self->second.Account); it != m_Accounts.end())
				hItem = it->second.Item;
		}
	}
	else {
		DWORD session;
		if (::ProcessIdToSessionId(::GetCurrentProcessId(), &session))
			if (auto it = m_Sessions.find(session); it != m_Sessions.end())
				hItem = it->second.Item;
	}
	if (hItem)
		m_Tree.Expand(hItem);
}

void CUsersView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	if (active)
		m_Tree.SetFocus();
}

void CUsersView::OnFontChanged(HFONT font) {
	m_Tree.SetFont(font);
	m_Filter.SetFont(font);
}

bool CUsersView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	auto& p = *row.Process;
	if (CFilterBox::Contains(p.GetImageName(), filter) || CFilterBox::Contains(std::to_wstring(p.Id), filter))
		return true;
	// the account's processes, if the account matches (the account the process runs as, in either mode)
	if (CFilterBox::Contains(row.Account, filter))
		return true;
	// the session's processes, if the session matches
	auto session = m_Sessions.find(row.SessionId);
	if (session == m_Sessions.end())
		return false;
	auto& s = session->second.Session;
	return CFilterBox::Contains(GetSessionTitle(s), filter) || CFilterBox::Contains(s.Name, filter)
		|| CFilterBox::Contains(s.ClientName, filter);
}

LRESULT CUsersView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CUsersView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
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
	std::optional<std::wstring> selectedAccount;
	std::optional<std::pair<std::wstring, int64_t>> selectedLogon;
	std::optional<ProcessOrThreadKey> selectedProcess;
	if (selected) {
		if (selected->IsSession)
			selectedSession = selected->SessionId;
		else if (selected->IsAccount)
			selectedAccount = selected->Account;
		else if (selected->IsLogon)
			selectedLogon = std::pair{ selected->Account, selected->LogonId };
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
	else if (selectedAccount) {
		if (auto it = m_Accounts.find(*selectedAccount); it != m_Accounts.end())
			hSelect = it->second.Item;
	}
	else if (selectedLogon) {
		if (auto it = m_Logons.find(*selectedLogon); it != m_Logons.end())
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
		// the session's processes go with it (DeleteItem takes the subtree); by account, its processes are
		// under their accounts, and go as they exit
		if (it->second.Item) {
			std::erase_if(m_Processes, [=](auto& pair) { return pair.second.SessionId == id; });
			m_Tree.DeleteItem(it->second.Item);
		}
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
		// in the tree only while they're the groups (kept otherwise, for the filter and their details)
		if (!m_ByAccount && !row.Item) {
			row.Item = m_Tree.InsertItem(TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, image, reinterpret_cast<LPARAM>(&row));
			m_AddedGroups.push_back(row.Item);
		}
	}
}

std::wstring CUsersView::GetAccountTitle(std::wstring const& account) {
	// a process that can't be opened (another user's, without elevation)
	return account.empty() ? L"(Unknown)" : account;
}

WinSys::LogonSessionData const* CUsersView::GetLogon(int64_t logonId) const {
	if (logonId == 0)
		return nullptr;
	auto [it, added] = m_LogonDetails.try_emplace(logonId);
	if (added) {
		LUID luid{ (DWORD)logonId, (LONG)(logonId >> 32) };
		it->second = LsaSecurity::GetLogonSessionData(luid);
	}
	return it->second ? &*it->second : nullptr;
}

std::wstring CUsersView::GetLogonTitle(Row const& logon) const {
	// the token couldn't be opened
	if (logon.LogonId == 0)
		return L"(Unknown logon)";
	auto details = GetLogon(logon.LogonId);
	auto id = FormatLogonId(logon.LogonId);
	return details ? std::format(L"{} logon ({})", LsaSecurity::LogonTypeName(details->LogonType), id) : std::format(L"Logon {}", id);
}

bool CUsersView::IsOtherUser(Row const& process) const {
	if (process.Account.empty() || IsSystemAccount(process.Account))
		return false;
	auto session = m_Sessions.find(process.SessionId);
	if (session == m_Sessions.end() || session->second.Session.UserName.empty())
		return false;
	return ::_wcsicmp(GetSessionTitle(session->second.Session).c_str(), process.Account.c_str()) != 0;
}

LRESULT CUsersView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}
	auto cd = reinterpret_cast<NMTLVCUSTOMDRAW*>(hdr);
	if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
		return CDRF_NOTIFYITEMDRAW;
	if ((cd->nmcd.dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT && cd->hItem) {
		// another user's process in a session (Run As)
		auto& row = *reinterpret_cast<Row*>(m_Tree.GetItemData(cd->hItem));
		if (!row.IsGroup() && IsOtherUser(row)) {
			cd->clrText = WTLHelper::IsDarkMode() ? RGB(255, 190, 110) : RGB(170, 80, 0);
			return CDRF_NEWFONT;
		}
	}
	return CDRF_DODEFAULT;
}

void CUsersView::SyncProcesses() {
	auto reset = [](Row& group) {
		group.Processes = 0;
		group.CPU = 0;
		group.PrivateBytes = group.WorkingSet = 0;
	};
	for (auto& [id, session] : m_Sessions)
		reset(session);
	for (auto& [name, account] : m_Accounts)
		reset(account);
	for (auto& [key, logon] : m_Logons)
		reset(logon);
	// the account and logon rows' sessions, collected as their processes are counted
	std::unordered_map<Row*, std::set<uint32_t>> groupSessions;
	std::unordered_set<int64_t> liveLogons;

	auto& pm = Frame()->GetProcessManager();
	auto accountImage = Frame()->GetIconIndex(IDI_USER);
	auto logonImage = Frame()->GetIconIndex(IDI_SESSION);
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
			// once: it opens the process (unelevated) and looks up the SID, and ProcessInfoEx keeps it
			row.Account = p->GetUser();
			if (auto luid = p->GetLogonId())
				row.LogonId = ((int64_t)luid->HighPart << 32) | luid->LowPart;
		}
		if (row.LogonId)
			liveLogons.insert(row.LogonId);

		// the group: the session's row, or the account's logon's (added, with the account, as its first process comes)
		Row* group = &session->second;
		if (m_ByAccount) {
			auto [account, added] = m_Accounts.try_emplace(row.Account);
			auto& a = account->second;
			if (added) {
				a.IsAccount = true;
				a.Account = row.Account;
				a.Item = m_Tree.InsertItem(TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, accountImage, reinterpret_cast<LPARAM>(&a));
				m_AddedGroups.push_back(a.Item);
			}
			auto [logon, logonAdded] = m_Logons.try_emplace({ row.Account, row.LogonId });
			group = &logon->second;
			if (logonAdded) {
				// expanded: an account's row is what's collapsed
				group->IsLogon = true;
				group->Account = row.Account;
				group->LogonId = row.LogonId;
				group->Item = m_Tree.InsertItem(a.Item, TLV_LAST, LPSTR_TEXTCALLBACK, logonImage, reinterpret_cast<LPARAM>(group));
			}
			a.Processes++;
			a.CPU += p->CPU;
			a.PrivateBytes += info->PrivatePageCount;
			a.WorkingSet += info->WorkingSetSize;
			groupSessions[&a].insert(row.SessionId);
			groupSessions[group].insert(row.SessionId);
		}

		// in the tree only while it matches the filter
		if (bool show = Matches(row); show && !row.Item)
			row.Item = m_Tree.InsertItem(group->Item, TLV_LAST, LPSTR_TEXTCALLBACK, Frame()->GetProcessIcon(*p),
				reinterpret_cast<LPARAM>(&row));
		else if (!show && row.Item) {
			m_Tree.DeleteItem(row.Item);
			row.Item = nullptr;
		}

		group->Processes++;
		group->CPU += p->CPU;
		group->PrivateBytes += info->PrivatePageCount;
		group->WorkingSet += info->WorkingSetSize;
	}

	// processes that exited; their NativeInfo points into the snapshot buffer this update overwrote
	std::erase_if(m_Processes, [&](auto& pair) {
		if (live.contains(pair.first))
			return false;
		if (pair.second.Item)
			m_Tree.DeleteItem(pair.second.Item);
		return true;
		});

	// logons, then accounts, go with their last process (its row is gone already)
	auto keep = [&](Row& group) {
		if (group.Processes > 0) {
			group.Sessions.clear();
			for (auto id : groupSessions[&group])
				group.Sessions += (group.Sessions.empty() ? L"" : L", ") + std::to_wstring(id);
			return true;
		}
		m_Tree.DeleteItem(group.Item);
		return false;
	};
	std::erase_if(m_Logons, [&](auto& pair) { return !keep(pair.second); });
	std::erase_if(m_Accounts, [&](auto& pair) { return !keep(pair.second); });
	std::erase_if(m_LogonDetails, [&](auto& pair) { return !liveLogons.contains(pair.first); });

	// the TreeListView expands a new parent; new groups start collapsed
	for (auto hItem : m_AddedGroups)
		m_Tree.Expand(hItem, false);
	m_AddedGroups.clear();
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
	if (di->iSubItem < (int)m_Columns.size())
		wcsncpy_s(di->pszText, di->cchTextMax, GetColumnText(row, m_Columns[di->iSubItem]).c_str(), _TRUNCATE);
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
	if (row.IsLogon) {
		auto logon = GetLogon(row.LogonId);
		switch (column) {
			case Column::Name: return GetLogonTitle(row);
			case Column::SessionId: return row.Sessions;
			case Column::User: return row.Account;
			case Column::LogonId: return row.LogonId ? FormatLogonId(row.LogonId) : L"";
			case Column::LogonType: return logon ? LsaSecurity::LogonTypeName(logon->LogonType) : L"";
			case Column::LogonTime: return logon ? FormatLogonTime(logon->LogonTime) : L"";
			case Column::Processes: return std::to_wstring(row.Processes);
			case Column::CPU: return FormatHelper::FormatCpu(row.CPU);
			case Column::PrivateBytes: return FormatHelper::FormatSize(row.PrivateBytes);
			case Column::WorkingSet: return FormatHelper::FormatSize(row.WorkingSet);
		}
		return L"";
	}
	if (row.IsAccount) {
		switch (column) {
			case Column::Name: return GetAccountTitle(row.Account);
			case Column::SessionId: return row.Sessions;
			case Column::User: return row.Account;
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
		// by session, the parent has it
		case Column::SessionId: return m_ByAccount ? std::to_wstring(row.SessionId) : L"";
		case Column::User: return row.Account;
		case Column::LogonId: return row.LogonId ? FormatLogonId(row.LogonId) : L"";
		case Column::LogonType:
			if (auto logon = GetLogon(row.LogonId))
				return LsaSecurity::LogonTypeName(logon->LogonType);
			return L"";
		case Column::CPU: return FormatHelper::FormatCpu(p.CPU);
		case Column::PrivateBytes: return FormatHelper::FormatSize(info->PrivatePageCount);
		case Column::WorkingSet: return FormatHelper::FormatSize(info->WorkingSetSize);
	}
	return L"";
}

LRESULT CUsersView::OnSort(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ns = reinterpret_cast<NMTLVSORT*>(hdr);
	m_SortColumn = (int)m_Columns[ns->iSubItem];
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
	// siblings are all sessions, all accounts, all logons or all processes
	if (r1.IsSession) {
		switch (column) {
			case Column::SessionId: return compare(r1.SessionId, r2.SessionId);
			case Column::State: return compare(r1.Session.State, r2.Session.State);
			case Column::LogonTime: return compare(r1.Session.LogonTime, r2.Session.LogonTime);
		}
	}
	if (r1.IsLogon) {
		switch (column) {
			case Column::LogonId: return compare(r1.LogonId, r2.LogonId);
			case Column::LogonTime: {
				auto l1 = GetLogon(r1.LogonId), l2 = GetLogon(r2.LogonId);
				return compare(l1 ? l1->LogonTime.QuadPart : 0, l2 ? l2->LogonTime.QuadPart : 0);
			}
		}
	}
	if (r1.IsGroup()) {
		switch (column) {
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
			case Column::SessionId: return compare(r1.SessionId, r2.SessionId);
			case Column::LogonId: return compare(r1.LogonId, r2.LogonId);
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
	// the header's: told from a row's by the position (it comes up through the list view and the TreeListView)
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
	popup.EnableMenuItem(ID_USER_GOTOPROCESS, MF_BYCOMMAND | (row.IsGroup() ? MF_GRAYED : MF_ENABLED));
	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
}

LRESULT CUsersView::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& bHandled) {
	// a session expands (the TreeListView does that); a process is shown in the Processes tab
	auto row = GetSelectedRow();
	if (row && !row->IsGroup())
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
	if (row && !row->IsGroup())
		Frame()->GoToProcess(row->Process->Id);
	return 0;
}

LRESULT CUsersView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row)
		return 0;

	// the shown columns in display order, tab separated
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetColumnText(*row, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

void CUsersView::ShowError(PCWSTR operation) const {
	auto error = ::GetLastError();
	AtlMessageBox(m_hWnd, std::format(L"{} failed: {}", operation, Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

LRESULT CUsersView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CUsersView::GetShownColumns() const {
	int count = (int)m_Columns.size();
	std::vector<int> order(count);
	m_Tree.GetHeader().GetOrderArray(count, order.data());
	std::vector<ColumnLayout::ShownColumn> columns;
	auto dpi = DpiHelper::GetDpi(m_Tree);
	for (int index : order)
		columns.push_back({ (int)m_Columns[index], DpiHelper::Scale(m_Tree.GetColumnWidth(index), USER_DEFAULT_SCREEN_DPI, dpi) });
	return columns;
}

void CUsersView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
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

	// the sort stays if its column is still shown
	auto sorted = std::ranges::find(m_Columns, (Column)m_SortColumn);
	if (m_SortColumn >= 0 && sorted != m_Columns.end())
		m_Tree.SetSortColumn((int)(sorted - m_Columns.begin()), m_SortAscending ? 1 : -1);
	else
		m_SortColumn = -1;
	m_Tree.Invalidate();
}

void CUsersView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.UserColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CUsersView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
	}
	return 0;
}

LRESULT CUsersView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CUsersView::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the table's defaults (Parse of no layout), in their order and widths
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}
