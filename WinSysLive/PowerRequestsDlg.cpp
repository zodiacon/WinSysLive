#include "pch.h"
#include "PowerRequestsDlg.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

namespace {
	const UINT_PTR RefreshTimerId = 1;

	std::wstring FileName(std::wstring const& path) {
		auto slash = path.rfind(L'\\');
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}

	PCWSTR KindName(PowerRequester kind) {
		switch (kind) {
			case PowerRequester::Driver: return L"Driver";
			case PowerRequester::Process: return L"Process";
			case PowerRequester::Service: return L"Service";
		}
		return L"";
	}

	// "Display, System (2)"
	std::wstring TypesText(PowerRequest const& r) {
		std::wstring text;
		for (int t = 0; t < (int)PowerRequestType::_Count; t++) {
			if (!r.Counts[t])
				continue;
			if (!text.empty())
				text += L", ";
			text += PowerRequests::TypeName((PowerRequestType)t);
			if (r.Counts[t] > 1)
				text += std::format(L" ({})", r.Counts[t]);
		}
		return text.empty() ? L"(not active)" : text;
	}

	// who: a process's image file name, a service's name (and host), a driver's device description
	std::wstring Requester(PowerRequestSource const& s) {
		if (s.Kind == PowerRequester::Service)
			return s.ServiceName.empty() ? FileName(s.Name) : s.ServiceName + L" (" + FileName(s.Name) + L")";
		if (s.Kind == PowerRequester::Process)
			return FileName(s.Name);
		return s.Name.empty() ? L"(kernel)" : s.Name;
	}

	std::wstring Milliseconds(uint32_t units) {
		return std::format(L"{:.3f} ms", units / 10000.0);
	}

	std::wstring TimeText(int64_t time) {
		FILETIME ft{ (DWORD)time, (DWORD)(time >> 32) }, local;
		SYSTEMTIME st;
		if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
			return L"";
		WCHAR date[64]{}, clock[64]{};
		::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, _countof(date), nullptr);
		::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, clock, _countof(clock));
		return std::wstring(date) + L" " + clock;
	}

	std::wstring PeriodText(uint32_t ms) {
		if (ms == 0)
			return L"Once";
		if (ms % 3600000 == 0)
			return std::format(L"Every {} h", ms / 3600000);
		if (ms % 60000 == 0)
			return std::format(L"Every {} min", ms / 60000);
		return std::format(L"Every {} s", ms / 1000.0);
	}
}

LRESULT CPowerRequestsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_CAPABILITY, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_CAPABILITY, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_RequestTable.Columns = { L"Requests", L"Requester", L"Kind", L"PID", L"Reason", L"Path" };
	m_RequestTable.Widths = { 150, 220, 60, 60, 330, 360 };
	m_TimerTable.Columns = { L"Expires", L"Period", L"Requester", L"Kind", L"PID", L"Reason", L"Path" };
	m_TimerTable.Widths = { 150, 90, 220, 60, 60, 360, 300 };
	m_Requests.Attach(GetDlgItem(IDC_PR_REQUESTS));
	m_Timers.Attach(GetDlgItem(IDC_PR_TIMERS));
	InitColumns(m_Requests, m_RequestTable);
	InitColumns(m_Timers, m_TimerTable);
	m_FocusList = m_Requests;

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	SetTimer(RefreshTimerId, 2000);
	return TRUE;
}

void CPowerRequestsDlg::InitColumns(CListViewCtrl& list, Table const& table) {
	list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	auto dpi = ::GetDpiForWindow(list.GetParent());
	for (int i = 0; i < (int)table.Columns.size(); i++)
		list.InsertColumn(i, table.Columns[i], table.Columns[i] == std::wstring(L"PID") ? LVCFMT_RIGHT : LVCFMT_LEFT,
			::MulDiv(table.Widths[i], dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(list);
}

void CPowerRequestsDlg::Load() {
	m_Error = 0;
	std::wstring resolution;
	if (auto r = PowerRequests::GetTimerResolution())
		resolution = std::format(L"Timer resolution: {} (finest {}, coarsest {}){}", Milliseconds(r->Current), Milliseconds(r->Finest),
			Milliseconds(r->Coarsest), r->Current < r->Coarsest ? L": a program asked for a finer one, which costs power" : L"");
	SetDlgItemText(IDC_PR_RESOLUTION, resolution.c_str());

	m_RequestTable.Rows.clear();
	m_RequestTable.Pids.clear();
	if (auto requests = PowerRequests::Enum(!m_ShowInactive)) {
		for (auto& r : *requests) {
			m_RequestTable.Rows.push_back({ TypesText(r), Requester(r), KindName(r.Kind),
				r.ProcessId ? std::to_wstring(r.ProcessId) : L"", r.Reason, r.Kind == PowerRequester::Driver ? r.DevicePath : r.Name });
			m_RequestTable.Pids.push_back(r.ProcessId);
		}
	}
	else
		m_Error = ::GetLastError();

	m_TimerTable.Rows.clear();
	m_TimerTable.Pids.clear();
	m_TimerTable.SortKeys.clear();
	if (auto timers = PowerRequests::EnumWakeTimers()) {
		for (auto& t : *timers) {
			m_TimerTable.Rows.push_back({ TimeText(t.DueTime), PeriodText(t.PeriodMs), Requester(t), KindName(t.Kind),
				t.ProcessId ? std::to_wstring(t.ProcessId) : L"", t.Reason, t.Kind == PowerRequester::Driver ? t.DevicePath : t.Name });
			m_TimerTable.Pids.push_back(t.ProcessId);
			m_TimerTable.SortKeys.push_back(t.DueTime);
		}
	}
	else if (!m_Error)
		m_Error = ::GetLastError();

	Fill(m_Requests, m_RequestTable);
	Fill(m_Timers, m_TimerTable);

	std::wstring status;
	if (m_Error)
		status = L"The power requests can't be read: " + Helpers::GetErrorText(m_Error)
			+ (SecurityHelper::IsRunningElevated() ? L"" : L" (they need administrator rights)");
	else
		status = std::format(L"{} {}requests, {} wake timers; read every 2 seconds", m_RequestTable.Rows.size(),
			m_ShowInactive ? L"" : L"active ", m_TimerTable.Rows.size());
	SetDlgItemText(IDC_PR_STATUS, status.c_str());
}

void CPowerRequestsDlg::Fill(CListViewCtrl& list, Table& table) {
	// the selection, by its first two cells
	std::vector<std::pair<std::wstring, std::wstring>> selected;
	for (int i = list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = list.GetNextItem(i, LVNI_SELECTED)) {
		CString a, b;
		list.GetItemText(i, 0, a);
		list.GetItemText(i, 1, b);
		selected.emplace_back((PCWSTR)a, (PCWSTR)b);
	}
	std::vector<int> order((int)table.Rows.size());
	for (int i = 0; i < (int)order.size(); i++)
		order[i] = i;
	if (table.SortColumn >= 0) {
		auto column = table.SortColumn;
		bool numeric = table.Columns[column] == std::wstring(L"PID");
		bool due = column == 0 && !table.SortKeys.empty();
		std::ranges::stable_sort(order, [&](int a, int b) {
			int result;
			if (due)
				result = table.SortKeys[a] < table.SortKeys[b] ? -1 : table.SortKeys[a] > table.SortKeys[b] ? 1 : 0;
			else if (numeric)
				result = (int)table.Pids[a] - (int)table.Pids[b];
			else
				result = ::_wcsicmp(table.Rows[a][column].c_str(), table.Rows[b][column].c_str());
			return table.SortAscending ? result < 0 : result > 0;
			});
	}
	list.SetRedraw(FALSE);
	list.DeleteAllItems();
	for (int i = 0; i < (int)order.size(); i++) {
		auto& cells = table.Rows[order[i]];
		list.InsertItem(i, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			list.SetItemText(i, c, cells[c].c_str());
		list.SetItemData(i, order[i]);
		if (std::ranges::find(selected, std::make_pair(cells[0], cells[1])) != selected.end())
			list.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
	}
	list.SetRedraw(TRUE);
	list.Invalidate();
}

LRESULT CPowerRequestsDlg::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != RefreshTimerId) {
		bHandled = FALSE;
		return 0;
	}
	Load();
	return 0;
}

CPowerRequestsDlg::Table& CPowerRequestsDlg::TableOf(HWND list) {
	return list == m_Timers.m_hWnd ? m_TimerTable : m_RequestTable;
}

CListViewCtrl* CPowerRequestsDlg::FocusList() {
	return m_FocusList == m_Timers.m_hWnd ? &m_Timers : &m_Requests;
}

uint32_t CPowerRequestsDlg::SelectedPid() {
	auto list = FocusList();
	int selected = list->GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return 0;
	auto& table = TableOf(*list);
	auto index = (size_t)list->GetItemData(selected);
	return index < table.Pids.size() ? table.Pids[index] : 0;
}

LRESULT CPowerRequestsDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Requests && hdr->hwndFrom != m_Timers) {
		bHandled = FALSE;
		return 0;
	}
	auto& table = TableOf(hdr->hwndFrom);
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == table.SortColumn)
		table.SortAscending = !table.SortAscending;
	else {
		table.SortColumn = column;
		table.SortAscending = true;
	}
	CListViewCtrl list(hdr->hwndFrom);
	Fill(list, table);
	return 0;
}

LRESULT CPowerRequestsDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_Requests || hdr->hwndFrom == m_Timers)
		m_FocusList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CPowerRequestsDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Requests && hdr->hwndFrom != m_Timers) {
		bHandled = FALSE;
		return 0;
	}
	m_FocusList = hdr->hwndFrom;
	BOOL handled;
	return OnGoToProcess(0, 0, nullptr, handled);
}

LRESULT CPowerRequestsDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto hList = (HWND)wParam;
	if (hList != m_Requests && hList != m_Timers) {
		bHandled = FALSE;
		return 0;
	}
	m_FocusList = hList;
	CListViewCtrl list(hList);
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		int selected = list.GetNextItem(-1, LVNI_SELECTED);
		if (selected >= 0)
			list.GetItemRect(selected, &rc, LVIR_LABEL);
		else
			list.GetClientRect(&rc);
		pt = selected >= 0 ? rc.CenterPoint() : rc.TopLeft();
		list.ClientToScreen(&pt);
	}
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (SelectedPid() ? 0 : MF_GRAYED), ID_POWER_GOTOPROCESS, L"&Go to Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (list.GetItemCount() ? 0 : MF_GRAYED), IDC_PR_COPY, list.GetSelectedCount() ? L"&Copy" : L"&Copy All");
	menu.SetMenuDefaultItem(ID_POWER_GOTOPROCESS);
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CPowerRequestsDlg::OnShowInactive(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_ShowInactive = IsDlgButtonChecked(IDC_PR_INACTIVE) == BST_CHECKED;
	Load();
	return 0;
}

LRESULT CPowerRequestsDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto pid = SelectedPid();
	if (!pid || !m_GoToProcess)
		return 0;
	EndDialog(IDOK);
	m_GoToProcess(pid);
	return 0;
}

LRESULT CPowerRequestsDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto list = FocusList();
	auto& table = TableOf(*list);
	std::wstring text;
	for (int c = 0; c < (int)table.Columns.size(); c++)
		text += (c ? L"\t" : L"") + std::wstring(table.Columns[c]);
	text += L"\r\n";
	bool selected = list->GetSelectedCount() > 0;
	for (int i = 0; i < list->GetItemCount(); i++) {
		if (selected && !(list->GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& cells = table.Rows[list->GetItemData(i)];
		for (int c = 0; c < (int)cells.size(); c++)
			text += (c ? L"\t" : L"") + cells[c];
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CPowerRequestsDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	KillTimer(RefreshTimerId);
	EndDialog(wID);
	return 0;
}
