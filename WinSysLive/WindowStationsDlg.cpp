#include "pch.h"
#include "WindowStationsDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	// a tree item's data: the station's index and the desktop's plus one (0: the station), or -1 for a session
	LPARAM ItemData(int station, int desktop) {
		return (LPARAM)station << 16 | (LPARAM)(desktop + 1);
	}
}

LRESULT CWindowStationsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_WINSTA, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_WINSTA, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_Tree.Attach(GetDlgItem(IDC_WS_TREE));
	if (m_Images)
		m_Tree.SetImageList(m_Images, TVSIL_NORMAL);
	m_List.Attach(GetDlgItem(IDC_WS_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"#", 45, LVCFMT_RIGHT }, { L"Handle", 90, LVCFMT_RIGHT }, { L"Title", 260, LVCFMT_LEFT }, { L"Class", 200, LVCFMT_LEFT },
		{ L"Process", 140, LVCFMT_LEFT }, { L"PID", 60, LVCFMT_RIGHT }, { L"TID", 60, LVCFMT_RIGHT }, { L"State", 120, LVCFMT_LEFT },
		{ L"Rectangle", 170, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, ::MulDiv(columns[i].Width, dpi, 96));
	// the list view ignores column 0's alignment on insertion
	LVCOLUMN col{ LVCF_FMT };
	col.fmt = LVCFMT_RIGHT;
	m_List.SetColumn(0, &col);
	ListViewHelper::EnableColumnDpiScaling(m_List);
	CheckDlgButton(IDC_WS_VISIBLE, m_VisibleOnly ? BST_CHECKED : BST_UNCHECKED);
	::SendDlgItemMessage(m_hWnd, IDC_WS_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by title, class, process or handle");

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CWindowStationsDlg::Load() {
	CWaitCursor wait;
	// the selection, by station (session and name) and desktop name
	std::wstring station, desktop;
	uint32_t session = 0;
	if (auto [s, d] = GetSelection(); s >= 0) {
		station = m_Stations[s].Name;
		session = m_Stations[s].Session;
		if (d >= 0)
			desktop = m_Stations[s].Desktops[d].Name;
	}
	m_Stations.clear();
	if (auto stations = WinSys::WindowStations::Enum())
		m_Stations = std::move(*stations);

	auto icon = [&](UINT id) { return m_IconIndex ? m_IconIndex(id) : -1; };
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	HTREEITEM select = nullptr, own = nullptr;
	HTREEITEM sessionItem = nullptr;
	uint32_t lastSession = UINT32_MAX;
	for (int s = 0; s < (int)m_Stations.size(); s++) {
		auto& st = m_Stations[s];
		if (st.Session != lastSession) {
			lastSession = st.Session;
			auto text = std::format(L"Session {}{}", st.Session, st.CurrentSession ? L" (this session)" : L"");
			sessionItem = m_Tree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, text.c_str(),
				icon(IDI_SESSION), icon(IDI_SESSION), 0, 0, -1, TVI_ROOT, TVI_LAST);
		}
		auto name = st.Name + (st.Own ? L" (this process's)" : L"") + (st.Visible ? L" (interactive)" : L"");
		auto stationItem = m_Tree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, name.c_str(),
			icon(IDI_WINSTA), icon(IDI_WINSTA), 0, 0, ItemData(s, -1), sessionItem, TVI_LAST);
		if (st.Name == station && st.Session == session && desktop.empty())
			select = stationItem;
		for (int d = 0; d < (int)st.Desktops.size(); d++) {
			auto& dt = st.Desktops[d];
			auto text = dt.Name;
			if (dt.Input)
				text += L" (input)";
			if (dt.Opened && dt.HeapSizeKB)
				text += std::format(L" ({} KB heap)", FormatHelper::FormatNumber(dt.HeapSizeKB));
			auto item = m_Tree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, text.c_str(),
				icon(IDI_DESKTOP), icon(IDI_DESKTOP), 0, 0, ItemData(s, d), stationItem, TVI_LAST);
			if (st.Name == station && st.Session == session && dt.Name == desktop)
				select = item;
			// at first, this process's input desktop
			if (st.Own && dt.Input)
				own = item;
		}
		if (st.CurrentSession)
			m_Tree.Expand(stationItem);
		m_Tree.Expand(sessionItem);
	}
	m_Tree.SetRedraw(TRUE);
	if (!select)
		select = own;
	if (select) {
		m_Tree.SelectItem(select);
		m_Tree.EnsureVisible(select);
	}
	else
		LoadWindows();
}

std::pair<int, int> CWindowStationsDlg::GetSelection() const {
	auto item = m_Tree ? m_Tree.GetSelectedItem() : nullptr;
	if (!item)
		return { -1, -1 };
	auto data = m_Tree.GetItemData(item);
	if (data == (DWORD_PTR)-1)
		return { -1, -1 };
	int station = (int)(data >> 16), desktop = (int)(data & 0xFFFF) - 1;
	if (station >= (int)m_Stations.size() || desktop >= (int)m_Stations[station].Desktops.size())
		return { -1, -1 };
	return { station, desktop };
}

void CWindowStationsDlg::LoadWindows() {
	m_Windows.clear();
	m_WindowsError = 0;
	auto [s, d] = GetSelection();
	if (s >= 0 && d >= 0) {
		if (!m_Stations[s].Own)
			m_WindowsError = ERROR_NOT_SUPPORTED;
		else if (auto windows = WinSys::WindowStations::EnumDesktopWindows(m_Stations[s].Desktops[d].Name))
			m_Windows = std::move(*windows);
		else
			m_WindowsError = ::GetLastError();
	}
	ShowRows();
}

std::wstring CWindowStationsDlg::GetText(WinSys::WindowInfo const& w, Column column) const {
	switch (column) {
		case Column::Order: return std::to_wstring(&w - m_Windows.data() + 1);
		case Column::Handle: return std::format(L"0x{:X}", (ULONG_PTR)w.Handle);
		case Column::Title: return w.Text;
		case Column::Class: return w.ClassName;
		case Column::Process: return m_ProcessName ? m_ProcessName(w.ProcessId) : L"";
		case Column::Pid: return std::to_wstring(w.ProcessId);
		case Column::Tid: return std::to_wstring(w.ThreadId);
		case Column::State:
		{
			std::wstring text = w.Visible ? L"Visible" : L"Hidden";
			if (w.Minimized)
				text += L", minimized";
			else if (w.Maximized)
				text += L", maximized";
			if (w.Hung)
				text += L", not responding";
			return text;
		}
		case Column::Rect:
			return std::format(L"({}, {}) {} x {}", w.Rect.left, w.Rect.top, w.Rect.right - w.Rect.left, w.Rect.bottom - w.Rect.top);
	}
	return L"";
}

void CWindowStationsDlg::ShowRows() {
	std::vector<int> rows;
	for (int i = 0; i < (int)m_Windows.size(); i++) {
		auto& w = m_Windows[i];
		if (m_VisibleOnly && !w.Visible)
			continue;
		if (!m_Filter.empty()) {
			bool match = false;
			for (auto column : { Column::Title, Column::Class, Column::Process, Column::Handle, Column::Pid }) {
				auto text = GetText(w, column);
				::CharLowerBuff(text.data(), (DWORD)text.size());
				if (text.find(m_Filter) != std::wstring::npos) {
					match = true;
					break;
				}
			}
			if (!match)
				continue;
		}
		rows.push_back(i);
	}
	auto column = (Column)m_SortColumn;
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : b < a ? 1 : 0; };
	std::ranges::stable_sort(rows, [&](int a, int b) {
		auto& x = m_Windows[a];
		auto& y = m_Windows[b];
		int result;
		switch (column) {
			case Column::Order: result = compare(a, b); break;
			case Column::Handle: result = compare((ULONG_PTR)x.Handle, (ULONG_PTR)y.Handle); break;
			case Column::Pid: result = compare(x.ProcessId, y.ProcessId); break;
			case Column::Tid: result = compare(x.ThreadId, y.ThreadId); break;
			default: result = ::_wcsicmp(GetText(x, column).c_str(), GetText(y, column).c_str()); break;
		}
		if (result == 0)
			result = compare(a, b);
		return m_SortAscending ? result < 0 : result > 0;
		});

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (int i = 0; i < (int)rows.size(); i++) {
		auto& w = m_Windows[rows[i]];
		m_List.InsertItem(i, GetText(w, Column::Order).c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, GetText(w, (Column)c).c_str());
		m_List.SetItemData(i, rows[i]);
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate();
	UpdateStatus();
}

void CWindowStationsDlg::UpdateStatus() {
	std::wstring text;
	auto [s, d] = GetSelection();
	if (s < 0)
		text = std::format(L"{} window stations", m_Stations.size());
	else if (d < 0) {
		auto& st = m_Stations[s];
		text = !st.CurrentSession ? std::format(L"{}: in session {}; a process can open only its own session's window stations", st.Name, st.Session)
			: std::format(L"{}: {} desktops{}", st.Name, st.Desktops.size(), st.Opened ? L"" : L" (it can't be opened)");
	}
	else if (m_WindowsError == ERROR_NOT_SUPPORTED)
		text = L"Only the desktops of this process's window station can have their windows listed";
	else if (m_WindowsError)
		text = L"The desktop's windows can't be listed: " + WinSys::Helpers::GetErrorText(m_WindowsError);
	else {
		auto visible = std::ranges::count_if(m_Windows, [](auto& w) { return w.Visible; });
		text = std::format(L"{} top-level windows, {} visible", FormatHelper::FormatNumber((int64_t)m_Windows.size()), visible);
		if (!m_Filter.empty() || m_VisibleOnly)
			text += std::format(L"; {} shown", m_List.GetItemCount());
		text += L" (double-click: go to the process)";
	}
	SetDlgItemText(IDC_WS_STATUS, text.c_str());
}

WinSys::WindowInfo const* CWindowStationsDlg::GetSelectedWindow() const {
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(selected);
	return index < m_Windows.size() ? &m_Windows[index] : nullptr;
}

LRESULT CWindowStationsDlg::OnTreeSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	LoadWindows();
	return 0;
}

LRESULT CWindowStationsDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = column;
		m_SortAscending = true;
	}
	ShowRows();
	return 0;
}

LRESULT CWindowStationsDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnGoToProcess(0, 0, nullptr, handled);
}

LRESULT CWindowStationsDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List || !GetSelectedWindow()) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		m_List.GetItemRect(m_List.GetNextItem(-1, LVNI_SELECTED), &rc, LVIR_LABEL);
		pt = rc.CenterPoint();
		m_List.ClientToScreen(&pt);
	}
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_WINDOW_GOTOPROCESS, L"&Go to Process");
	menu.AppendMenu(MF_STRING, ID_WINDOW_BRINGTOFRONT, L"&Bring to Front");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_WS_COPY, L"&Copy");
	menu.SetMenuDefaultItem(ID_WINDOW_GOTOPROCESS);
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CWindowStationsDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto w = GetSelectedWindow();
	if (!w || !m_GoToProcess)
		return 0;
	auto pid = w->ProcessId;
	EndDialog(IDOK);
	m_GoToProcess(pid);
	return 0;
}

LRESULT CWindowStationsDlg::OnBringToFront(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto w = GetSelectedWindow(); w && !WinSys::ProcessWindows::BringToFront(w->Handle))
		SetDlgItemText(IDC_WS_STATUS, (L"Bring to Front failed: " + WinSys::Helpers::GetErrorText(::GetLastError())).c_str());
	return 0;
}

LRESULT CWindowStationsDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_WS_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CWindowStationsDlg::OnVisibleOnly(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_VisibleOnly = IsDlgButtonChecked(IDC_WS_VISIBLE) == BST_CHECKED;
	ShowRows();
	return 0;
}

LRESULT CWindowStationsDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CWindowStationsDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text = L"#\tHandle\tTitle\tClass\tProcess\tPID\tTID\tState\tRectangle\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& w = m_Windows[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(w, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CWindowStationsDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
