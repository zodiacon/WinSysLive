#include "pch.h"
#include "IpcView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>

namespace {
	// in CIpcView::Column order; the keys are stored in the IpcColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Client", L"Client", 180, LVCFMT_LEFT, true, true },
		{ L"Client PID", L"ClientPid", 75, LVCFMT_RIGHT, true },
		{ L"Server", L"Server", 180, LVCFMT_LEFT, true },
		{ L"Server PID", L"ServerPid", 75, LVCFMT_RIGHT, true },
		{ L"Kind", L"Kind", 55, LVCFMT_LEFT, true },
		{ L"Interface", L"Interface", 280, LVCFMT_LEFT, true },
		{ L"Endpoint", L"Endpoint", 200, LVCFMT_LEFT, true },
		{ L"Protocol", L"Protocol", 80, LVCFMT_LEFT, true },
		{ L"Count", L"Count", 90, LVCFMT_RIGHT, true },
		{ L"Rate (/s)", L"Rate", 75, LVCFMT_RIGHT, true },
		{ L"First Seen", L"FirstSeen", 150, LVCFMT_LEFT, false },
		{ L"Last Seen", L"LastSeen", 150, LVCFMT_LEFT, true },
	};

	// the session's name is this and the process ID
	const PCWSTR SessionPrefix = L"WinSysLive IPC";
	// highlighting of new rows lasts this many refreshes
	const int NewRowTicks = 2;

	std::wstring GuidText(GUID const& iface) {
		WCHAR text[64];
		return ::StringFromGUID2(iface, text, _countof(text)) ? text : L"";
	}
}

CString CIpcView::GetTitle() const {
	return L"RPC/ALPC";
}

LRESULT CIpcView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	// the frame's image list, for the processes' icons
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().IpcColumns(), s_Columns));

	int filterWidth = 280;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Process, PID, interface, endpoint (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

void CIpcView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		// the session costs a little all the time: it starts only when the tab is first shown
		StartTracker();
		if (m_RowsPending)
			ShowRows(m_PendingSelection);
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CIpcView::StartTracker() {
	if (m_Tracker || m_TrackerError)
		return;
	auto tracker = std::make_unique<WinSys::IpcTracker>();
	if (tracker->Start(SessionPrefix))
		m_Tracker = std::move(tracker);
	else
		m_TrackerError = ::GetLastError();
}

void CIpcView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CIpcView::OnRefresh(bool active) {
	if (!m_Tracker)
		return;
	// an exited process's ID may be reused: its rows go
	for (auto& p : Frame()->GetProcessManager().GetTerminatedProcesses())
		m_Tracker->RemoveProcess(p->Id);

	LARGE_INTEGER now, frequency;
	::QueryPerformanceCounter(&now);
	::QueryPerformanceFrequency(&frequency);
	double elapsed = m_LastMerge ? double(now.QuadPart - m_LastMerge) / frequency.QuadPart : 0;
	m_LastMerge = now.QuadPart;

	// the rows point into m_All, which the merge replaces: the selection is taken first, and the rows are rebuilt
	// (while hidden, when the view is shown)
	auto selected = m_RowsPending ? m_PendingSelection : GetSelectedKey();
	m_Rows.clear();
	Merge(m_Tracker->GetLinks(), elapsed);
	m_Loaded = true;
	if (!active) {
		m_RowsPending = true;
		m_PendingSelection = selected;
		return;
	}
	ShowRows(selected);
	UpdateStatus();
}

std::wstring CIpcView::KeyOf(WinSys::IpcLink const& link) {
	if (link.Kind == WinSys::IpcKind::Alpc)
		return std::format(L"A|{}|{}", link.ClientPid, link.ServerPid);
	return std::format(L"R|{}|{}|{}|{}|{}", link.ClientPid, GuidText(link.Interface), (uint32_t)link.Protocol, link.Endpoint, link.NetworkAddress);
}

void CIpcView::Merge(std::vector<WinSys::IpcLink> links, double elapsed) {
	std::unordered_map<std::wstring, Row const*> previous;
	previous.reserve(m_All.size());
	for (auto& row : m_All)
		previous[row.Key] = &row;

	std::vector<Row> merged;
	merged.reserve(links.size());
	for (auto& link : links) {
		Row row{ std::move(link) };
		row.Key = KeyOf(row.Link);
		uint64_t before = 0;
		if (auto it = previous.find(row.Key); it != previous.end()) {
			row.NewTicks = (std::max)(it->second->NewTicks - 1, 0);
			before = it->second->Link.Count;
		}
		else if (m_Loaded)
			// on the first read, they're all new to the view, not to the system
			row.NewTicks = NewRowTicks;
		if (elapsed > 0 && row.Link.Count >= before && (before || m_Loaded))
			row.Rate = (row.Link.Count - before) / elapsed;
		merged.push_back(std::move(row));
	}
	m_All = std::move(merged);
}

std::wstring CIpcView::GetProcessName(uint32_t pid) const {
	return pid == 0 ? L"" : Frame()->GetProcessManager().GetProcessNameById(pid);
}

std::wstring const& CIpcView::GetInterfaceName(GUID const& iface) const {
	// a registry read for a COM interface: once per interface
	auto key = GuidText(iface);
	auto it = m_InterfaceNames.find(key);
	if (it == m_InterfaceNames.end()) {
		auto name = WinSys::IpcTracker::GetInterfaceName(iface);
		it = m_InterfaceNames.emplace(key, name.empty() ? key : std::format(L"{} {}", name, key)).first;
	}
	return it->second;
}

std::wstring CIpcView::GetText(Row const& row, Column column) const {
	auto& link = row.Link;
	bool rpc = link.Kind == WinSys::IpcKind::Rpc;
	switch (column) {
		case Column::Client: return GetProcessName(link.ClientPid);
		case Column::ClientPid: return std::to_wstring(link.ClientPid);
		// a call to another machine: its address
		case Column::Server: return link.ServerPid ? GetProcessName(link.ServerPid) : link.NetworkAddress;
		case Column::ServerPid: return link.ServerPid ? std::to_wstring(link.ServerPid) : L"";
		case Column::Kind: return rpc ? L"RPC" : L"ALPC";
		case Column::Interface: return rpc ? GetInterfaceName(link.Interface) : L"";
		case Column::Endpoint: return link.Endpoint;
		case Column::Protocol: return rpc ? WinSys::IpcTracker::ProtocolName(link.Protocol) : L"";
		case Column::Count: return FormatHelper::FormatNumber((int64_t)link.Count);
		case Column::Rate: return row.Rate >= 0.05 ? std::format(L"{:.1f}", row.Rate) : L"";
		case Column::FirstSeen: return link.FirstSeen ? FormatHelper::FormatDateTime(link.FirstSeen) : L"";
		case Column::LastSeen: return link.LastSeen ? FormatHelper::FormatDateTime(link.LastSeen) : L"";
	}
	return L"";
}

bool CIpcView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Client, Column::ClientPid, Column::Server, Column::ServerPid, Column::Kind,
		Column::Interface, Column::Endpoint, Column::Protocol })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

std::wstring CIpcView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? row->Key : L"";
}

CIpcView::Row const* CIpcView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CIpcView::ShowRows(std::wstring const& selected) {
	m_RowsPending = false;
	m_Rows.clear();
	for (auto& row : m_All)
		if (Matches(row))
			m_Rows.push_back(&row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectRow(selected);
	m_List.Invalidate();
}

void CIpcView::SelectRow(std::wstring const& key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (key.empty())
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Key == key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CIpcView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (!m_Tracker)
		text = m_TrackerError == ERROR_ACCESS_DENIED
			? L"RPC/ALPC monitoring needs administrator rights (File > Run as Administrator)"
			: std::format(L"Starting RPC/ALPC monitoring failed: {}", WinSys::Helpers::GetErrorText(m_TrackerError));
	else {
		auto alpc = std::ranges::count_if(m_All, [](auto& r) { return r.Link.Kind == WinSys::IpcKind::Alpc; });
		text = std::format(L"{} ALPC pairs, {} RPC links", alpc, m_All.size() - alpc);
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
		text += L"; since " + FormatHelper::FormatDateTime(m_Tracker->GetStartTime());
	}
	Frame()->SetStatusText(0, text.c_str());
}

CString CIpcView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CIpcView::GetRowImage(HWND, int row, int col) const {
	// the client's icon, wherever that column was dragged
	if (row >= (int)m_Rows.size() || GetColumnManager(m_List)->GetColumnTag<Column>(col) != Column::Client)
		return -1;
	auto process = Frame()->GetProcessManager().GetProcessById(m_Rows[row]->Link.ClientPid);
	return process ? Frame()->GetProcessIcon(*process) : -1;
}

int CIpcView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by row key (ShowRows, DoSort)
	return -1;
}

void CIpcView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	switch (column) {
		case Column::ClientPid: case Column::ServerPid: case Column::Count: case Column::Rate:
		case Column::FirstSeen: case Column::LastSeen:
			std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
				auto& l1 = r1->Link;
				auto& l2 = r2->Link;
				int result = 0;
				switch (column) {
					case Column::ClientPid: result = compare(l1.ClientPid, l2.ClientPid); break;
					case Column::ServerPid: result = compare(l1.ServerPid, l2.ServerPid); break;
					case Column::Count: result = compare(l1.Count, l2.Count); break;
					case Column::Rate: result = compare(r1->Rate, r2->Rate); break;
					case Column::FirstSeen: result = compare(l1.FirstSeen, l2.FirstSeen); break;
					case Column::LastSeen: result = compare(l1.LastSeen, l2.LastSeen); break;
				}
				return si->SortAscending ? result < 0 : result > 0;
				});
			break;
		default: {
			// a text column: each row's text formatted once (a process name is a lookup), not twice per comparison
			std::vector<std::pair<std::wstring, Row const*>> texts;
			texts.reserve(m_Rows.size());
			for (auto row : m_Rows)
				texts.push_back({ GetText(*row, column), row });
			std::ranges::stable_sort(texts, [&](auto const& t1, auto const& t2) {
				int result = ::_wcsicmp(t1.first.c_str(), t2.first.c_str());
				return si->SortAscending ? result < 0 : result > 0;
				});
			for (size_t i = 0; i < texts.size(); i++)
				m_Rows[i] = texts[i].second;
			break;
		}
	}
	SelectRow(selected);
}

LRESULT CIpcView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			if (index < 0 || index >= (int)m_Rows.size() || m_Rows[index]->NewTicks == 0)
				break;
			// the Network tab's colors for new rows
			bool dark = WTLHelper::IsDarkMode();
			cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CIpcView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	auto& link = m_Rows[row]->Link;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_IPC_GOTOCLIENT, L"Go to &Client");
	menu.AppendMenu(MF_STRING | (link.ServerPid ? 0 : MF_GRAYED), ID_IPC_GOTOSERVER, L"Go to &Server");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"C&opy\tCtrl+C");
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CIpcView::OnDoubleClickList(HWND, int row, int col, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	// the server, on its columns (when it's known); the client otherwise
	auto& link = m_Rows[row]->Link;
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(col);
	bool server = (column == Column::Server || column == Column::ServerPid) && link.ServerPid;
	Frame()->GoToProcess(server ? link.ServerPid : link.ClientPid);
	return true;
}

LRESULT CIpcView::OnGoToClient(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		Frame()->GoToProcess(row->Link.ClientPid);
	return 0;
}

LRESULT CIpcView::OnGoToServer(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && row->Link.ServerPid)
		Frame()->GoToProcess(row->Link.ServerPid);
	return 0;
}

LRESULT CIpcView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row)
		return 0;
	// the shown columns in display order, tab separated
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetText(*row, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CIpcView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CIpcView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CIpcView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	m_Tracker.reset();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CIpcView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CIpcView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CIpcView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.IpcColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CIpcView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CIpcView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CIpcView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	ShowRows(GetSelectedKey());
	return 0;
}
