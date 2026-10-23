#include "pch.h"
#include "DiskView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>

namespace {
	// in CDiskView::Column order; the keys are stored in the DiskColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Process", L"Process", 170, LVCFMT_LEFT, true, true },
		{ L"PID", L"Pid", 65, LVCFMT_RIGHT, true },
		{ L"File", L"File", 420, LVCFMT_LEFT, true },
		{ L"Read (B/s)", L"ReadRate", 90, LVCFMT_RIGHT, true },
		{ L"Write (B/s)", L"WriteRate", 90, LVCFMT_RIGHT, true },
		{ L"Total (B/s)", L"TotalRate", 90, LVCFMT_RIGHT, true },
		{ L"Bytes Read", L"BytesRead", 90, LVCFMT_RIGHT, true },
		{ L"Bytes Written", L"BytesWritten", 90, LVCFMT_RIGHT, true },
		{ L"Reads", L"Reads", 70, LVCFMT_RIGHT, false },
		{ L"Writes", L"Writes", 70, LVCFMT_RIGHT, false },
		{ L"First I/O", L"FirstSeen", 150, LVCFMT_LEFT, false },
		{ L"Last I/O", L"LastSeen", 150, LVCFMT_LEFT, true },
	};

	// the session's name is this and the process ID
	const PCWSTR SessionPrefix = L"WinSysLive Disk";
	// highlighting of new rows lasts this many refreshes
	const int NewRowTicks = 2;

	std::wstring FormatRate(double rate) {
		// below a byte a second is nothing
		return rate >= 1 ? FormatHelper::FormatBytes((uint64_t)rate) : L"";
	}
}

CString CDiskView::GetTitle() const {
	return L"Disk";
}

LRESULT CDiskView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	// the frame's image list, for the processes' icons
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().DiskColumns(), s_Columns));

	int filterWidth = 280;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Process, PID or file (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

void CDiskView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		// the session costs a little all the time: it starts only when the tab is first shown
		StartTracker();
		if (m_RowsPending)
			ShowRows(m_PendingSelection);
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CDiskView::StartTracker() {
	if (m_Tracker || m_TrackerError)
		return;
	auto tracker = std::make_unique<WinSys::FileIoTracker>();
	if (tracker->Start(SessionPrefix))
		m_Tracker = std::move(tracker);
	else
		m_TrackerError = ::GetLastError();
}

void CDiskView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CDiskView::OnRefresh(bool active) {
	if (!m_Tracker)
		return;
	// an exited process's ID may be reused: its rows go
	for (auto& p : Frame()->GetProcessManager().GetTerminatedProcesses())
		m_Tracker->RemoveProcess(p->Id);

	LARGE_INTEGER now, frequency;
	::QueryPerformanceCounter(&now);
	::QueryPerformanceFrequency(&frequency);

	// the rows point into m_All, which the merge replaces: the selection is taken first, and the rows are rebuilt
	// (while hidden, when the view is shown)
	auto selected = m_RowsPending ? m_PendingSelection : GetSelectedKey();
	m_Rows.clear();
	Merge(m_Tracker->GetFiles(), now.QuadPart, frequency.QuadPart);
	m_Loaded = true;
	if (!active) {
		m_RowsPending = true;
		m_PendingSelection = selected;
		return;
	}
	ShowRows(selected);
	UpdateStatus();
}

void CDiskView::Merge(std::vector<WinSys::FileIoCounters> files, int64_t now, int64_t frequency) {
	std::map<Key, Row*> previous;
	for (auto& row : m_All)
		previous[row.GetKey()] = &row;

	std::vector<Row> merged;
	merged.reserve(files.size());
	for (auto& file : files) {
		Row row{ std::move(file) };
		if (auto it = previous.find(row.GetKey()); it != previous.end()) {
			auto& before = *it->second;
			row.NewTicks = (std::max)(before.NewTicks - 1, 0);
			row.History = std::move(before.History);
			// the name may have been learned since, but not lost
			if (row.File.Name.empty())
				row.File.Name = std::move(before.File.Name);
			previous.erase(it);
		}
		else if (m_Loaded)
			// on the first read, they're all new to the view, not to the system
			row.NewTicks = NewRowTicks;

		row.History.push_back({ now, row.File.BytesRead, row.File.BytesWritten });
		while (row.History.size() > RateUpdates + 1)
			row.History.pop_front();
		auto& first = row.History.front();
		auto& last = row.History.back();
		if (last.Time > first.Time) {
			double seconds = double(last.Time - first.Time) / frequency;
			row.ReadRate = (last.Read - first.Read) / seconds;
			row.WriteRate = (last.Written - first.Written) / seconds;
		}
		merged.push_back(std::move(row));
	}
	// the rows the tracker forgot, shown once more
	for (auto& [key, row] : previous)
		if (!row->Gone) {
			Row gone{ std::move(row->File) };
			gone.Gone = true;
			merged.push_back(std::move(gone));
		}
	// the tracker's order is a hash table's: until a header is clicked, the rows stay by process and file
	std::ranges::sort(merged, [](auto const& r1, auto const& r2) { return r1.GetKey() < r2.GetKey(); });
	m_All = std::move(merged);
}

std::wstring CDiskView::GetProcessName(uint32_t pid) const {
	if (pid == 0)
		return L"Idle";
	auto name = Frame()->GetProcessManager().GetProcessNameById(pid);
	return name.empty() ? L"" : name;
}

std::wstring CDiskView::GetText(Row const& row, Column column) const {
	auto& file = row.File;
	switch (column) {
		case Column::Process: return GetProcessName(file.Pid);
		case Column::Pid: return std::to_wstring(file.Pid);
		case Column::File: return file.Name.empty() ? std::format(L"(file 0x{:X})", file.FileKey) : file.Name;
		case Column::ReadRate: return FormatRate(row.ReadRate);
		case Column::WriteRate: return FormatRate(row.WriteRate);
		case Column::TotalRate: return FormatRate(row.ReadRate + row.WriteRate);
		case Column::BytesRead: return FormatHelper::FormatBytes(file.BytesRead);
		case Column::BytesWritten: return FormatHelper::FormatBytes(file.BytesWritten);
		case Column::Reads: return file.Reads ? FormatHelper::FormatNumber((int64_t)file.Reads) : L"";
		case Column::Writes: return file.Writes ? FormatHelper::FormatNumber((int64_t)file.Writes) : L"";
		case Column::FirstSeen: return FormatHelper::FormatDateTime(file.FirstSeen);
		case Column::LastSeen: return FormatHelper::FormatDateTime(file.LastSeen);
	}
	return L"";
}

bool CDiskView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Process, Column::Pid, Column::File })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

std::optional<CDiskView::Key> CDiskView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? std::optional(row->GetKey()) : std::nullopt;
}

CDiskView::Row const* CDiskView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CDiskView::ShowRows(std::optional<Key> selected) {
	m_RowsPending = false;
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

void CDiskView::SelectRow(std::optional<Key> key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (!key)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->GetKey() == *key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CDiskView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (!m_Tracker)
		text = m_TrackerError == ERROR_ACCESS_DENIED
			? L"Disk monitoring needs administrator rights (File > Run as Administrator)"
			: std::format(L"Starting disk monitoring failed: {}", WinSys::Helpers::GetErrorText(m_TrackerError));
	else {
		double read = 0, write = 0;
		for (auto& row : m_All) {
			read += row.ReadRate;
			write += row.WriteRate;
		}
		text = std::format(L"{} files with I/O in the last minute", std::ranges::count_if(m_All, [](auto& r) { return !r.Gone; }));
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
		auto rate = [](double r) { auto text = FormatRate(r); return text.empty() ? std::wstring(L"0 B") : text; };
		text += std::format(L"; read {}/s, write {}/s; since {}", rate(read), rate(write), FormatHelper::FormatDateTime(m_Tracker->GetStartTime()));
	}
	Frame()->SetStatusText(0, text.c_str());
}

CString CDiskView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CDiskView::GetRowImage(HWND, int row, int col) const {
	// the process's icon, wherever that column was dragged
	if (row >= (int)m_Rows.size() || GetColumnManager(m_List)->GetColumnTag<Column>(col) != Column::Process)
		return -1;
	auto process = Frame()->GetProcessManager().GetProcessById(m_Rows[row]->File.Pid);
	return process ? Frame()->GetProcessIcon(*process) : -1;
}

int CDiskView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by row key (ShowRows, DoSort)
	return -1;
}

void CDiskView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	switch (column) {
		case Column::Process: case Column::File: {
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
		default:
			std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
				auto& f1 = r1->File;
				auto& f2 = r2->File;
				int result = 0;
				switch (column) {
					case Column::Pid: result = compare(f1.Pid, f2.Pid); break;
					case Column::ReadRate: result = compare(r1->ReadRate, r2->ReadRate); break;
					case Column::WriteRate: result = compare(r1->WriteRate, r2->WriteRate); break;
					case Column::TotalRate: result = compare(r1->ReadRate + r1->WriteRate, r2->ReadRate + r2->WriteRate); break;
					case Column::BytesRead: result = compare(f1.BytesRead, f2.BytesRead); break;
					case Column::BytesWritten: result = compare(f1.BytesWritten, f2.BytesWritten); break;
					case Column::Reads: result = compare(f1.Reads, f2.Reads); break;
					case Column::Writes: result = compare(f1.Writes, f2.Writes); break;
					case Column::FirstSeen: result = compare(f1.FirstSeen, f2.FirstSeen); break;
					case Column::LastSeen: result = compare(f1.LastSeen, f2.LastSeen); break;
				}
				return si->SortAscending ? result < 0 : result > 0;
				});
			break;
	}
	SelectRow(selected);
}

LRESULT CDiskView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			// the Network tab's colors: gone red, new green
			auto row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			if (row->Gone) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row->NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CDiskView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	// a file's own commands need a path, not an unnamed one or a device's (\Device\...)
	auto& name = m_Rows[row]->File.Name;
	bool path = name.size() > 2 && name[1] == L':';
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_DISK_GOTOPROCESS, L"Go to &Process");
	menu.AppendMenu(MF_STRING | (path ? 0 : MF_GRAYED), ID_DISK_PROPERTIES, L"File P&roperties...");
	menu.AppendMenu(MF_STRING | (path ? 0 : MF_GRAYED), ID_DISK_OPENLOCATION, L"Open File &Location");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.SetMenuDefaultItem(ID_DISK_GOTOPROCESS);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CDiskView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	Frame()->GoToProcess(m_Rows[row]->File.Pid);
	return true;
}

LRESULT CDiskView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		Frame()->GoToProcess(row->File.Pid);
	return 0;
}

LRESULT CDiskView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && !row->File.Name.empty())
		::SHObjectProperties(m_hWnd, SHOP_FILEPATH, row->File.Name.c_str(), nullptr);
	return 0;
}

LRESULT CDiskView::OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && !row->File.Name.empty())
		::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", row->File.Name).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CDiskView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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

LRESULT CDiskView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CDiskView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CDiskView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	m_Tracker.reset();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CDiskView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CDiskView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CDiskView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.DiskColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CDiskView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CDiskView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CDiskView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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
