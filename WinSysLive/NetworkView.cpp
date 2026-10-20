#include "pch.h"
#include "NetworkView.h"
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

	auto cm = GetColumnManager(m_List);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	cm->AddColumn(L"Process", LVCFMT_LEFT, width(180), Column::Process);
	cm->AddColumn(L"PID", LVCFMT_RIGHT, width(60), Column::Pid);
	cm->AddColumn(L"Protocol", LVCFMT_LEFT, width(65), Column::Protocol);
	cm->AddColumn(L"Local Address", LVCFMT_LEFT, width(170), Column::LocalAddress);
	cm->AddColumn(L"Local Port", LVCFMT_RIGHT, width(70), Column::LocalPort);
	cm->AddColumn(L"Remote Address", LVCFMT_LEFT, width(170), Column::RemoteAddress);
	cm->AddColumn(L"Remote Port", LVCFMT_RIGHT, width(80), Column::RemotePort);
	cm->AddColumn(L"State", LVCFMT_LEFT, width(90), Column::State);
	cm->AddColumn(L"Created", LVCFMT_LEFT, width(150), Column::Created);

	int filterWidth = width(360);
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Filter: process, PID, protocol, address, port or state (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, filterWidth);

	OnRefresh(false);
	return 0;
}

void CNetworkView::OnActivate(bool active) {
	m_Active = active;
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
	auto selected = GetSelectedKey();
	m_Rows.clear();
	// on the first read, all of them are new to the view, not opened
	ConnectionRows::Merge(m_All, std::move(*current), m_Loaded);
	m_Loaded = true;
	ShowRows(selected);
	if (active) {
		UpdateStatus();
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}
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

std::optional<WinSys::ConnectionKey> CNetworkView::GetSelectedKey() const {
	auto c = GetSelected();
	return c ? std::optional(c->Id) : std::nullopt;
}

void CNetworkView::ShowRows(std::optional<WinSys::ConnectionKey> const& selected) {
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

void CNetworkView::SelectConnection(WinSys::ConnectionKey const* key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (!key)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto c) { return c->Id == *key; });
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
	// the columns, tab separated
	std::wstring text;
	for (int column = 0; column < (int)Column::_Count; column++) {
		if (column > 0)
			text += L'\t';
		text += GetText(*c, (Column)column);
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
