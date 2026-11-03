#include "pch.h"
#include "RegistryView.h"
#include "Recorders.h"
#include "Highlight.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include "RegistryEditor.h"

using WinSys::RegistryIoParser;

namespace {
	// in CRegistryView::Column order; the keys are stored in the RegistryColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Process", L"Process", 170, LVCFMT_LEFT, true, true },
		{ L"PID", L"Pid", 65, LVCFMT_RIGHT, true },
		{ L"Key", L"Key", 420, LVCFMT_LEFT, true },
		{ L"Operations/s", L"Rate", 90, LVCFMT_RIGHT, true },
		{ L"Opens", L"Opens", 70, LVCFMT_RIGHT, true },
		{ L"Reads", L"Reads", 70, LVCFMT_RIGHT, true },
		{ L"Writes", L"Writes", 70, LVCFMT_RIGHT, true },
		{ L"Failures", L"Failures", 70, LVCFMT_RIGHT, true },
		{ L"Last Operation", L"LastOperation", 110, LVCFMT_LEFT, true },
		{ L"Last Value", L"LastValue", 140, LVCFMT_LEFT, true },
		{ L"Last Result", L"Result", 120, LVCFMT_LEFT, true },
		{ L"First Operation", L"FirstSeen", 150, LVCFMT_LEFT, false },
		{ L"Last Seen", L"LastSeen", 150, LVCFMT_LEFT, true },
	};

	std::wstring FormatRate(double rate) {
		// below one a second over the last refreshes is nothing to show
		return rate >= 0.05 ? std::format(L"{:.1f}", rate) : L"";
	}

	std::wstring FormatCount(uint64_t count) {
		return count ? FormatHelper::FormatNumber((int64_t)count) : L"";
	}
}

CString CRegistryView::GetTitle() const {
	return L"Registry";
}

LRESULT CRegistryView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	// the frame's image list, for the processes' icons
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().RegistryColumns(), s_Columns));

	int filterWidth = 280;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Process, PID, key or value (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

void CRegistryView::OnActivate(bool active) {
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

void CRegistryView::StartTracker() {
	if (m_Tracker || m_TrackerError)
		return;
	if (!(m_Tracker = Recorders::AcquireRegistryIo()))
		m_TrackerError = ::GetLastError();
}

void CRegistryView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CRegistryView::OnRefresh(bool active) {
	// the frame made the tracker forget the exited processes (Recorders::OnRefresh): their rows go
	if (!m_Tracker)
		return;

	LARGE_INTEGER now, frequency;
	::QueryPerformanceCounter(&now);
	::QueryPerformanceFrequency(&frequency);

	// the rows point into m_All, which the merge replaces: the selection is taken first, and the rows are rebuilt
	// (while hidden, when the view is shown)
	auto selected = m_RowsPending ? m_PendingSelection : GetSelectedKey();
	m_Rows.clear();
	Merge(m_Tracker->GetKeys(), now.QuadPart, frequency.QuadPart);
	m_Loaded = true;
	if (!active) {
		m_RowsPending = true;
		m_PendingSelection = selected;
		return;
	}
	ShowRows(selected);
	UpdateStatus();
}

void CRegistryView::Merge(std::vector<WinSys::RegistryIoCounters> keys, int64_t now, int64_t frequency) {
	std::map<Key, Row*> previous;
	for (auto& row : m_All)
		previous[row.GetKey()] = &row;

	std::vector<Row> merged;
	merged.reserve(keys.size());
	for (auto& key : keys) {
		Row row{ std::move(key) };
		if (auto it = previous.find(row.GetKey()); it != previous.end()) {
			auto& before = *it->second;
			row.NewTicks = (std::max)(before.NewTicks - 1, 0);
			row.History = std::move(before.History);
			previous.erase(it);
		}
		else if (m_Loaded)
			// on the first read, they're all new to the view, not to the system
			row.NewTicks = Highlight::NewTicks();

		row.History.push_back({ now, row.Counters.Operations() });
		while (row.History.size() > RateUpdates + 1)
			row.History.pop_front();
		auto& first = row.History.front();
		auto& last = row.History.back();
		if (last.Time > first.Time)
			row.Rate = (last.Operations - first.Operations) / (double(last.Time - first.Time) / frequency);
		merged.push_back(std::move(row));
	}
	// the rows the tracker forgot, shown for a while (those forgotten before, while they're still shown)
	auto ticks = Highlight::ExitedTicks();
	for (auto& [key, row] : previous)
		if (row->Gone) {
			if (row->GoneTicks > 1) {
				Row gone{ std::move(row->Counters) };
				gone.Gone = true;
				gone.GoneTicks = row->GoneTicks - 1;
				merged.push_back(std::move(gone));
			}
		}
		else if (ticks > 0) {
			Row gone{ std::move(row->Counters) };
			gone.Gone = true;
			gone.GoneTicks = ticks;
			merged.push_back(std::move(gone));
		}
	// the tracker's order is a hash table's: until a header is clicked, the rows stay by process and key
	std::ranges::sort(merged, [](auto const& r1, auto const& r2) { return r1.GetKey() < r2.GetKey(); });
	m_All = std::move(merged);
}

std::wstring CRegistryView::GetProcessName(uint32_t pid) const {
	if (pid == 0)
		return L"Idle";
	auto name = Frame()->GetProcessManager().GetProcessNameById(pid);
	return name.empty() ? L"" : name;
}

std::wstring CRegistryView::GetKeyText(WinSys::RegistryIoCounters const& counters) {
	return counters.Key.empty() ? RegistryIoParser::UnnamedName(counters.Object, counters.Unnamed) : counters.Key;
}

std::wstring CRegistryView::GetEditorPath(std::wstring const& key) {
	// the Registry Editor's roots for the tracker's abbreviations (HandleNames::RegistryName)
	static const std::pair<PCWSTR, PCWSTR> roots[] = {
		{ L"HKLM", L"HKEY_LOCAL_MACHINE" }, { L"HKCU", L"HKEY_CURRENT_USER" }, { L"HKU", L"HKEY_USERS" },
	};
	for (auto& [abbreviation, root] : roots) {
		auto len = wcslen(abbreviation);
		if (key.size() >= len && ::_wcsnicmp(key.c_str(), abbreviation, len) == 0 && (key.size() == len || key[len] == L'\\'))
			return L"Computer\\" + std::wstring(root) + key.substr(len);
	}
	return L"";
}

std::wstring CRegistryView::GetText(Row const& row, Column column) const {
	auto& c = row.Counters;
	switch (column) {
		case Column::Process: return GetProcessName(c.Pid);
		case Column::Pid: return std::to_wstring(c.Pid);
		case Column::Key: return GetKeyText(c);
		case Column::Rate: return FormatRate(row.Rate);
		case Column::Opens: return FormatCount(c.Opens);
		case Column::Reads: return FormatCount(c.Reads);
		case Column::Writes: return FormatCount(c.Writes);
		case Column::Failures: return FormatCount(c.Failures);
		case Column::LastOperation: return RegistryIoParser::OperationName(c.LastOperation);
		case Column::LastValue: return c.LastValue;
		case Column::Result: return RegistryIoParser::ResultName(c.LastStatus);
		case Column::FirstSeen: return FormatHelper::FormatDateTime(c.FirstSeen);
		case Column::LastSeen: return FormatHelper::FormatDateTime(c.LastSeen);
	}
	return L"";
}

bool CRegistryView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Process, Column::Pid, Column::Key, Column::LastValue })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

std::optional<CRegistryView::Key> CRegistryView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? std::optional(row->GetKey()) : std::nullopt;
}

CRegistryView::Row const* CRegistryView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CRegistryView::ShowRows(std::optional<Key> selected) {
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

void CRegistryView::SelectRow(std::optional<Key> const& key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (!key)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->GetKey() == *key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CRegistryView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (!m_Tracker)
		text = m_TrackerError == ERROR_ACCESS_DENIED
			? L"Registry monitoring needs administrator rights (File > Run as Administrator)"
			: std::format(L"Starting registry monitoring failed: {}", WinSys::Helpers::GetErrorText(m_TrackerError));
	else {
		double rate = 0;
		for (auto& row : m_All)
			rate += row.Rate;
		text = std::format(L"{} keys with operations in the last minute", std::ranges::count_if(m_All, [](auto& r) { return !r.Gone; }));
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
		text += std::format(L"; {:.0f} operations/s; since {}", rate, FormatHelper::FormatDateTime(m_Tracker->GetStartTime()));
		if (auto lost = m_Tracker->GetLostEvents())
			text += std::format(L"; {} events lost", lost);
	}
	Frame()->SetStatusText(0, text.c_str());
}

CString CRegistryView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CRegistryView::GetRowImage(HWND, int row, int col) const {
	// the process's icon, wherever that column was dragged
	if (row >= (int)m_Rows.size() || GetColumnManager(m_List)->GetColumnTag<Column>(col) != Column::Process)
		return -1;
	auto process = Frame()->GetProcessManager().GetProcessById(m_Rows[row]->Counters.Pid);
	return process ? Frame()->GetProcessIcon(*process) : -1;
}

int CRegistryView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by row key (ShowRows, DoSort)
	return -1;
}

void CRegistryView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	switch (column) {
		case Column::Process: case Column::Key: case Column::LastOperation: case Column::LastValue: case Column::Result: {
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
				auto& c1 = r1->Counters;
				auto& c2 = r2->Counters;
				int result = 0;
				switch (column) {
					case Column::Pid: result = compare(c1.Pid, c2.Pid); break;
					case Column::Rate: result = compare(r1->Rate, r2->Rate); break;
					case Column::Opens: result = compare(c1.Opens, c2.Opens); break;
					case Column::Reads: result = compare(c1.Reads, c2.Reads); break;
					case Column::Writes: result = compare(c1.Writes, c2.Writes); break;
					case Column::Failures: result = compare(c1.Failures, c2.Failures); break;
					case Column::FirstSeen: result = compare(c1.FirstSeen, c2.FirstSeen); break;
					case Column::LastSeen: result = compare(c1.LastSeen, c2.LastSeen); break;
				}
				return si->SortAscending ? result < 0 : result > 0;
				});
			break;
	}
	SelectRow(selected);
}

LRESULT CRegistryView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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

bool CRegistryView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	bool editor = !GetEditorPath(m_Rows[row]->Counters.Key).empty();
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_REGISTRY_GOTOPROCESS, L"Go to &Process");
	menu.AppendMenu(MF_STRING | (editor ? 0 : MF_GRAYED), ID_REGISTRY_OPENKEY, L"Open in &Registry Editor");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, ID_REGISTRY_COPYKEY, L"Copy &Key");
	menu.SetMenuDefaultItem(ID_REGISTRY_GOTOPROCESS);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CRegistryView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	Frame()->GoToProcess(m_Rows[row]->Counters.Pid);
	return true;
}

LRESULT CRegistryView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		Frame()->GoToProcess(row->Counters.Pid);
	return 0;
}

LRESULT CRegistryView::OnOpenKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row)
		return 0;
	auto path = GetEditorPath(row->Counters.Key);
	if (path.empty())
		return 0;
	OpenRegistryEditor(m_hWnd, path);
	return 0;
}

LRESULT CRegistryView::OnCopyKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		ClipboardHelper::CopyText(m_hWnd, GetKeyText(row->Counters).c_str());
	return 0;
}

LRESULT CRegistryView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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

LRESULT CRegistryView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CRegistryView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CRegistryView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	m_Tracker.reset();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CRegistryView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CRegistryView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CRegistryView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.RegistryColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CRegistryView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CRegistryView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CRegistryView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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

LRESULT CRegistryView::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the table's defaults (Parse of no layout), in their order and widths
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	ShowRows(GetSelectedKey());
	return 0;
}
