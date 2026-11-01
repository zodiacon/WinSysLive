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
	for (auto& s : m_All) {
		auto buffers = std::format(L"{} KB x {}", s.BufferSizeKB, s.Buffers);
		m_SessionTable.Rows.push_back({ s.Name, std::to_wstring(s.LoggerId), s.Mode, std::to_wstring(s.Providers.size()), buffers,
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
	SetDlgItemText(IDC_ET_STATUS, text.c_str());
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
