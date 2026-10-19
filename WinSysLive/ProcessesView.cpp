#include "pch.h"
#include "resource.h"
#include "ProcessesView.h"
#include "FormatHelper.h"
#include "AppSettings.h"
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>

using namespace WinSys;

namespace {
	struct ColumnInfo {
		PCWSTR Name;
		int Width;
		int Format;
	};

	// in CProcessesView::Column order
	const ColumnInfo s_Columns[] = {
		{ L"Name", 220, LVCFMT_LEFT },
		{ L"PID", 70, LVCFMT_RIGHT },
		{ L"CPU (%)", 70, LVCFMT_RIGHT },
		{ L"Private Bytes", 100, LVCFMT_RIGHT },
		{ L"Working Set", 100, LVCFMT_RIGHT },
		{ L"Threads", 60, LVCFMT_RIGHT },
		{ L"Handles", 70, LVCFMT_RIGHT },
		{ L"Session", 60, LVCFMT_RIGHT },
		{ L"User Name", 180, LVCFMT_LEFT },
		{ L"Priority", 90, LVCFMT_LEFT },
		{ L"Start Time", 150, LVCFMT_LEFT },
		{ L"Image Path", 300, LVCFMT_LEFT },
		{ L"Command Line", 400, LVCFMT_LEFT },
	};

	// ID_PRIORITY_* order
	const struct {
		WinSys::ProcessPriorityClass PriorityClass;
		int BasePriority;
	} s_Priorities[] = {
		{ WinSys::ProcessPriorityClass::Realtime, 24 },
		{ WinSys::ProcessPriorityClass::High, 13 },
		{ WinSys::ProcessPriorityClass::AboveNormal, 10 },
		{ WinSys::ProcessPriorityClass::Normal, 8 },
		{ WinSys::ProcessPriorityClass::BelowNormal, 6 },
		{ WinSys::ProcessPriorityClass::Idle, 4 },
	};

	// highlighting of new processes lasts this many updates
	const int NewProcessTicks = 2;
}

CString CProcessesView::GetTitle() const {
	return L"Processes";
}

LRESULT CProcessesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_hWndClient = m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// the image list is the frame's, so the list view must not destroy it
	m_Tree.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	m_Tree.SetCallbackMode(true);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());

	for (int i = 0; i < _countof(s_Columns); i++) {
		auto& c = s_Columns[i];
		m_Tree.InsertColumn(i, c.Name, c.Width, c.Format);
		m_Tree.SetColumnSortable(i, true);
	}

	m_TreeMode = AppSettings::Get().ProcessTreeMode();
	BuildItems();

	// enabled while active (see OnActivate)
	Frame()->GetUI().UIEnable(ID_VIEW_PROCESSTREE, false);
	Frame()->GetUI().UIEnable(ID_PROCESS_KILL, false);

	return 0;
}

void CProcessesView::OnActivate(bool active) {
	m_Active = active;
	UpdateUI();
	if (active)
		m_Tree.SetFocus();
}

void CProcessesView::OnThemeChanged(bool dark) {
	// the TreeListView doesn't follow WTLHelper's mode switch by itself
	m_Tree.SetBkColor(dark ? DarkMode::getViewBackgroundColor() : ::GetSysColor(COLOR_WINDOW));
	m_Tree.SetTextColor(dark ? DarkMode::getViewTextColor() : ::GetSysColor(COLOR_WINDOWTEXT));
	m_Tree.Invalidate();
}

void CProcessesView::ShowProcess(uint32_t pid) {
	auto process = Frame()->GetProcessManager().GetProcessById(pid);
	if (!process)
		return;
	if (auto it = m_Items.find(process->Key); it != m_Items.end() && it->second.Item) {
		m_Tree.SelectItem(it->second.Item);
		m_Tree.SetFocus();
	}
}

void CProcessesView::BuildItems() {
	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;

	m_Tree.BeginUpdate();
	m_Tree.DeleteAllItems();
	m_Items.clear();
	m_Tree.SetTreeLines(m_TreeMode);

	auto& processes = Frame()->GetProcessManager().GetProcesses();
	m_Items.reserve(processes.size() + 64);
	for (auto& p : processes)
		m_Items.insert({ p->Key, ProcessItem{ p } });
	// in snapshot order (not the map's), so unsorted siblings keep the system's order; parents are inserted first anyway
	for (auto& p : processes)
		EnsureInserted(m_Items.at(p->Key));

	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();

	RestoreSelection(selectedKey ? &*selectedKey : nullptr);
}

void CProcessesView::OnRefresh(bool /*active*/) {
	auto& pm = Frame()->GetProcessManager();
	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;

	m_Tree.BeginUpdate();

	// processes marked terminated on the previous update go away now; their NativeInfo points into
	// the snapshot buffer that this update just overwrote, so they must not be painted again
	std::vector<ProcessOrThreadKey> terminated;
	for (auto& [key, item] : m_Items) {
		if (item.Terminated)
			terminated.push_back(key);
		else if (item.NewTicks > 0)
			item.NewTicks--;
	}
	for (auto& key : terminated) {
		if (auto it = m_Items.find(key); it != m_Items.end())
			RemoveItem(it->second);
	}

	for (auto& p : pm.GetTerminatedProcesses()) {
		if (auto it = m_Items.find(p->Key); it != m_Items.end()) {
			it->second.Terminated = true;
			it->second.NewTicks = 0;
		}
	}

	// add all new processes before inserting, so a new parent and child are linked regardless of order
	std::vector<ProcessItem*> added;
	for (auto& p : pm.GetNewProcesses()) {
		auto [it, inserted] = m_Items.insert({ p->Key, ProcessItem{ p } });
		if (inserted) {
			it->second.NewTicks = NewProcessTicks;
			added.push_back(&it->second);
		}
	}
	for (auto item : added)
		EnsureInserted(*item);

	// values change on every update, so the order may too
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();

	RestoreSelection(selectedKey ? &*selectedKey : nullptr);
	UpdateUI();
}

HTLITEM CProcessesView::EnsureInserted(ProcessItem& item) {
	if (item.Item)
		return item.Item;

	auto parent = FindParent(item);
	auto hParent = parent ? EnsureInserted(*parent) : TLV_ROOT;
	if (item.Image < 0)
		item.Image = GetProcessIcon(*item.Process);
	item.Item = m_Tree.InsertItem(hParent, TLV_LAST, LPSTR_TEXTCALLBACK, item.Image, reinterpret_cast<LPARAM>(&item));
	return item.Item;
}

CProcessesView::ProcessItem* CProcessesView::FindParent(ProcessItem const& item) {
	if (!m_TreeMode)
		return nullptr;

	// a parent must be created before its child; otherwise the parent ID was reused by a newer process
	auto& process = *item.Process;
	auto parent = Frame()->GetProcessManager().GetProcessById(process.ParentId);
	if (!parent || parent->Id == process.Id || parent->Key.Created >= process.Key.Created)
		return nullptr;

	auto it = m_Items.find(parent->Key);
	return it == m_Items.end() ? nullptr : &it->second;
}

void CProcessesView::RemoveItem(ProcessItem& item) {
	// DeleteItem takes the whole subtree; the children move to the top level, since their parent is gone
	std::vector<ProcessItem*> descendants;
	CollectDescendants(item.Item, descendants);
	m_Tree.DeleteItem(item.Item);
	for (auto d : descendants)
		d->Item = nullptr;

	m_Items.erase(item.Process->Key);
	for (auto d : descendants)
		EnsureInserted(*d);
}

void CProcessesView::CollectDescendants(HTLITEM hItem, std::vector<ProcessItem*>& items) const {
	for (auto hChild = m_Tree.GetChildItem(hItem); hChild; hChild = m_Tree.GetNextSiblingItem(hChild)) {
		items.push_back(reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(hChild)));
		CollectDescendants(hChild, items);
	}
}

CProcessesView::ProcessItem* CProcessesView::GetSelectedProcess() const {
	auto hItem = m_Tree.GetSelectedItem();
	return hItem ? reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(hItem)) : nullptr;
}

void CProcessesView::RestoreSelection(ProcessOrThreadKey const* key) {
	// the list view keeps the selection by index, which inserts, deletes and sorting shift
	if (key) {
		if (auto it = m_Items.find(*key); it != m_Items.end() && it->second.Item) {
			m_Tree.SelectItem(it->second.Item, false);
			return;
		}
	}
	m_Tree.DeselectAll();
}

LRESULT CProcessesView::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto& item = *reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(di->hItem));
	wcsncpy_s(di->pszText, di->cchTextMax, GetColumnText(item, (Column)di->iSubItem).c_str(), _TRUNCATE);
	return 0;
}

std::wstring CProcessesView::GetColumnText(ProcessItem& item, Column column) const {
	auto& p = *item.Process;
	auto info = p.NativeInfo;

	switch (column) {
		case Column::Name: return p.GetImageName();
		case Column::Id: return std::to_wstring(p.Id);
		case Column::CPU: return p.Id == 0 || !item.Terminated ? FormatHelper::FormatCpu(p.CPU) : L"";
		case Column::PrivateBytes: return FormatHelper::FormatSize(info->PrivatePageCount);
		case Column::WorkingSet: return FormatHelper::FormatSize(info->WorkingSetSize);
		case Column::Threads: return FormatHelper::FormatNumber(info->NumberOfThreads);
		case Column::Handles: return FormatHelper::FormatNumber(info->HandleCount);
		case Column::Session: return p.Id == 0 ? L"" : std::to_wstring(info->SessionId);
		case Column::User: return p.GetUser();
		case Column::Priority: return FormatHelper::FormatPriority(info->BasePriority);
		case Column::StartTime: return FormatHelper::FormatDateTime(p.Key.Created);
		case Column::ImagePath: return p.GetImagePath();
		case Column::CommandLine: return p.GetCommandLine();
	}
	return L"";
}

int CProcessesView::GetProcessIcon(ProcessInfoEx& process) {
	auto defaultIcon = Frame()->GetIconIndex(IDI_PROCESS);
	auto& path = process.GetImagePath();
	if (path.empty())
		return defaultIcon;

	CString key(path.c_str());
	key.MakeLower();
	if (auto it = m_Icons.find((PCWSTR)key); it != m_Icons.end())
		return it->second;

	HICON hIcon = nullptr;
	::ExtractIconEx(path.c_str(), 0, nullptr, &hIcon, 1);
	int index = defaultIcon;
	if (hIcon) {
		index = ImageList_AddIcon(Frame()->GetImageList(), hIcon);
		::DestroyIcon(hIcon);
	}
	m_Icons.insert({ (PCWSTR)key, index });
	return index;
}

LRESULT CProcessesView::OnSort(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ns = reinterpret_cast<NMTLVSORT*>(hdr);
	m_SortColumn = ns->iSubItem;
	m_SortAscending = ns->iDirection > 0;

	auto selected = GetSelectedProcess();
	auto selectedKey = selected ? std::optional(selected->Process->Key) : std::nullopt;
	Sort();
	RestoreSelection(selectedKey ? &*selectedKey : nullptr);
	return 0;
}

void CProcessesView::Sort() {
	// in tree mode this sorts siblings; in flat mode everything is top level, so it's a full sort
	m_Tree.SortChildren(TLV_ROOT, CompareItems, reinterpret_cast<LPARAM>(this), true);
}

int CALLBACK CProcessesView::CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam) {
	auto view = reinterpret_cast<CProcessesView*>(lParam);
	auto& item1 = *reinterpret_cast<ProcessItem*>(view->m_Tree.GetItemData(h1));
	auto& item2 = *reinterpret_cast<ProcessItem*>(view->m_Tree.GetItemData(h2));
	auto result = view->CompareProcesses(item1, item2);
	return view->m_SortAscending ? result : -result;
}

int CProcessesView::CompareProcesses(ProcessItem& item1, ProcessItem& item2) const {
	auto& p1 = *item1.Process;
	auto& p2 = *item2.Process;
	auto i1 = p1.NativeInfo, i2 = p2.NativeInfo;

	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	auto compareText = [](std::wstring const& a, std::wstring const& b) { return ::_wcsicmp(a.c_str(), b.c_str()); };

	switch ((Column)m_SortColumn) {
		case Column::Name: return compareText(p1.GetImageName(), p2.GetImageName());
		case Column::Id: return compare(p1.Id, p2.Id);
		case Column::CPU: return compare(p1.CPU, p2.CPU);
		case Column::PrivateBytes: return compare(i1->PrivatePageCount, i2->PrivatePageCount);
		case Column::WorkingSet: return compare(i1->WorkingSetSize, i2->WorkingSetSize);
		case Column::Threads: return compare(i1->NumberOfThreads, i2->NumberOfThreads);
		case Column::Handles: return compare(i1->HandleCount, i2->HandleCount);
		case Column::Session: return compare(i1->SessionId, i2->SessionId);
		case Column::User: return compareText(p1.GetUser(), p2.GetUser());
		case Column::Priority: return compare(i1->BasePriority, i2->BasePriority);
		case Column::StartTime: return compare(p1.Key.Created, p2.Key.Created);
		case Column::ImagePath: return compareText(p1.GetImagePath(), p2.GetImagePath());
		case Column::CommandLine: return compareText(p1.GetCommandLine(), p2.GetCommandLine());
	}
	return 0;
}

LRESULT CProcessesView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}

	auto cd = reinterpret_cast<NMTLVCUSTOMDRAW*>(hdr);
	if ((cd->nmcd.dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT && cd->hItem) {
		auto& item = *reinterpret_cast<ProcessItem*>(m_Tree.GetItemData(cd->hItem));
		bool dark = WTLHelper::IsDarkMode();
		if (item.Terminated) {
			cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
		}
		else if (item.NewTicks > 0) {
			cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CProcessesView::OnSelectionChanged(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	UpdateUI();
	return 0;
}

bool CProcessesView::CanControl(ProcessItem const* item) {
	// not Idle and System, and not a process that's gone
	return item && !item->Terminated && item->Process->Id > 4;
}

void CProcessesView::UpdateUI() {
	// the frame's commands (menu, toolbar) that this view handles; the rest are on the context menu (see OnRightClick)
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_VIEW_PROCESSTREE, m_Active);
	ui.UIEnable(ID_PROCESS_KILL, m_Active && CanControl(GetSelectedProcess()));
	if (m_Active)
		ui.UISetCheck(ID_VIEW_PROCESSTREE, m_TreeMode);
}

LRESULT CProcessesView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
	auto item = GetSelectedProcess();
	if (ia->iItem < 0 || !item)
		return 0;

	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CMenuHandle popup = menu.GetSubMenu(0);
	WTLHelper::InitMenu(popup, MenuItemData{ ID_PROCESS_KILL, IDI_PROCESS_KILL });

	bool live = CanControl(item);
	for (UINT id : { ID_PROCESS_KILL, ID_PROCESS_SUSPEND, ID_PROCESS_RESUME })
		popup.EnableMenuItem(id, MF_BYCOMMAND | (live ? MF_ENABLED : MF_GRAYED));
	popup.EnableMenuItem(ID_PROCESS_GOTOFILELOCATION, MF_BYCOMMAND | (item->Process->GetImagePath().empty() ? MF_GRAYED : MF_ENABLED));
	for (int i = 0; i < _countof(s_Priorities); i++) {
		popup.EnableMenuItem(ID_PRIORITY_REALTIME + i, MF_BYCOMMAND | (live ? MF_ENABLED : MF_GRAYED));
		if (s_Priorities[i].BasePriority == item->Process->NativeInfo->BasePriority)
			popup.CheckMenuRadioItem(ID_PRIORITY_REALTIME, ID_PRIORITY_IDLE, ID_PRIORITY_REALTIME + i, MF_BYCOMMAND);
	}

	// ptAction is relative to the inner list view
	CPoint pt(ia->ptAction);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
	return 0;
}

void CProcessesView::ShowError(PCWSTR operation) const {
	auto error = ::GetLastError();
	AtlMessageBox(m_hWnd, std::format(L"{} failed: {}", operation, Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

LRESULT CProcessesView::OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	auto& p = *item->Process;
	auto text = std::format(L"Terminate process {} ({})?", p.GetImageName(), p.Id);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;

	auto process = Process::OpenById(p.Id, ProcessAccessMask::Terminate);
	if (!process) {
		ShowError(L"Opening the process");
		return 0;
	}
	if (!process->Terminate())
		AtlMessageBox(m_hWnd, L"Failed to terminate the process.", IDR_MAINFRAME, MB_ICONERROR);
	return 0;
}

LRESULT CProcessesView::OnSuspend(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	auto process = Process::OpenById(item->Process->Id, ProcessAccessMask::SuspendResume);
	if (!process)
		ShowError(L"Opening the process");
	else if (!process->Suspend())
		AtlMessageBox(m_hWnd, L"Failed to suspend the process.", IDR_MAINFRAME, MB_ICONERROR);
	return 0;
}

LRESULT CProcessesView::OnResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	auto process = Process::OpenById(item->Process->Id, ProcessAccessMask::SuspendResume);
	if (!process)
		ShowError(L"Opening the process");
	else if (!process->Resume())
		AtlMessageBox(m_hWnd, L"Failed to resume the process.", IDR_MAINFRAME, MB_ICONERROR);
	return 0;
}

LRESULT CProcessesView::OnSetPriority(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Terminated)
		return 0;

	// ProcessAccessMask has no SetInformation value
	auto process = Process::OpenById(item->Process->Id, static_cast<ProcessAccessMask>(PROCESS_SET_INFORMATION));
	if (!process)
		ShowError(L"Opening the process");
	else if (!process->SetPriorityClass(s_Priorities[id - ID_PRIORITY_REALTIME].PriorityClass))
		ShowError(L"Setting the priority");
	return 0;
}

LRESULT CProcessesView::OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item || item->Process->GetImagePath().empty())
		return 0;

	auto args = std::format(L"/select,\"{}\"", item->Process->GetImagePath());
	::ShellExecute(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CProcessesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedProcess();
	if (!item)
		return 0;

	// all columns, tab separated
	std::wstring text;
	for (int i = 0; i < (int)Column::_Count; i++) {
		if (i > 0)
			text += L'\t';
		text += GetColumnText(*item, (Column)i);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CProcessesView::OnToggleTree(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_TreeMode = !m_TreeMode;
	AppSettings::Get().ProcessTreeMode(m_TreeMode);
	// flat mode puts every process at the top level, so sorting covers all of them
	BuildItems();
	UpdateUI();
	return 0;
}
