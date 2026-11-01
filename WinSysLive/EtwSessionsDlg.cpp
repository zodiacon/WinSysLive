#include "pch.h"
#include "EtwSessionsDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	std::wstring GuidText(GUID const& guid) {
		WCHAR text[64];
		return ::StringFromGUID2(guid, text, _countof(text)) ? text : L"";
	}

	const std::wstring_view OwnPrefix = L"WinSysLive ";

	// a WinSysLive session's process: the number after the name's last space, before a '.' ("WinSysLive DotNet 1234.2")
	uint32_t OwnerPid(std::wstring const& name) {
		auto space = name.rfind(L' ');
		return space == std::wstring::npos ? 0 : (uint32_t)::wcstoul(name.c_str() + space + 1, nullptr, 10);
	}

	// a WinSysLive session whose process is gone, or has another image (its ID reused)
	bool IsOrphaned(std::wstring const& name) {
		if (!name.starts_with(OwnPrefix))
			return false;
		auto pid = OwnerPid(name);
		if (pid == 0)
			return false;
		wil::unique_handle hProcess(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
		if (!hProcess)
			return ::GetLastError() == ERROR_INVALID_PARAMETER;	// no such process (access denied: another user's, running)
		if (::WaitForSingleObject(hProcess.get(), 0) == WAIT_OBJECT_0)
			return true;
		WCHAR path[MAX_PATH];
		DWORD size = _countof(path);
		if (!::QueryFullProcessImageName(hProcess.get(), 0, path, &size))
			return false;
		auto file = ::wcsrchr(path, L'\\');
		return ::_wcsnicmp(file ? file + 1 : path, L"WinSysLive", 10) != 0;
	}
}

LRESULT CEtwSessionsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_ETW, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_ETW, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_SessionTable.Columns = { { L"Session", 280 }, { L"ID", 50, true }, { L"Mode", 200 }, { L"Providers", 70, true },
		{ L"Buffers", 90 }, { L"Events Lost", 80, true }, { L"Buffers Written", 100, true }, { L"Log File", 300 } };
	m_ProviderTable.Columns = { { L"Provider", 300 }, { L"GUID", 260 }, { L"Level", 90 }, { L"Any Keywords", 140 },
		{ L"All Keywords", 140 }, { L"Processes", 260 } };
	m_Sessions.Attach(GetDlgItem(IDC_ET_SESSIONS));
	m_Providers.Attach(GetDlgItem(IDC_ET_PROVIDERS));
	InitColumns(m_Sessions, m_SessionTable);
	InitColumns(m_Providers, m_ProviderTable);
	::SendDlgItemMessage(m_hWnd, IDC_ET_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by session, provider name or GUID");
	m_CopyList = m_Sessions;

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CEtwSessionsDlg::InitColumns(CListViewCtrl& list, Table const& table) {
	list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	auto dpi = ::GetDpiForWindow(list);
	for (int i = 0; i < (int)table.Columns.size(); i++) {
		auto& c = table.Columns[i];
		list.InsertColumn(i, c.Name, c.Numeric ? LVCFMT_RIGHT : LVCFMT_LEFT, ::MulDiv(c.Width, dpi, 96));
	}
	ListViewHelper::EnableColumnDpiScaling(list);
}

void CEtwSessionsDlg::Load() {
	CWaitCursor wait;
	auto selected = GetSelectedSession() ? GetSelectedSession()->Name : std::wstring();
	m_Error = 0;
	m_All.clear();
	if (auto sessions = WinSys::EtwSessions::Enum())
		m_All = std::move(*sessions);
	else
		m_Error = ::GetLastError();
	m_SessionTable.Rows.clear();
	m_Orphaned.clear();
	for (auto& s : m_All) {
		auto buffers = std::format(L"{} KB x {}", s.BufferSizeKB, s.Buffers);
		bool orphaned = IsOrphaned(s.Name);
		m_Orphaned.push_back(orphaned);
		m_SessionTable.Rows.push_back({ orphaned ? s.Name + L" (orphaned)" : s.Name, std::to_wstring(s.LoggerId), s.Mode, std::to_wstring(s.Providers.size()), buffers,
			std::to_wstring(s.EventsLost), std::to_wstring(s.BuffersWritten), s.LogFile });
	}
	ShowSessions();
	// the session selected before
	m_FillingSessions = true;
	for (int i = 0; i < m_Sessions.GetItemCount(); i++)
		if (m_All[m_Sessions.GetItemData(i)].Name == selected) {
			m_Sessions.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_Sessions.EnsureVisible(i, FALSE);
			break;
		}
	m_FillingSessions = false;
	ShowProviders();
}

bool CEtwSessionsDlg::ProviderMatches(WinSys::EtwSessionProvider const& p) const {
	return Lowercase(p.Name).find(m_Filter) != std::wstring::npos || Lowercase(GuidText(p.Id)).find(m_Filter) != std::wstring::npos;
}

void CEtwSessionsDlg::ShowSessions() {
	auto& table = m_SessionTable;
	table.Shown.clear();
	for (int i = 0; i < (int)m_All.size(); i++) {
		auto& s = m_All[i];
		if (!m_Filter.empty() && Lowercase(s.Name).find(m_Filter) == std::wstring::npos
			&& std::none_of(s.Providers.begin(), s.Providers.end(), [&](auto& p) { return ProviderMatches(p); }))
			continue;
		table.Shown.push_back(i);
	}
	m_FillingSessions = true;
	Fill(m_Sessions, table);
	if (m_Sessions.GetItemCount() > 0 && m_Sessions.GetNextItem(-1, LVNI_SELECTED) < 0)
		m_Sessions.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_FillingSessions = false;
	UpdateStatus();
}

std::wstring CEtwSessionsDlg::ProviderText(WinSys::EtwSessionProvider const& p) const {
	std::wstring text;
	int shown = 0;
	for (auto pid : p.Pids) {
		if (shown++ == 6) {
			text += std::format(L" and {} more", p.Pids.size() - 6);
			break;
		}
		auto name = pid == 0 ? std::wstring(L"Kernel") : pid == 4 ? std::wstring(L"System") : m_ProcessName ? m_ProcessName(pid) : L"";
		text += (text.empty() ? L"" : L", ") + (name.empty() ? std::to_wstring(pid) : std::format(L"{} ({})", name, pid));
	}
	return text;
}

void CEtwSessionsDlg::ShowProviders() {
	auto& table = m_ProviderTable;
	table.Rows.clear();
	auto session = GetSelectedSession();
	if (session) {
		// the session's name matched: all its providers; else those that match
		bool all = m_Filter.empty() || Lowercase(session->Name).find(m_Filter) != std::wstring::npos;
		for (auto& p : session->Providers) {
			if (!all && !ProviderMatches(p))
				continue;
			auto guid = GuidText(p.Id);
			table.Rows.push_back({ p.Name.empty() ? guid : p.Name, guid, WinSys::EtwSessions::LevelName(p.Level),
				std::format(L"0x{:X}", p.MatchAnyKeyword), std::format(L"0x{:X}", p.MatchAllKeyword), ProviderText(p) });
		}
	}
	table.Shown.clear();
	for (int i = 0; i < (int)table.Rows.size(); i++)
		table.Shown.push_back(i);
	Fill(m_Providers, table);
	UpdateStatus();
}

void CEtwSessionsDlg::Sort(Table& table) {
	auto column = table.SortColumn;
	bool numeric = column < (int)table.Columns.size() && table.Columns[column].Numeric;
	std::ranges::stable_sort(table.Shown, [&](int a, int b) {
		auto& x = table.Rows[a][column];
		auto& y = table.Rows[b][column];
		int result;
		if (numeric) {
			auto nx = std::wcstoll(x.c_str(), nullptr, 10), ny = std::wcstoll(y.c_str(), nullptr, 10);
			result = nx < ny ? -1 : ny < nx ? 1 : 0;
		}
		else
			result = ::_wcsicmp(x.c_str(), y.c_str());
		return table.SortAscending ? result < 0 : result > 0;
		});
}

void CEtwSessionsDlg::Fill(CListViewCtrl& list, Table& table) {
	Sort(table);
	// the selection by its first cell
	std::wstring selected;
	if (int sel = list.GetNextItem(-1, LVNI_SELECTED); sel >= 0) {
		WCHAR text[512];
		if (list.GetItemText(sel, 0, text, _countof(text)))
			selected = text;
	}
	list.SetRedraw(FALSE);
	list.DeleteAllItems();
	int select = -1;
	for (int i = 0; i < (int)table.Shown.size(); i++) {
		auto& cells = table.Rows[table.Shown[i]];
		list.InsertItem(i, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			list.SetItemText(i, c, cells[c].c_str());
		list.SetItemData(i, table.Shown[i]);
		if (select < 0 && !selected.empty() && cells[0] == selected)
			select = i;
	}
	if (select >= 0)
		list.SetItemState(select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	list.SetRedraw(TRUE);
	list.Invalidate();
}

WinSys::EtwSession const* CEtwSessionsDlg::GetSelectedSession() const {
	if (!m_Sessions)
		return nullptr;
	int sel = m_Sessions.GetNextItem(-1, LVNI_SELECTED);
	if (sel < 0)
		return nullptr;
	auto index = (size_t)m_Sessions.GetItemData(sel);
	return index < m_All.size() ? &m_All[index] : nullptr;
}

void CEtwSessionsDlg::UpdateStatus() {
	auto orphans = std::ranges::count(m_Orphaned, true);
	SetDlgItemText(IDC_ET_STOPORPHANED, orphans ? std::format(L"Stop &Orphaned ({})", orphans).c_str() : L"Stop &Orphaned");
	GetDlgItem(IDC_ET_STOPORPHANED).EnableWindow(orphans > 0);
	GetDlgItem(IDC_ET_STOP).EnableWindow(GetSelectedSession() != nullptr);
	std::wstring text;
	if (m_Error)
		text = L"The sessions can't be read: " + WinSys::Helpers::GetErrorText(m_Error);
	else {
		text = std::format(L"{} sessions", m_All.size());
		if (!m_Filter.empty())
			text += std::format(L" ({} match)", m_SessionTable.Shown.size());
		if (auto s = GetSelectedSession())
			text += std::format(L"; {}: {} providers{}", s->Name, s->Providers.size(),
				s->Providers.empty() && s->EnableFlags ? L" (a kernel logger: its events come by flags)" : L"");
	}
	if (!WinSys::SecurityHelper::IsRunningElevated())
		text += L" (most sessions need administrator rights)";
	// the last stop's outcome first, until Refresh
	if (!m_Result.empty())
		text = m_Result + L"; " + text;
	SetDlgItemText(IDC_ET_STATUS, text.c_str());
}

LRESULT CEtwSessionsDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_Sessions || !GetSelectedSession()) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		m_Sessions.GetItemRect(m_Sessions.GetNextItem(-1, LVNI_SELECTED), &rc, LVIR_LABEL);
		pt = rc.CenterPoint();
		m_Sessions.ClientToScreen(&pt);
	}
	m_CopyList = m_Sessions;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, IDC_ET_STOP, L"&Stop Session...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_ET_COPY, L"&Copy");
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CEtwSessionsDlg::OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto s = GetSelectedSession();
	if (!s)
		return 0;
	auto name = s->Name;
	bool orphaned = m_Orphaned[s - m_All.data()];
	std::wstring question;
	if (orphaned)
		question = std::format(L"Stop the session \"{}\"?\n\nIt was started by a WinSysLive that isn't running anymore.", name);
	else if (name.starts_with(OwnPrefix))
		question = std::format(L"Stop the session \"{}\"?\n\nA running WinSysLive (process {}) uses it: what it shows from the session "
			L"stops updating.", name, OwnerPid(name));
	else
		question = std::format(L"Stop the session \"{}\"?\n\nWindows or another program started it, and may rely on it: event logs, "
			L"diagnostics or that program's tracing stop until it starts the session again (some start it only at boot).", name);
	if (AtlMessageBox(m_hWnd, question.c_str(), L"ETW Sessions", MB_OKCANCEL | (orphaned ? MB_ICONQUESTION : MB_ICONWARNING) | MB_DEFBUTTON2) != IDOK)
		return 0;
	m_Result = WinSys::EtwSessions::Stop(name) ? std::format(L"Stopped {}", name)
		: std::format(L"{} couldn't be stopped: {}", name, WinSys::Helpers::GetErrorText(::GetLastError()));
	Load();
	return 0;
}

LRESULT CEtwSessionsDlg::OnStopOrphaned(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::vector<std::wstring> names;
	for (size_t i = 0; i < m_All.size(); i++)
		if (m_Orphaned[i])
			names.push_back(m_All[i].Name);
	if (names.empty())
		return 0;
	std::wstring list;
	for (auto& n : names)
		list += L"\n" + n;
	auto question = names.size() == 1
		? std::format(L"Stop the session left by a WinSysLive that isn't running anymore?\n{}", list)
		: std::format(L"Stop {} sessions left by WinSysLive instances that aren't running anymore?\n{}", names.size(), list);
	if (AtlMessageBox(m_hWnd, question.c_str(), L"ETW Sessions", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
		return 0;
	int stopped = 0;
	DWORD error = 0;
	for (auto& n : names) {
		if (WinSys::EtwSessions::Stop(n))
			stopped++;
		else
			error = ::GetLastError();
	}
	m_Result = std::format(L"Stopped {} of {} orphaned sessions", stopped, names.size());
	if (error)
		m_Result += L" (" + WinSys::Helpers::GetErrorText(error) + L")";
	Load();
	return 0;
}

LRESULT CEtwSessionsDlg::OnSessionChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (!m_FillingSessions && (lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
		ShowProviders();
	return 0;
}

LRESULT CEtwSessionsDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto& table = hdr->hwndFrom == m_Sessions ? m_SessionTable : m_ProviderTable;
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == table.SortColumn)
		table.SortAscending = !table.SortAscending;
	else {
		table.SortColumn = column;
		// names A to Z, numbers largest first
		table.SortAscending = !table.Columns[column].Numeric;
	}
	bool sessions = hdr->hwndFrom == m_Sessions;
	m_FillingSessions = sessions;
	Fill(sessions ? m_Sessions : m_Providers, table);
	m_FillingSessions = false;
	return 0;
}

LRESULT CEtwSessionsDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_Sessions || hdr->hwndFrom == m_Providers)
		m_CopyList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CEtwSessionsDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_ET_FILTER, text);
	m_Filter = Lowercase((PCWSTR)text);
	ShowSessions();
	ShowProviders();
	return 0;
}

LRESULT CEtwSessionsDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Result.clear();
	Load();
	return 0;
}

LRESULT CEtwSessionsDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CListViewCtrl list(m_CopyList);
	auto& table = m_CopyList == m_Sessions ? m_SessionTable : m_ProviderTable;
	std::wstring text;
	for (int c = 0; c < (int)table.Columns.size(); c++)
		text += (c ? L"\t" : L"") + std::wstring(table.Columns[c].Name);
	text += L"\r\n";
	bool selected = list.GetSelectedCount() > 0;
	for (int i = 0; i < list.GetItemCount(); i++) {
		if (selected && !(list.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& cells = table.Rows[list.GetItemData(i)];
		for (int c = 0; c < (int)cells.size(); c++)
			text += (c ? L"\t" : L"") + cells[c];
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CEtwSessionsDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
