#include "pch.h"
#include "ObjectManagerView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include "HandleIcons.h"
#include "HandleSheet.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	// in CObjectManagerView::Column order; the keys are stored in the ObjectManagerColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Name", L"Name", 280, LVCFMT_LEFT, true, true },
		{ L"Type", L"Type", 130, LVCFMT_LEFT, true },
		{ L"Symbolic Link Target", L"Target", 380, LVCFMT_LEFT, true },
		{ L"Handles", L"Handles", 70, LVCFMT_RIGHT, true },
		{ L"Created", L"Created", 150, LVCFMT_LEFT, true },
		{ L"Full Path", L"FullPath", 400, LVCFMT_LEFT, false },
	};

	int CompareNoCase(std::wstring const& a, std::wstring const& b) {
		return ::_wcsicmp(a.c_str(), b.c_str());
	}
}

CString CObjectManagerView::GetTitle() const {
	return L"Object Manager";
}

UINT CObjectManagerView::ObjectIcon(std::wstring const& type) {
	if (type == L"Device")
		return IDI_DEVICE;
	if (type == L"Driver")
		return IDI_DRIVER;
	// the Handles page's (a non-file name: no file icon)
	return GetHandleIcon(type, L"\\");
}

LRESULT CObjectManagerView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	// the filter box goes in a rebar of the view's own, above the panes
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	// the splitter forwards the tree's and the list's notifications to the view
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP |
		TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, TreeId);
	m_Tree.SetExtendedStyle(TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
	// tree views don't destroy their image lists: the frame's is safe here
	m_Tree.SetImageList(Frame()->GetImageList(), TVSIL_NORMAL);

	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(settings.ObjectManagerColumns(), s_Columns));

	m_Splitter.SetSplitterPanes(m_Tree, m_List);
	m_Splitter.SetSplitterPosPct(std::clamp(settings.ObjectManagerSplitterPos(), 10, 80), false);

	int filterWidth = 280;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Name, type or target (Ctrl+F)",
		m_List, [this](auto&) {
			auto row = GetSelected();
			ShowRows(row ? row->Name : L"");
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

void CObjectManagerView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (!active) {
		Frame()->SetStatusText(0, L"");
		return;
	}
	if (!m_Loaded) {
		// the root, and the directory last shown (or as much of its path as is still there)
		m_Loaded = true;
		auto root = m_Tree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN, L"\\",
			Frame()->GetIconIndex(IDI_DIRECTORY), Frame()->GetIconIndex(IDI_DIRECTORY), 0, 0, 0, TVI_ROOT, TVI_LAST);
		TVITEM item{ TVIF_CHILDREN, root };
		item.cChildren = 1;
		m_Tree.SetItem(&item);
		FillNode(root);
		m_Tree.Expand(root, TVE_EXPAND);
		if (!SelectDirectory(AppSettings::Get().ObjectManagerPath()))
			m_Tree.SelectItem(root);
	}
	UpdateStatus();
	m_List.SetFocus();
}

void CObjectManagerView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Tree.SetFont(font);
	m_Filter.SetFont(font);
}

void CObjectManagerView::OnDpiChanged(UINT /*dpi*/) {
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Tree.SetImageList(Frame()->GetImageList(), TVSIL_NORMAL);
}

void CObjectManagerView::OnFrameIdle() {
	auto row = GetSelected();
	Frame()->GetUI().UIEnable(ID_VIEW_PROPERTIES, row && !row->Removed && WinSys::ObjectNamespace::CanOpen(row->Type));
}

void CObjectManagerView::OnRefresh(bool active) {
	// a directory's listing is cheap, but only the shown one is read
	if (active && m_Loaded && !m_Path.empty())
		Load(false);
}

std::wstring CObjectManagerView::GetItemPath(HTREEITEM item) const {
	std::wstring path;
	for (; item; item = m_Tree.GetParentItem(item)) {
		CString text;
		m_Tree.GetItemText(item, text);
		if (!m_Tree.GetParentItem(item))
			break;	// the root
		path = L"\\" + std::wstring(text) + path;
	}
	return path.empty() ? L"\\" : path;
}

void CObjectManagerView::FillNode(HTREEITEM item) {
	if (m_Tree.GetItemData(item))
		return;
	m_Tree.SetItemData(item, 1);
	auto entries = WinSys::ObjectNamespace::EnumDirectory(GetItemPath(item));
	SyncNode(item, entries ? *entries : std::vector<WinSys::NamespaceEntry>());
}

void CObjectManagerView::SyncNode(HTREEITEM item, std::vector<WinSys::NamespaceEntry> const& entries) {
	// the subdirectories, sorted by name; nodes that went are deleted, new ones inserted in order
	std::vector<std::wstring> names;
	for (auto& e : entries)
		if (e.TypeName == L"Directory")
			names.push_back(e.Name);
	std::ranges::sort(names, [](auto& a, auto& b) { return CompareNoCase(a, b) < 0; });

	for (auto child = m_Tree.GetChildItem(item); child; ) {
		auto next = m_Tree.GetNextSiblingItem(child);
		CString text;
		m_Tree.GetItemText(child, text);
		if (!std::ranges::any_of(names, [&](auto& n) { return CompareNoCase(n, (PCWSTR)text) == 0; }))
			m_Tree.DeleteItem(child);
		child = next;
	}
	int image = Frame()->GetIconIndex(IDI_DIRECTORY);
	HTREEITEM after = TVI_FIRST;
	for (auto& name : names) {
		if (auto existing = FindChild(item, name)) {
			after = existing;
			continue;
		}
		TVINSERTSTRUCT insert{ item, after };
		insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN | TVIF_PARAM;
		insert.item.pszText = const_cast<PWSTR>(name.c_str());
		insert.item.iImage = insert.item.iSelectedImage = image;
		// it may have subdirectories: told when it's expanded
		insert.item.cChildren = 1;
		insert.item.lParam = 0;
		after = m_Tree.InsertItem(&insert);
	}
	TVITEM state{ TVIF_CHILDREN, item };
	state.cChildren = names.empty() ? 0 : 1;
	m_Tree.SetItem(&state);
}

HTREEITEM CObjectManagerView::FindChild(HTREEITEM parent, std::wstring const& name) const {
	for (auto child = m_Tree.GetChildItem(parent); child; child = m_Tree.GetNextSiblingItem(child)) {
		CString text;
		m_Tree.GetItemText(child, text);
		if (CompareNoCase(name, (PCWSTR)text) == 0)
			return child;
	}
	return nullptr;
}

HTREEITEM CObjectManagerView::SelectDirectory(std::wstring const& path) {
	auto item = m_Tree.GetRootItem();
	if (!item)
		return nullptr;
	size_t start = 1;
	while (start < path.size()) {
		auto end = path.find(L'\\', start);
		auto name = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
		start = end == std::wstring::npos ? path.size() : end + 1;
		if (name.empty())
			continue;
		FillNode(item);
		auto child = FindChild(item, name);
		if (!child)
			return nullptr;
		m_Tree.Expand(item, TVE_EXPAND);
		item = child;
	}
	m_Tree.SelectItem(item);
	m_Tree.EnsureVisible(item);
	return item;
}

LRESULT CObjectManagerView::OnTreeExpanding(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto tv = reinterpret_cast<NMTREEVIEW*>(hdr);
	if (tv->action == TVE_EXPAND)
		FillNode(tv->itemNew.hItem);
	return FALSE;
}

LRESULT CObjectManagerView::OnTreeSelChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto tv = reinterpret_cast<NMTREEVIEW*>(hdr);
	if (!tv->itemNew.hItem)
		return 0;
	m_Path = GetItemPath(tv->itemNew.hItem);
	Load(true);
	return 0;
}

void CObjectManagerView::Load(bool newDirectory) {
	auto selectedRow = GetSelected();
	auto selected = !newDirectory && selectedRow ? selectedRow->Name : std::wstring();
	auto entries = WinSys::ObjectNamespace::EnumDirectory(m_Path);
	m_Error = entries ? ERROR_SUCCESS : ::GetLastError();
	// the rows point into m_All, which is replaced
	m_Rows.clear();
	std::vector<Row> rows;
	if (entries) {
		rows.reserve(entries->size());
		for (auto& e : *entries) {
			Row row{ e.Name, e.TypeName };
			auto previous = newDirectory ? m_All.end() : std::ranges::find_if(m_All, [&](auto& r) { return !r.Removed && r.Name == e.Name; });
			if (previous != m_All.end() && previous->Type == e.TypeName) {
				// kept: its link target, and what's left of its highlight
				row.Target = previous->Target;
				row.Created = previous->Created;
				row.New = previous->New ? previous->New - 1 : 0;
			}
			else {
				if (e.TypeName == L"SymbolicLink")
					if (auto link = WinSys::ObjectNamespace::GetSymbolicLink(WinSys::ObjectNamespace::Join(m_Path, e.Name))) {
						row.Target = link->Target;
						row.Created = link->CreateTime;
					}
				row.New = newDirectory || m_All.empty() ? 0 : 2;
			}
			rows.push_back(std::move(row));
		}
		// the ones that went, once more
		if (!newDirectory)
			for (auto& r : m_All)
				if (!r.Removed && !std::ranges::any_of(*entries, [&](auto& e) { return e.Name == r.Name; })) {
					auto gone = r;
					gone.Removed = true;
					gone.New = 0;
					rows.push_back(std::move(gone));
				}
		// the tree follows the selected directory's subdirectories, once it lists them
		if (auto item = m_Tree.GetSelectedItem(); item && m_Tree.GetItemData(item))
			SyncNode(item, *entries);
	}
	m_All = std::move(rows);
	ShowRows(selected);
	if (newDirectory && m_List.GetItemCount())
		m_List.EnsureVisible(0, FALSE);
	UpdateStatus();
}

std::wstring CObjectManagerView::FullPath(Row const& row) const {
	return WinSys::ObjectNamespace::Join(m_Path, row.Name);
}

std::wstring CObjectManagerView::GetText(Row const& row, Column column) const {
	switch (column) {
		case Column::Name: return row.Name;
		case Column::Type: return row.Type;
		case Column::Target: return row.Target;
		case Column::Handles:
			// read when shown, once per refresh
			if (!row.HandlesRead && !row.Removed) {
				row.HandlesRead = true;
				row.Handles = WinSys::ObjectNamespace::CanOpen(row.Type) ? WinSys::ObjectNamespace::GetHandleCount(FullPath(row), row.Type) : std::nullopt;
			}
			return row.Handles ? std::to_wstring(*row.Handles) : L"";
		case Column::Created: return row.Created ? FormatHelper::FormatDateTime(row.Created) : L"";
		case Column::FullPath: return FullPath(row);
	}
	return L"";
}

bool CObjectManagerView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	return CFilterBox::Contains(row.Name, filter) || CFilterBox::Contains(row.Type, filter) || CFilterBox::Contains(row.Target, filter);
}

void CObjectManagerView::ShowRows(std::wstring const& selected) {
	m_Rows.clear();
	for (auto& row : m_All) {
		// the handle counts are read again for the rows shown
		row.HandlesRead = false;
		if (Matches(row))
			m_Rows.push_back(&row);
	}
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	else
		std::ranges::stable_sort(m_Rows, [](auto r1, auto r2) { return CompareNoCase(r1->Name, r2->Name) < 0; });
	SelectRow(selected);
	m_List.Invalidate();
}

CObjectManagerView::Row const* CObjectManagerView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CObjectManagerView::SelectRow(std::wstring const& name) {
	// the list view keeps the selection by index, which a new listing and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (name.empty())
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return CompareNoCase(row->Name, name) == 0; });
	if (it != m_Rows.end()) {
		int index = (int)(it - m_Rows.begin());
		m_List.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.EnsureVisible(index, FALSE);
	}
}

void CObjectManagerView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (m_Error != ERROR_SUCCESS) {
		text = std::format(L"{}: {}", m_Path, WinSys::Helpers::GetErrorText(m_Error));
		if (m_Error == ERROR_ACCESS_DENIED && !WinSys::SecurityHelper::IsRunningElevated())
			text += L" (run elevated)";
	}
	else {
		auto count = std::ranges::count_if(m_All, [](auto& r) { return !r.Removed; });
		text = std::format(L"{}: {} object{}", m_Path, count, count == 1 ? L"" : L"s");
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
	}
	Frame()->SetStatusText(0, text.c_str());
}

CString CObjectManagerView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CObjectManagerView::GetRowImage(HWND, int row, int) const {
	if (row >= (int)m_Rows.size())
		return -1;
	return Frame()->GetIconIndex(ObjectIcon(m_Rows[row]->Type));
}

int CObjectManagerView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by name (ShowRows, DoSort)
	return -1;
}

void CObjectManagerView::DoSort(SortInfo const* si) {
	auto selectedRow = GetSelected();
	auto selected = selectedRow ? selectedRow->Name : std::wstring();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
		int result;
		switch (column) {
			case Column::Handles:
				// read for every row
				GetText(*r1, column), GetText(*r2, column);
				result = compare(r1->Handles.value_or(0), r2->Handles.value_or(0));
				break;
			case Column::Created: result = compare(r1->Created, r2->Created); break;
			default: result = CompareNoCase(GetText(*r1, column), GetText(*r2, column)); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectRow(selected);
}

LRESULT CObjectManagerView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_List) {
		bHandled = FALSE;
		return 0;
	}
	// new objects green, removed ones red, as the Network tab's connections
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT: {
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto row = m_Rows[index];
			if (!row->New && !row->Removed)
				break;
			bool dark = WTLHelper::IsDarkMode();
			cd->clrTextBk = row->Removed ? (dark ? RGB(128, 0, 0) : RGB(255, 160, 160)) : (dark ? RGB(0, 96, 0) : RGB(160, 255, 160));
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CObjectManagerView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	auto& r = *m_Rows[row];
	CMenu menu;
	menu.CreatePopupMenu();
	if (r.Type == L"Directory")
		menu.AppendMenu(MF_STRING, ID_OBJMGR_OPEN, L"&Open");
	if (r.Type == L"SymbolicLink")
		menu.AppendMenu(MF_STRING, ID_OBJMGR_GOTOTARGET, L"&Go to Target");
	menu.AppendMenu(MF_STRING | (WinSys::ObjectNamespace::CanOpen(r.Type) && !r.Removed ? 0 : MF_GRAYED), ID_VIEW_PROPERTIES,
		L"&Properties...\tAlt+Enter");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, ID_OBJMGR_COPYPATH, L"Copy &Full Path");
	menu.SetMenuDefaultItem(r.Type == L"Directory" ? ID_OBJMGR_OPEN : r.Type == L"SymbolicLink" ? ID_OBJMGR_GOTOTARGET : ID_VIEW_PROPERTIES);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CObjectManagerView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	auto& r = *m_Rows[row];
	// posted to the frame (which routes it to this view), so a dialog doesn't run inside the list's mouse handling
	UINT id = r.Type == L"Directory" ? ID_OBJMGR_OPEN : r.Type == L"SymbolicLink" ? ID_OBJMGR_GOTOTARGET : ID_VIEW_PROPERTIES;
	::PostMessage(Frame()->GetHwnd(), WM_COMMAND, id, 0);
	return true;
}

LRESULT CObjectManagerView::OnTreeRightClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	// the node under the cursor, which the right click doesn't select
	CPoint pt;
	::GetCursorPos(&pt);
	CPoint client(pt);
	m_Tree.ScreenToClient(&client);
	UINT flags;
	auto item = m_Tree.HitTest(client, &flags);
	if (!item)
		return 0;
	m_Tree.SelectItem(item);
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_OBJMGR_DIRPROPERTIES, L"&Properties...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_OBJMGR_COPYDIRECTORY, L"&Copy Path");
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return 1;
}

LRESULT CObjectManagerView::OnOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row || row->Type != L"Directory")
		return 0;
	auto path = FullPath(*row);
	SelectDirectory(path);
	return 0;
}

LRESULT CObjectManagerView::OnGoToTarget(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (row && !row->Target.empty())
		GoToTarget(row->Target);
	return 0;
}

void CObjectManagerView::GoToTarget(std::wstring const& target) {
	// the deepest directory on the target's path, then the next component in it (\Device\HarddiskVolume3\Windows goes
	// to HarddiskVolume3 in \Device: a device's own namespace isn't the object manager's)
	std::vector<std::wstring> parts;
	for (size_t start = 1; start < target.size(); ) {
		auto end = target.find(L'\\', start);
		auto part = target.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
		if (!part.empty())
			parts.push_back(part);
		start = end == std::wstring::npos ? target.size() : end + 1;
	}
	if (!target.starts_with(L'\\') || parts.empty()) {
		Frame()->SetStatusText(0, std::format(L"The target {} isn't in the object namespace", target).c_str());
		return;
	}
	std::wstring directory = L"\\";
	size_t depth = 0;
	for (; depth < parts.size(); depth++) {
		auto next = WinSys::ObjectNamespace::Join(directory, parts[depth]);
		auto entries = WinSys::ObjectNamespace::EnumDirectory(next);
		if (!entries)
			break;
		directory = next;
	}
	if (!SelectDirectory(directory)) {
		Frame()->SetStatusText(0, std::format(L"Can't open the target's directory {}", directory).c_str());
		return;
	}
	if (depth < parts.size()) {
		// SelectDirectory loaded the list; a filter that hides the target goes, as Go to Process does
		if (!std::ranges::any_of(m_Rows, [&](auto row) { return CompareNoCase(row->Name, parts[depth]) == 0; }) && !m_Filter.GetText().empty())
			m_Filter.Clear();
		SelectRow(parts[depth]);
		m_List.SetFocus();
	}
	else
		m_Tree.SetFocus();
}

void CObjectManagerView::ShowProperties(std::wstring const& path, std::wstring const& type) {
	// a handle of this process, with the most access it gets; the handle sheet inspects it as any other
	wil::unique_handle h(WinSys::ObjectNamespace::Open(path, type, MAXIMUM_ALLOWED));
	if (!h)
		h.reset(WinSys::ObjectNamespace::Open(path, type, 0));
	auto entry = h ? WinSys::ObjectNamespace::Describe(h.get()) : std::nullopt;
	if (!entry) {
		auto error = ::GetLastError();
		AtlMessageBox(m_hWnd, std::format(L"Can't open {}: {}", path, error == ERROR_NOT_SUPPORTED
			? std::format(L"objects of type {} can't be opened by name", type) : WinSys::Helpers::GetErrorText(error)).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return;
	}
	WCHAR image[MAX_PATH];
	::GetModuleFileName(nullptr, image, _countof(image));
	auto name = wcsrchr(image, L'\\');
	CHandleSheet sheet(::GetCurrentProcess(), ::GetCurrentProcessId(), name ? name + 1 : image, *entry, path,
		[this](DWORD pid) { return Frame()->GetProcessManager().GetProcessNameById(pid); });
	// the object's, not the handle opened to show it
	sheet.SetSheetTitle(std::format(L"{} ({})", path, type));
	sheet.DoModal(m_hWnd);
}

LRESULT CObjectManagerView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row || row->Removed)
		return 0;
	// copies: a refresh replaces the rows while the sheet is up
	auto path = FullPath(*row);
	auto type = row->Type;
	ShowProperties(path, type);
	return 0;
}

LRESULT CObjectManagerView::OnDirectoryProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto item = m_Tree.GetSelectedItem())
		ShowProperties(GetItemPath(item), L"Directory");
	return 0;
}

LRESULT CObjectManagerView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (::GetFocus() == m_Tree) {
		if (auto item = m_Tree.GetSelectedItem())
			ClipboardHelper::CopyText(m_hWnd, GetItemPath(item).c_str());
		return 0;
	}
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

LRESULT CObjectManagerView::OnCopyPath(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		ClipboardHelper::CopyText(m_hWnd, FullPath(*row).c_str());
	return 0;
}

LRESULT CObjectManagerView::OnCopyDirectory(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto item = m_Tree.GetSelectedItem())
		ClipboardHelper::CopyText(m_hWnd, GetItemPath(item).c_str());
	return 0;
}

LRESULT CObjectManagerView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CObjectManagerView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

void CObjectManagerView::SaveSettings() {
	SaveColumns();
	auto& settings = AppSettings::Get();
	settings.ObjectManagerSplitterPos(m_Splitter.GetSplitterPosPct());
	if (m_Loaded)
		settings.ObjectManagerPath(m_Path);
	settings.Save();
}

LRESULT CObjectManagerView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveSettings();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CObjectManagerView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CObjectManagerView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CObjectManagerView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ObjectManagerColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CObjectManagerView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		auto row = GetSelected();
		auto selected = row ? row->Name : std::wstring();
		SetColumns(*columns);
		SaveColumns();
		ShowRows(selected);
	}
	return 0;
}

LRESULT CObjectManagerView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CObjectManagerView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	auto row = GetSelected();
	auto selected = row ? row->Name : std::wstring();
	SetColumns(columns);
	SaveColumns();
	ShowRows(selected);
	return 0;
}
