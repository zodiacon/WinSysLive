#include "pch.h"
#include "ZombiesView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/Handles.h>
#include <WinSys/Helpers.h>
#include <WinSys/Processes.h>
#include <WinSys/SecurityHelper.h>
#include <ClipboardHelper.h>

namespace {
	// in CZombiesView::Column order; the keys are stored in the ZombieColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Zombie", L"Zombie", 180, LVCFMT_LEFT, true, true },
		{ L"PID", L"Pid", 70, LVCFMT_RIGHT, true },
		{ L"Exited", L"Exited", 150, LVCFMT_LEFT, true },
		{ L"Dead For", L"DeadFor", 110, LVCFMT_RIGHT, true },
		{ L"Exit Code", L"ExitCode", 90, LVCFMT_RIGHT, true },
		{ L"Holder", L"Holder", 180, LVCFMT_LEFT, true },
		{ L"Holder PID", L"HolderPid", 75, LVCFMT_RIGHT, true },
		{ L"Handles", L"Handles", 65, LVCFMT_RIGHT, true },
		{ L"Handle Values", L"Handle", 200, LVCFMT_LEFT, true },
		{ L"Access", L"Access", 90, LVCFMT_LEFT, true },
		{ L"Image Path", L"ImagePath", 350, LVCFMT_LEFT, false },
	};

	int64_t Now() {
		int64_t now;
		::GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&now));
		return now;
	}
}

CString CZombiesView::GetTitle() const {
	return L"Zombie Processes";
}

LRESULT CZombiesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	// subitem images: both the zombie and the holder have icons
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	// the frame's image list, for the processes' icons
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().ZombieColumns(), s_Columns));

	int filterWidth = 280;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Zombie, holder, PID or path (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

void CZombiesView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		// scanned only while the tab is active
		if (::GetTickCount64() - m_LastScan >= ScanInterval || !m_ScanTime)
			StartScan();
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CZombiesView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CZombiesView::OnRefresh(bool active) {
	if (!active)
		return;
	if (::GetTickCount64() - m_LastScan >= ScanInterval)
		StartScan();
	// the Dead For column moves on
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

void CZombiesView::StartScan() {
	if (m_Scanning)
		return;
	m_Scanning = true;
	m_LastScan = ::GetTickCount64();
	// the previous scan is done (m_Scanning was off): joining it doesn't wait
	m_Worker = std::jthread([this, hWnd = m_hWnd] {
		auto zombies = WinSys::ZombieProcesses::Find();
		auto error = zombies ? ERROR_SUCCESS : ::GetLastError();
		{
			std::lock_guard lock(m_ResultLock);
			m_Result = std::move(zombies);
			m_ResultError = error;
		}
		::PostMessage(hWnd, WM_ZOMBIES_FOUND, 0, 0);
		});
}

LRESULT CZombiesView::OnZombiesFound(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Scanning = false;
	std::optional<std::vector<WinSys::ZombieProcess>> result;
	{
		std::lock_guard lock(m_ResultLock);
		result = std::move(m_Result);
		m_ScanError = m_ResultError;
	}
	if (!result) {
		UpdateStatus();
		return 0;
	}

	// the rows point into m_Zombies, which is replaced: the selection is taken first
	auto selected = GetSelectedKey();
	m_Rows.clear();
	m_All.clear();
	m_Zombies = std::move(*result);
	m_ScanTime = Now();
	// a row per holder process, with its handles in the order they were found
	for (auto& zombie : m_Zombies)
		for (auto& holder : zombie.Holders) {
			auto it = std::ranges::find_if(m_All, [&](auto& r) { return r.Zombie == &zombie && r.HolderPid == holder.Pid; });
			if (it == m_All.end())
				m_All.push_back({ &zombie, holder.Pid, { holder }, std::format(L"{}|{}|{}", zombie.Pid, zombie.CreateTime, holder.Pid) });
			else
				it->Handles.push_back(holder);
		}
	ShowRows(selected);
	UpdateStatus();
	return 0;
}

std::wstring CZombiesView::GetProcessName(uint32_t pid) const {
	return Frame()->GetProcessManager().GetProcessNameById(pid);
}

std::wstring CZombiesView::ZombieName(WinSys::ZombieProcess const& zombie) {
	auto slash = zombie.ImagePath.rfind(L'\\');
	return slash == std::wstring::npos ? zombie.ImagePath : zombie.ImagePath.substr(slash + 1);
}

std::wstring CZombiesView::GetText(Row const& row, Column column) const {
	auto& zombie = *row.Zombie;
	switch (column) {
		case Column::Zombie: return ZombieName(zombie);
		case Column::Pid: return std::to_wstring(zombie.Pid);
		case Column::Exited: return zombie.ExitTime ? FormatHelper::FormatDateTime(zombie.ExitTime) : L"";
		case Column::DeadFor: return zombie.ExitTime ? FormatHelper::FormatTimeSpan(Now() - zombie.ExitTime) : L"";
		case Column::ExitCode: return std::format(L"0x{:X}", zombie.ExitCode);
		case Column::Holder: return GetProcessName(row.HolderPid);
		case Column::HolderPid: return std::to_wstring(row.HolderPid);
		case Column::Handles: return std::to_wstring(row.Handles.size());
		case Column::Handle: {
			std::wstring text;
			for (auto& h : row.Handles)
				text += std::format(L"{}0x{:X}", text.empty() ? L"" : L", ", h.Handle);
			return text;
		}
		case Column::Access: {
			// usually all the same
			std::vector<uint32_t> access;
			for (auto& h : row.Handles)
				if (!std::ranges::contains(access, h.Access))
					access.push_back(h.Access);
			std::wstring text;
			for (auto a : access)
				text += std::format(L"{}0x{:X}", text.empty() ? L"" : L", ", a);
			return text;
		}
		case Column::ImagePath: return zombie.ImagePath;
	}
	return L"";
}

bool CZombiesView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Zombie, Column::Pid, Column::Holder, Column::HolderPid, Column::ImagePath })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

std::wstring CZombiesView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? row->Key : L"";
}

CZombiesView::Row const* CZombiesView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CZombiesView::ShowRows(std::wstring const& selected) {
	m_Rows.clear();
	for (auto& row : m_All)
		if (Matches(row))
			m_Rows.push_back(&row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectRow(selected);
	m_List.Invalidate();
}

void CZombiesView::SelectRow(std::wstring const& key) {
	// the list view keeps the selection by index, which a new scan and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (key.empty())
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Key == key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CZombiesView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (m_ScanError)
		text = std::format(L"Scanning the handle table failed: {}", WinSys::Helpers::GetErrorText(m_ScanError));
	else if (!m_ScanTime)
		text = L"Scanning...";
	else {
		size_t handles = 0;
		for (auto& zombie : m_Zombies)
			handles += zombie.Holders.size();
		text = std::format(L"{} zombie process{} kept by {} handle{}", m_Zombies.size(), m_Zombies.size() == 1 ? L"" : L"es",
			handles, handles == 1 ? L"" : L"s");
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
		// the holders' handles are duplicated: without elevation, only those of the processes this user can open
		if (!WinSys::SecurityHelper::IsRunningElevated())
			text += L"; not elevated: other users' handles aren't seen";
	}
	Frame()->SetStatusText(0, text.c_str());
}

CString CZombiesView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CZombiesView::GetRowImage(HWND, int row, int col) const {
	// the zombie's icon (from its executable: it's not in the snapshot) and the holder's, wherever their columns were
	// dragged (subitem images)
	if (row >= (int)m_Rows.size())
		return -1;
	switch (GetColumnManager(m_List)->GetColumnTag<Column>(col)) {
		case Column::Zombie: return Frame()->GetProcessIcon(m_Rows[row]->Zombie->ImagePath);
		case Column::Holder: {
			auto process = Frame()->GetProcessManager().GetProcessById(m_Rows[row]->HolderPid);
			return process ? Frame()->GetProcessIcon(*process) : -1;
		}
	}
	return -1;
}

int CZombiesView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by row key (ShowRows, DoSort)
	return -1;
}

void CZombiesView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	switch (column) {
		case Column::Pid: case Column::Exited: case Column::DeadFor: case Column::ExitCode: case Column::Handles:
		case Column::HolderPid:
			std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
				auto& z1 = *r1->Zombie;
				auto& z2 = *r2->Zombie;
				int result = 0;
				switch (column) {
					case Column::Pid: result = compare(z1.Pid, z2.Pid); break;
					case Column::Exited: result = compare(z1.ExitTime, z2.ExitTime); break;
					// the longer dead, the earlier it exited
					case Column::DeadFor: result = compare(z2.ExitTime, z1.ExitTime); break;
					case Column::ExitCode: result = compare(z1.ExitCode, z2.ExitCode); break;
					case Column::Handles: result = compare(r1->Handles.size(), r2->Handles.size()); break;
					case Column::HolderPid: result = compare(r1->HolderPid, r2->HolderPid); break;
				}
				return si->SortAscending ? result < 0 : result > 0;
				});
			break;
		default: {
			// a text column: each row's text formatted once (a process name is a lookup), not twice per comparison
			std::vector<std::pair<std::wstring, Row const*>> texts;
			texts.reserve(m_Rows.size());
			for (auto row : m_Rows)
				texts.push_back({ GetText(*row, column), row });
			std::ranges::stable_sort(texts, [&](auto const& t1, auto const& t2) {
				int result = ::_wcsicmp(t1.first.c_str(), t2.first.c_str());
				return si->SortAscending ? result < 0 : result > 0;
				});
			for (size_t i = 0; i < texts.size(); i++)
				m_Rows[i] = texts[i].second;
			break;
		}
	}
	SelectRow(selected);
}

bool CZombiesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_ZOMBIE_GOTOHOLDER, L"&Go to Holder");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_ZOMBIE_CLOSEHANDLE, L"C&lose Handles...");
	menu.AppendMenu(MF_STRING, ID_ZOMBIE_CLOSEALL, L"Close &All Handles to the Zombie...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.SetMenuDefaultItem(ID_ZOMBIE_GOTOHOLDER);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CZombiesView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	Frame()->GoToProcess(m_Rows[row]->HolderPid);
	return true;
}

LRESULT CZombiesView::OnGoToHolder(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		Frame()->GoToProcess(row->HolderPid);
	return 0;
}

DWORD CZombiesView::CloseHandles(std::vector<WinSys::ZombieHolder> const& holders) {
	DWORD error = ERROR_SUCCESS;
	for (auto& holder : holders) {
		// only if the value still refers to a process (it may have been closed and reused since the scan)
		wil::unique_handle hProcess(::OpenProcess(PROCESS_DUP_HANDLE, FALSE, holder.Pid));
		if ((!hProcess || !WinSys::HandleNames::CloseInProcess(hProcess.get(), holder.Handle, L"Process")) && error == ERROR_SUCCESS)
			error = ::GetLastError();
	}
	return error;
}

LRESULT CZombiesView::OnCloseHandle(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row)
		return 0;
	// copies: a scan may replace the rows while the message box is up
	auto handles = row->Handles;
	auto holderPid = row->HolderPid;
	auto name = ZombieName(*row->Zombie);
	auto pid = row->Zombie->Pid;
	auto total = row->Zombie->Holders.size();
	auto text = std::format(L"Close {} in {} ({})?\n\n{} of the {} handle{} keeping {} ({}) alive. The holder may fail if it still uses them.",
		handles.size() == 1 ? std::format(L"handle 0x{:X}", handles[0].Handle) : std::format(L"its {} handles to the zombie", handles.size()),
		GetProcessName(holderPid), holderPid, handles.size() == total ? L"All" : std::to_wstring(handles.size()), total,
		total == 1 ? L"" : L"s", name, pid);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;
	if (auto error = CloseHandles(handles); error != ERROR_SUCCESS)
		AtlMessageBox(m_hWnd, std::format(L"Closing a handle failed: {}", WinSys::Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
	// the next scan shows what's left
	m_LastScan = 0;
	StartScan();
	return 0;
}

LRESULT CZombiesView::OnCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row)
		return 0;
	// copies: a scan may replace the rows while the message box is up
	auto holders = row->Zombie->Holders;
	auto name = ZombieName(*row->Zombie);
	auto pid = row->Zombie->Pid;
	std::wstring list;
	for (auto& holder : holders)
		list += std::format(L"\n  0x{:X} in {} ({})", holder.Handle, GetProcessName(holder.Pid), holder.Pid);
	auto text = std::format(L"Close the {} handle{} keeping {} ({}) alive?\n{}\n\nThe holders may fail if they still use them.",
		holders.size(), holders.size() == 1 ? L"" : L"s", name, pid, list);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;
	if (auto error = CloseHandles(holders); error != ERROR_SUCCESS)
		AtlMessageBox(m_hWnd, std::format(L"Closing a handle failed: {}", WinSys::Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
	m_LastScan = 0;
	StartScan();
	return 0;
}

LRESULT CZombiesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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

LRESULT CZombiesView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CZombiesView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CZombiesView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	// a scan in progress finishes (its message goes nowhere) before the view goes
	if (m_Worker.joinable())
		m_Worker.join();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CZombiesView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CZombiesView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CZombiesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ZombieColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CZombiesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CZombiesView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CZombiesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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
