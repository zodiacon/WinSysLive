#include "pch.h"
#include "CommandPaletteDlg.h"
#include <DpiHelper.h>

namespace {
	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}
}

LRESULT CCommandPaletteDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Filter.SubclassWindow(GetDlgItem(IDC_CP_FILTER));
	m_Filter.SetCueBannerText(m_Loader || !m_Search.empty() ? L"Type a command, a tab's name, or a process, service, driver or device"
		: L"Type a command or a tab's name");
	if (m_Loader) {
		m_Loading = true;
		m_Worker = std::jthread([this, hWnd = m_hWnd] {
			auto items = m_Loader();
			{
				std::lock_guard lock(m_LoadLock);
				m_Loaded = std::move(items);
			}
			::PostMessage(hWnd, WM_SEARCHLOADED, 0, 0);
			});
	}
	m_List.Attach(GetDlgItem(IDC_CP_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	if (m_Images)
		m_List.SetImageList(m_Images, LVSIL_SMALL);
	CRect rc;
	m_List.GetClientRect(&rc);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	int shortcut = ::MulDiv(110, dpi, 96), path = ::MulDiv(140, dpi, 96);
	m_List.InsertColumn(0, L"Command", LVCFMT_LEFT, (std::max)(80, rc.Width() - shortcut - path - ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi)));
	m_List.InsertColumn(1, L"Menu", LVCFMT_LEFT, path);
	m_List.InsertColumn(2, L"Shortcut", LVCFMT_LEFT, shortcut);

	// centered on the frame, near its top
	if (auto owner = GetParent(); owner) {
		CRect frame, self;
		owner.GetWindowRect(&frame);
		GetWindowRect(&self);
		SetWindowPos(nullptr, frame.left + (frame.Width() - self.Width()) / 2, frame.top + ::MulDiv(80, dpi, 96), 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	ShowCommands();
	m_Filter.SetFocus();
	PostMessage(WM_SHOWN);
	return FALSE;	// the focus is set
}

LRESULT CCommandPaletteDlg::OnSearchLoaded(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	{
		std::lock_guard lock(m_LoadLock);
		if (m_Loaded) {
			for (auto& item : *m_Loaded)
				m_Search.push_back(std::move(item));
			m_Loaded.reset();
		}
	}
	m_Loading = false;
	ShowCommands();
	return 0;
}

LRESULT CCommandPaletteDlg::OnShown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the list filled before the dialog was shown painted only in part (as the theming reached it after its first
	// paint): all of it again, now
	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
	return 0;
}

void CCommandPaletteDlg::ShowCommands() {
	CString text;
	m_Filter.GetWindowText(text);
	auto filter = Lowercase((PCWSTR)text);
	// the words typed, each to be found
	std::vector<std::wstring> words;
	for (size_t start = 0; start < filter.size(); ) {
		auto end = filter.find(L' ', start);
		if (end == std::wstring::npos)
			end = filter.size();
		if (end > start)
			words.push_back(filter.substr(start, end - start));
		start = end + 1;
	}
	std::vector<int> first, rest;	// names starting with the text, then the others, each in menu order
	// with nothing typed, the recent commands first, newest first
	if (filter.empty())
		for (auto id : m_Recent)
			for (int i = 0; i < (int)m_Commands.size(); i++)
				if (m_Commands[i].Id == id && std::ranges::find(first, i) == first.end()) {
					first.push_back(i);
					break;
				}
	for (int i = 0; i < (int)m_Commands.size(); i++) {
		auto& c = m_Commands[i];
		auto name = Lowercase(c.Name);
		auto haystack = name + L" " + Lowercase(c.Path);
		if (!std::ranges::all_of(words, [&](auto& w) { return haystack.find(w) != std::wstring::npos; }))
			continue;
		if (filter.empty() ? std::ranges::find(first, i) == first.end() : true)
			(!filter.empty() && name.starts_with(filter) ? first : rest).push_back(i);
	}
	// a loop, not a range insert: that warns C4530 in Release, which has no exceptions
	for (auto i : rest)
		first.push_back(i);

	// the search's results, from two characters: names starting with the text first, each kind in the order given
	std::vector<int> found, foundRest;
	if (filter.size() >= 2) {
		for (int i = 0; i < (int)m_Search.size() && found.size() + foundRest.size() < MaxResults * 4; i++) {
			auto& item = m_Search[i];
			auto name = Lowercase(item.Name);
			auto haystack = name + L" " + Lowercase(item.Kind) + L" " + Lowercase(item.Detail);
			if (std::ranges::all_of(words, [&](auto& w) { return haystack.find(w) != std::wstring::npos; }))
				(name.starts_with(filter) ? found : foundRest).push_back(i);
		}
		for (auto i : foundRest)
			found.push_back(i);
		// at most MaxPerKind of a kind (thirty svchost.exe processes mustn't hide the services), MaxResults in all
		std::unordered_map<std::wstring, size_t> perKind;
		std::erase_if(found, [&](int i) { return ++perKind[m_Search[i].Kind] > MaxPerKind; });
		if (found.size() > MaxResults)
			found.resize(MaxResults);
	}

	// no SetRedraw: the first fill happens before the dialog is shown, and a list whose redraw was turned off and on
	// then stayed unpainted until its contents changed (a hundred or so rows fill quickly anyway)
	m_List.DeleteAllItems();
	for (int row = 0; row < (int)first.size(); row++) {
		auto& c = m_Commands[first[row]];
		// U+2713, a check mark (escaped: the source is compiled as ANSI)
		auto name = c.Checked ? c.Name + L" \x2713" : c.Name;
		m_List.InsertItem(LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM, row, name.c_str(), 0, 0, c.Image, first[row]);
		m_List.SetItemText(row, 1, c.Path.c_str());
		m_List.SetItemText(row, 2, c.Shortcut.c_str());
	}
	// a search result's item data: -1 - its index (a command's is its index)
	for (auto i : found) {
		auto& item = m_Search[i];
		int row = m_List.GetItemCount();
		m_List.InsertItem(LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM, row, item.Name.c_str(), 0, 0, item.Image, -1 - i);
		m_List.SetItemText(row, 1, item.Kind.c_str());
		m_List.SetItemText(row, 2, item.Detail.c_str());
	}
	if (filter.size() >= 2 && m_Loading)
		m_List.InsertItem(LVIF_TEXT | LVIF_PARAM, m_List.GetItemCount(), L"Searching services, drivers and devices...", 0, 0, -1, INT_MIN);
	if (m_List.GetItemCount() > 0 && (LPARAM)m_List.GetItemData(0) != INT_MIN) {
		m_List.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.EnsureVisible(0, FALSE);
	}
}

LRESULT CCommandPaletteDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ShowCommands();
	return 0;
}

LRESULT CCommandPaletteDlg::OnFilterKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// moving in the list while the focus stays in the box
	switch (wParam) {
		case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
			m_List.SendMessage(uMsg, wParam, lParam);
			return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteDlg::OnActivate(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	// clicking elsewhere closes it, as a menu
	if (LOWORD(wParam) == WA_INACTIVE && !m_Closing) {
		m_Closing = true;
		PostMessage(WM_COMMAND, IDCANCEL);
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	PostMessage(WM_COMMAND, IDOK);
	return 0;
}

LRESULT CCommandPaletteDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return 0;
	auto data = (LPARAM)m_List.GetItemData(selected);
	if (data == INT_MIN)	// "Searching..."
		return 0;
	if (data < 0) {
		m_SearchAction = m_Search[-1 - data].Go;
		m_Chosen = 0;
	}
	else
		m_Chosen = m_Commands[data].Id;
	m_Closing = true;
	EndDialog(IDOK);
	return 0;
}

LRESULT CCommandPaletteDlg::OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Closing = true;
	EndDialog(IDCANCEL);
	return 0;
}
