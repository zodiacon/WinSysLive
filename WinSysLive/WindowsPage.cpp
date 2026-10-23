#include "pch.h"
#include "WindowsPage.h"
#include <WinSys/Helpers.h>
#include <ListViewhelper.h>
#include <unordered_set>

namespace {
	const struct {
		PCWSTR Name;
		int Width;	// at 96 DPI
		int Format;
	} s_Columns[] = {
		{ L"Handle", 140, LVCFMT_LEFT },
		{ L"Class", 180, LVCFMT_LEFT },
		{ L"Text", 220, LVCFMT_LEFT },
		{ L"Visible", 55, LVCFMT_LEFT },
		{ L"Thread", 65, LVCFMT_RIGHT },
		{ L"Rectangle", 200, LVCFMT_LEFT },
		{ L"Style", 90, LVCFMT_RIGHT },
		{ L"Ex Style", 90, LVCFMT_RIGHT },
	};

	struct Flag {
		uint32_t Value;
		PCWSTR Name;
	};

	// the set flags' names, with what's left as a number
	std::wstring FlagsText(uint32_t value, std::initializer_list<Flag> flags) {
		std::wstring text;
		for (auto& f : flags)
			if (f.Value && (value & f.Value) == f.Value) {
				text += (text.empty() ? L"" : L" | ") + std::wstring(f.Name);
				value &= ~f.Value;
			}
		if (value)
			text += std::format(L"{}0x{:X}", text.empty() ? L"" : L" | ", value);
		return text;
	}

	std::wstring StyleText(uint32_t style) {
		// WS_CAPTION is two bits; 0x20000 and 0x10000 are the minimize and maximize boxes of a top-level window and
		// WS_GROUP and WS_TABSTOP of a child
		bool child = style & WS_CHILD;
		return FlagsText(style, { { WS_POPUP, L"POPUP" }, { WS_CHILD, L"CHILD" }, { WS_MINIMIZE, L"MINIMIZE" },
			{ WS_VISIBLE, L"VISIBLE" }, { WS_DISABLED, L"DISABLED" }, { WS_CLIPSIBLINGS, L"CLIPSIBLINGS" },
			{ WS_CLIPCHILDREN, L"CLIPCHILDREN" }, { WS_MAXIMIZE, L"MAXIMIZE" }, { WS_CAPTION, L"CAPTION" },
			{ WS_BORDER, L"BORDER" }, { WS_DLGFRAME, L"DLGFRAME" }, { WS_VSCROLL, L"VSCROLL" }, { WS_HSCROLL, L"HSCROLL" },
			{ WS_SYSMENU, L"SYSMENU" }, { WS_THICKFRAME, L"THICKFRAME" },
			{ WS_MINIMIZEBOX, child ? L"GROUP" : L"MINIMIZEBOX" }, { WS_MAXIMIZEBOX, child ? L"TABSTOP" : L"MAXIMIZEBOX" } });
	}

	std::wstring ExStyleText(uint32_t style) {
		return FlagsText(style, { { WS_EX_DLGMODALFRAME, L"DLGMODALFRAME" }, { WS_EX_NOPARENTNOTIFY, L"NOPARENTNOTIFY" },
			{ WS_EX_TOPMOST, L"TOPMOST" }, { WS_EX_ACCEPTFILES, L"ACCEPTFILES" }, { WS_EX_TRANSPARENT, L"TRANSPARENT" },
			{ WS_EX_MDICHILD, L"MDICHILD" }, { WS_EX_TOOLWINDOW, L"TOOLWINDOW" }, { WS_EX_WINDOWEDGE, L"WINDOWEDGE" },
			{ WS_EX_CLIENTEDGE, L"CLIENTEDGE" }, { WS_EX_CONTEXTHELP, L"CONTEXTHELP" }, { WS_EX_RIGHT, L"RIGHT" },
			{ WS_EX_RTLREADING, L"RTLREADING" }, { WS_EX_LEFTSCROLLBAR, L"LEFTSCROLLBAR" },
			{ WS_EX_CONTROLPARENT, L"CONTROLPARENT" }, { WS_EX_STATICEDGE, L"STATICEDGE" }, { WS_EX_APPWINDOW, L"APPWINDOW" },
			{ WS_EX_LAYERED, L"LAYERED" }, { WS_EX_NOINHERITLAYOUT, L"NOINHERITLAYOUT" },
			{ WS_EX_NOREDIRECTIONBITMAP, L"NOREDIRECTIONBITMAP" }, { WS_EX_LAYOUTRTL, L"LAYOUTRTL" },
			{ WS_EX_COMPOSITED, L"COMPOSITED" }, { WS_EX_NOACTIVATE, L"NOACTIVATE" } });
	}

	std::wstring ClassStyleText(uint32_t style) {
		return FlagsText(style, { { CS_VREDRAW, L"VREDRAW" }, { CS_HREDRAW, L"HREDRAW" }, { CS_DBLCLKS, L"DBLCLKS" },
			{ CS_OWNDC, L"OWNDC" }, { CS_CLASSDC, L"CLASSDC" }, { CS_PARENTDC, L"PARENTDC" }, { CS_NOCLOSE, L"NOCLOSE" },
			{ CS_SAVEBITS, L"SAVEBITS" }, { CS_BYTEALIGNCLIENT, L"BYTEALIGNCLIENT" }, { CS_BYTEALIGNWINDOW, L"BYTEALIGNWINDOW" },
			{ CS_GLOBALCLASS, L"GLOBALCLASS" }, { CS_DROPSHADOW, L"DROPSHADOW" } });
	}

	std::wstring HandleText(HWND hWnd) {
		return hWnd ? std::format(L"0x{:08X}", (uintptr_t)hWnd) : L"";
	}

	std::wstring RectText(RECT const& rc) {
		return std::format(L"({}, {}) - ({}, {})  {} x {}", rc.left, rc.top, rc.right, rc.bottom, rc.right - rc.left, rc.bottom - rc.top);
	}
}

CProcWindowsPage::CProcWindowsPage(ProcessProperties const& props) : m_Props(props) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_DESKTOP);
}

LRESULT CProcWindowsPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	// the splitter forwards the tree's notifications to the page
	m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT);
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP, 0);
	m_Tree.SetCallbackMode(true);
	m_Tree.SetExtendedStyle(m_Tree.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _countof(s_Columns); i++)
		m_Tree.InsertColumn(i, s_Columns[i].Name, ::MulDiv(s_Columns[i].Width, dpi, 96), s_Columns[i].Format);
	// the TreeListView doesn't scale its column widths to a new DPI
	ListViewHelper::EnableColumnDpiScaling(m_Tree.GetListCtrl());

	m_Details.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER |
		LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER);
	m_Details.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_Details.InsertColumn(0, L"Property", LVCFMT_LEFT, ::MulDiv(130, dpi, 96));
	m_Details.InsertColumn(1, L"Value", LVCFMT_LEFT, ::MulDiv(560, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_Details);

	m_Splitter.SetSplitterPanes(m_Tree, m_Details);
	m_Splitter.SetSplitterPosPct(60, false);
	ShowDetails();

	CRect rc;
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	return TRUE;
}

LRESULT CProcWindowsPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	Layout(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

void CProcWindowsPage::Layout(int cx, int cy) {
	if (!m_Splitter.IsWindow())
		return;
	// the other pages' margins; the actions in a row at the bottom left, Refresh at the right, the status line above
	// them, the panes above that. The buttons' sizes are read each time: the dialog manager scales them for a new DPI
	CRect units(0, 0, 7, 4);
	MapDialogRect(&units);
	int margin = units.right, gap = units.bottom;
	auto size = [&](UINT id) {
		CRect rc;
		GetDlgItem(id).GetWindowRect(&rc);
		return rc.Size();
	};
	auto refresh = size(IDC_WIN_REFRESH);
	int row = cy - margin - refresh.cy;
	GetDlgItem(IDC_WIN_REFRESH).SetWindowPos(nullptr, cx - margin - refresh.cx, row, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	int x = margin;
	for (UINT id : { IDC_WIN_SHOW, IDC_WIN_HIDE, IDC_WIN_FRONT, IDC_WIN_CLOSE }) {
		GetDlgItem(id).SetWindowPos(nullptr, x, row, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		x += size(id).cx + gap;
	}
	auto status = size(IDC_WIN_STATUS);
	int statusTop = row - gap - status.cy;
	GetDlgItem(IDC_WIN_STATUS).SetWindowPos(nullptr, margin, statusTop, (std::max)(0, cx - 2 * margin), status.cy,
		SWP_NOZORDER | SWP_NOACTIVATE);
	m_Splitter.SetWindowPos(nullptr, margin, margin, (std::max)(0, cx - 2 * margin), (std::max)(0, statusTop - gap - margin),
		SWP_NOZORDER | SWP_NOACTIVATE);
}

int CProcWindowsPage::OnSetActive() {
	// read when first shown, and on Refresh: the windows don't change by themselves often enough for a timer
	if (!m_Loaded) {
		m_Loaded = true;
		Load();
	}
	return 0;
}

void CProcWindowsPage::Load() {
	// the selection and the expanded windows, by handle
	HWND selected = nullptr;
	if (auto w = GetSelected())
		selected = w->Handle;
	std::unordered_set<HWND> expanded;
	auto collect = [&](auto&& self, HTLITEM parent) -> void {
		for (auto item = m_Tree.GetChildItem(parent); item; item = m_Tree.GetNextSiblingItem(item))
			if (m_Tree.GetChildItem(item) && m_Tree.IsExpanded(item)) {
				expanded.insert(m_Windows[m_Tree.GetItemData(item)].Handle);
				self(self, item);
			}
	};
	collect(collect, TLV_ROOT);

	m_Windows = WinSys::ProcessWindows::Enum(m_Props.Id);
	m_Tree.BeginUpdate();
	m_Tree.DeleteAllItems();
	// in tree order: a parent comes before its children
	std::vector<HTLITEM> items(m_Windows.size());
	HTLITEM selectedItem = nullptr;
	for (size_t i = 0; i < m_Windows.size(); i++) {
		auto& w = m_Windows[i];
		auto parent = w.Parent < 0 ? TLV_ROOT : items[w.Parent];
		items[i] = m_Tree.InsertItem(parent, TLV_LAST, LPSTR_TEXTCALLBACK, -1, (LPARAM)i);
		if (w.Handle == selected)
			selectedItem = items[i];
	}
	m_Tree.CollapseAll();
	for (size_t i = 0; i < m_Windows.size(); i++)
		if (expanded.contains(m_Windows[i].Handle))
			m_Tree.Expand(items[i]);
	m_Tree.EndUpdate();
	if (selectedItem) {
		for (auto p = m_Tree.GetParentItem(selectedItem); p && p != TLV_ROOT; p = m_Tree.GetParentItem(p))
			m_Tree.Expand(p);
		m_Tree.SelectItem(selectedItem);
	}

	auto topLevel = std::ranges::count_if(m_Windows, [](auto& w) { return w.Parent < 0 && !w.MessageOnly; });
	auto visible = std::ranges::count_if(m_Windows, [](auto& w) { return w.Parent < 0 && !w.MessageOnly && w.Visible; });
	auto messageOnly = std::ranges::count_if(m_Windows, [](auto& w) { return w.MessageOnly; });
	SetDlgItemText(IDC_WIN_STATUS, m_Windows.empty() ? L"The process has no windows on this desktop"
		: std::format(L"{} windows: {} top-level ({} visible), {} message-only, {} children", m_Windows.size(), topLevel, visible,
			messageOnly, m_Windows.size() - topLevel - messageOnly).c_str());
	ShowDetails();
}

WinSys::WindowInfo const* CProcWindowsPage::GetSelected() const {
	auto item = m_Tree.GetSelectedItem();
	if (!item)
		return nullptr;
	auto index = (size_t)m_Tree.GetItemData(item);
	return index < m_Windows.size() ? &m_Windows[index] : nullptr;
}

std::wstring CProcWindowsPage::GetText(WinSys::WindowInfo const& w, Column column) const {
	switch (column) {
		case Column::Handle: return HandleText(w.Handle);
		case Column::Class: return w.ClassName;
		case Column::Text: return w.Text;
		case Column::Visible: return w.Visible ? L"Yes" : L"";
		case Column::Thread: return std::to_wstring(w.ThreadId);
		case Column::Rectangle: return w.MessageOnly ? L"" : RectText(w.Rect);
		case Column::Style: return std::format(L"0x{:08X}", w.Style);
		case Column::ExStyle: return std::format(L"0x{:08X}", w.ExStyle);
	}
	return L"";
}

LRESULT CProcWindowsPage::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto index = (size_t)m_Tree.GetItemData(di->hItem);
	if (index < m_Windows.size() && di->iSubItem < (int)Column::_Count)
		wcsncpy_s(di->pszText, di->cchTextMax, GetText(m_Windows[index], (Column)di->iSubItem).c_str(), _TRUNCATE);
	return 0;
}

LRESULT CProcWindowsPage::OnSelChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}
	ShowDetails();
	return 0;
}

void CProcWindowsPage::ShowDetails() {
	m_Details.DeleteAllItems();
	auto w = GetSelected();
	// a message-only window has nothing to show or bring to the front
	for (UINT id : { IDC_WIN_SHOW, IDC_WIN_HIDE, IDC_WIN_FRONT })
		GetDlgItem(id).EnableWindow(w && !w->MessageOnly);
	GetDlgItem(IDC_WIN_CLOSE).EnableWindow(w != nullptr);
	if (!w)
		return;

	std::wstring state = w->Minimized ? L"Minimized" : w->Maximized ? L"Maximized" : L"Normal";
	if (!w->Visible)
		state += L", hidden";
	if (!w->Enabled)
		state += L", disabled";
	if (w->Hung)
		state += L", not responding";
	std::pair<PCWSTR, std::wstring> const rows[] = {
		{ L"Handle", HandleText(w->Handle) },
		{ L"Parent", w->Parent >= 0 ? HandleText(m_Windows[w->Parent].Handle) : w->MessageOnly ? L"Message-only (HWND_MESSAGE)" : L"Desktop" },
		{ L"Owner", HandleText(w->Owner) },
		{ L"Class", w->ClassName },
		{ L"Text", w->Text },
		{ L"Process ID", std::to_wstring(w->ProcessId) },
		{ L"Thread ID", std::to_wstring(w->ThreadId) },
		{ L"Control ID", (w->Style & WS_CHILD) ? std::format(L"{} (0x{:X})", w->Id, w->Id) : L"" },
		{ L"Style", std::format(L"0x{:08X} ({})", w->Style, StyleText(w->Style)) },
		{ L"Ex Style", std::format(L"0x{:08X} ({})", w->ExStyle, ExStyleText(w->ExStyle)) },
		{ L"Class Style", std::format(L"0x{:08X} ({})", w->ClassStyle, ClassStyleText(w->ClassStyle)) },
		{ L"Rectangle", RectText(w->Rect) },
		{ L"Client Size", std::format(L"{} x {}", w->ClientRect.right - w->ClientRect.left, w->ClientRect.bottom - w->ClientRect.top) },
		{ L"State", state },
		{ L"Unicode", w->Unicode ? L"Yes" : L"No" },
		{ L"DPI", w->Dpi ? std::to_wstring(w->Dpi) : L"" },
	};
	for (auto& [name, value] : rows) {
		int row = m_Details.InsertItem(m_Details.GetItemCount(), name);
		m_Details.SetItemText(row, 1, value.c_str());
	}
}

LRESULT CProcWindowsPage::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CProcWindowsPage::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != RefreshTimerId) {
		bHandled = FALSE;
		return 0;
	}
	KillTimer(RefreshTimerId);
	Load();
	return 0;
}

LRESULT CProcWindowsPage::OnAction(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto w = GetSelected();
	if (!w)
		return 0;
	auto hWnd = w->Handle;
	auto name = w->Text.empty() ? std::format(L"[{}]", w->ClassName) : std::format(L"\"{}\"", w->Text);
	bool ok = true;
	PCWSTR operation = L"";
	switch (wID) {
		case IDC_WIN_SHOW: operation = L"Showing the window"; ok = WinSys::ProcessWindows::Show(hWnd, true); break;
		case IDC_WIN_HIDE: operation = L"Hiding the window"; ok = WinSys::ProcessWindows::Show(hWnd, false); break;
		case IDC_WIN_FRONT: operation = L"Bringing the window to the front"; ok = WinSys::ProcessWindows::BringToFront(hWnd); break;
		case IDC_WIN_CLOSE:
		{
			auto text = std::format(L"Close window {} ({})?\n\nIt's sent WM_CLOSE, as its close box would: the window decides. "
				L"Closing a main window may end the process.", name, HandleText(hWnd));
			if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
				return 0;
			operation = L"Closing the window";
			ok = WinSys::ProcessWindows::Close(hWnd);
			break;
		}
	}
	if (!ok)
		AtlMessageBox(m_hWnd, std::format(L"{} failed: {}", operation, WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
	// the window changes asynchronously: read again shortly
	SetTimer(RefreshTimerId, RefreshDelay);
	return 0;
}
