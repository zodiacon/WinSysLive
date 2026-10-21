#include "pch.h"
#include "NetworkView.h"
#include "AppSettings.h"

namespace {
	// in CNetworkView::Column order; the keys are stored in the NetworkColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Process", L"Process", 180, LVCFMT_LEFT, true, true },
		{ L"PID", L"Pid", 60, LVCFMT_RIGHT, true },
		{ L"Protocol", L"Protocol", 65, LVCFMT_LEFT, true },
		{ L"Local Address", L"LocalAddress", 170, LVCFMT_LEFT, true },
		{ L"Local Port", L"LocalPort", 70, LVCFMT_RIGHT, true },
		{ L"Remote Address", L"RemoteAddress", 170, LVCFMT_LEFT, true },
		{ L"Remote Port", L"RemotePort", 80, LVCFMT_RIGHT, true },
		{ L"State", L"State", 90, LVCFMT_LEFT, true },
		{ L"Created", L"Created", 150, LVCFMT_LEFT, true },
	};
}
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>

CString CNetworkView::GetTitle() const {
	return L"Network";
}

LRESULT CNetworkView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	// the frame's image list, for the processes' icons
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().NetworkColumns(), s_Columns));

	int filterWidth = 360;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Filter: process, PID, protocol, address, port or state (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	OnRefresh(false);
	return 0;
}

void CNetworkView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active && m_RowsPending)
		ShowRows(m_PendingSelection);
	// the status line's count is this view's
	if (active) {
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CNetworkView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CNetworkView::OnRefresh(bool active) {
	// also while hidden: a few milliseconds, and the highlighting stays right
	auto current = WinSys::ConnectionInfo::Read();
	if (!current)
		return;	// keep what we have
	// the rows point into m_All, which the merge replaces: the selection is taken first, and the rows are rebuilt
	// (while hidden, when the view is shown: filtering and sorting format the rows' texts)
	auto selected = m_RowsPending ? m_PendingSelection : GetSelectedKey();
	m_Rows.clear();
	// on the first read, all of them are new to the view, not opened
	ConnectionRows::Merge(m_All, std::move(*current), m_Loaded);
	m_Loaded = true;
	if (!active) {
		m_RowsPending = true;
		m_PendingSelection = selected;
		return;
	}
	ShowRows(selected);
	UpdateStatus();
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

std::wstring CNetworkView::GetProcessName(uint32_t pid) const {
	// TIME_WAIT connections belong to no process anymore
	return pid == 0 ? L"" : Frame()->GetProcessManager().GetProcessNameById(pid);
}

bool CNetworkView::Matches(Connection const& c) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Process, Column::Pid, Column::Protocol, Column::LocalAddress, Column::LocalPort,
		Column::RemoteAddress, Column::RemotePort, Column::State })
		if (CFilterBox::Contains(GetText(c, column), filter))
			return true;
	return false;
}

std::optional<ConnectionRows::Key> CNetworkView::GetSelectedKey() const {
	auto c = GetSelected();
	return c ? std::optional(ConnectionRows::KeyOf(*c)) : std::nullopt;
}

void CNetworkView::ShowRows(std::optional<ConnectionRows::Key> const& selected) {
	m_RowsPending = false;
	m_Rows.clear();
	for (auto& c : m_All)
		if (Matches(c))
			m_Rows.push_back(&c);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectConnection(selected ? &*selected : nullptr);
	m_List.Invalidate();
}

void CNetworkView::UpdateStatus() {
	if (!m_Active)
		return;
	auto tcp = std::ranges::count_if(m_All, [](auto& c) { return !c.Closed && c.IsTcp(); });
	auto udp = std::ranges::count_if(m_All, [](auto& c) { return !c.Closed && !c.IsTcp(); });
	auto text = std::format(L"{} TCP connection{}, {} UDP endpoint{}", tcp, tcp == 1 ? L"" : L"s", udp, udp == 1 ? L"" : L"s");
	if (!m_Filter.GetText().empty())
		text += std::format(L"; {} match the filter", std::ranges::count_if(m_Rows, [](auto c) { return !c->Closed; }));
	Frame()->SetStatusText(0, text.c_str());
}

std::wstring CNetworkView::GetText(Connection const& c, Column column) const {
	switch (column) {
		case Column::Process: return GetProcessName(c.Id.Pid);
		case Column::Pid: return std::to_wstring(c.Id.Pid);
		case Column::Protocol: return WinSys::ConnectionInfo::ProtocolName(c.Id.Proto);
		case Column::LocalAddress: return c.LocalText;
		case Column::LocalPort: return std::to_wstring(c.Id.LocalPort);
		case Column::RemoteAddress: return c.RemoteText;
		case Column::RemotePort: return c.RemoteText.empty() ? L"" : std::to_wstring(c.Id.RemotePort);
		case Column::State: return WinSys::ConnectionInfo::StateName(c);
		case Column::Created: return c.Created ? FormatHelper::FormatDateTime(c.Created) : L"";
	}
	return L"";
}

CString CNetworkView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CNetworkView::GetRowImage(HWND, int row, int col) const {
	// the process's icon, by the process name wherever that column was dragged
	if (row >= (int)m_Rows.size() || GetColumnManager(m_List)->GetColumnTag<Column>(col) != Column::Process)
		return -1;
	auto pid = m_Rows[row]->Id.Pid;
	auto process = pid ? Frame()->GetProcessManager().GetProcessById(pid) : nullptr;
	return process ? Frame()->GetProcessIcon(*process) : -1;
}

int CNetworkView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by connection (ShowRows, DoSort)
	return -1;
}

void CNetworkView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	switch (column) {
		case Column::Pid: case Column::Protocol: case Column::LocalAddress: case Column::LocalPort:
		case Column::RemoteAddress: case Column::RemotePort: case Column::Created:
			break;
		default: {
			// a text column: each row's text formatted once (a process name is a lookup), not twice per comparison
			std::vector<std::pair<std::wstring, Connection const*>> texts;
			texts.reserve(m_Rows.size());
			for (auto c : m_Rows)
				texts.push_back({ GetText(*c, column), c });
			std::ranges::stable_sort(texts, [&](auto const& t1, auto const& t2) {
				int result = ::_wcsicmp(t1.first.c_str(), t2.first.c_str());
				return si->SortAscending ? result < 0 : result > 0;
				});
			for (size_t i = 0; i < texts.size(); i++)
				m_Rows[i] = texts[i].second;
			SelectConnection(selected ? &*selected : nullptr);
			return;
		}
	}
	std::ranges::stable_sort(m_Rows, [&](Connection const* c1, Connection const* c2) {
		int result = 0;
		switch (column) {
			case Column::Pid: result = compare(c1->Id.Pid, c2->Id.Pid); break;
			case Column::Protocol: result = compare(c1->Id.Proto, c2->Id.Proto); break;
			// by the address's bytes, so 10.0.0.9 comes before 10.0.0.10
			case Column::LocalAddress: result = compare(c1->Id.LocalAddress, c2->Id.LocalAddress); break;
			case Column::LocalPort: result = compare(c1->Id.LocalPort, c2->Id.LocalPort); break;
			case Column::RemoteAddress: result = compare(c1->Id.RemoteAddress, c2->Id.RemoteAddress); break;
			case Column::RemotePort: result = compare(c1->Id.RemotePort, c2->Id.RemotePort); break;
			case Column::Created: result = compare(c1->Created, c2->Created); break;
			default: result = ::_wcsicmp(GetText(*c1, column).c_str(), GetText(*c2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectConnection(selected ? &*selected : nullptr);
}

CNetworkView::Connection const* CNetworkView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CNetworkView::SelectConnection(ConnectionRows::Key const* key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (!key)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto c) { return ConnectionRows::KeyOf(*c) == *key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

LRESULT CNetworkView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			auto& c = *m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and exited threads and handles in the lower pane
			if (c.Closed) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (c.NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CNetworkView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (m_Rows[row]->Id.Pid ? 0 : MF_GRAYED), ID_NETWORK_GOTOPROCESS, L"&Go to Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.SetMenuDefaultItem(ID_NETWORK_GOTOPROCESS);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CNetworkView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	if (auto pid = m_Rows[row]->Id.Pid)
		Frame()->GoToProcess(pid);
	return true;
}

LRESULT CNetworkView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto c = GetSelected(); c && c->Id.Pid)
		Frame()->GoToProcess(c->Id.Pid);
	return 0;
}

LRESULT CNetworkView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto c = GetSelected();
	if (!c)
		return 0;
	// the shown columns in display order, tab separated
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetText(*c, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CNetworkView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CNetworkView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CNetworkView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CNetworkView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CNetworkView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CNetworkView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.NetworkColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CNetworkView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CNetworkView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CNetworkView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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
