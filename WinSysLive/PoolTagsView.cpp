#include "pch.h"
#include "PoolTagsView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/SystemInfo.h>
#include <ClipboardHelper.h>

namespace {
	// in CPoolTagsView::Column order; the keys are stored in the PoolTagColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Tag", L"Tag", 60, LVCFMT_LEFT, true, true },
		{ L"Value", L"Value", 90, LVCFMT_RIGHT, false },
		{ L"Paged Allocs", L"PagedAllocs", 100, LVCFMT_RIGHT, true },
		{ L"Paged Frees", L"PagedFrees", 100, LVCFMT_RIGHT, true },
		{ L"Paged Diff", L"PagedDiff", 85, LVCFMT_RIGHT, true },
		{ L"Paged Usage", L"PagedUsage", 100, LVCFMT_RIGHT, true },
		{ L"Non-Paged Allocs", L"NonPagedAllocs", 115, LVCFMT_RIGHT, true },
		{ L"Non-Paged Frees", L"NonPagedFrees", 115, LVCFMT_RIGHT, true },
		{ L"Non-Paged Diff", L"NonPagedDiff", 100, LVCFMT_RIGHT, true },
		{ L"Non-Paged Usage", L"NonPagedUsage", 115, LVCFMT_RIGHT, true },
		{ L"Total Usage", L"TotalUsage", 100, LVCFMT_RIGHT, false },
		{ L"Binary", L"Binary", 160, LVCFMT_LEFT, true },
		{ L"Description", L"Description", 380, LVCFMT_LEFT, true },
	};
}

CString CPoolTagsView::GetTitle() const {
	return L"Pool Tags";
}

LRESULT CPoolTagsView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().PoolTagColumns(), s_Columns));

	int filterWidth = 280;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Tag, binary or description (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedTag());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	LoadDescriptions();
	Update(false);
	ShowRows(std::nullopt);
	return 0;
}

void CPoolTagsView::LoadDescriptions() {
	// the Debugging Tools' copy is the newer one
	if (auto path = WinSys::PoolTagDescriptions::FindFile(); !path.empty() && m_Descriptions.Load(path)) {
		m_DescriptionsSource = path;
		return;
	}
	auto module = _AtlBaseModule.GetResourceInstance();
	if (auto res = ::FindResource(module, MAKEINTRESOURCE(IDR_POOLTAGS), RT_RCDATA)) {
		auto data = ::LockResource(::LoadResource(module, res));
		if (data) {
			m_Descriptions.Parse(std::string_view((char const*)data, ::SizeofResource(module, res)));
			m_DescriptionsSource = L"built-in";
		}
	}
}

void CPoolTagsView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		// not read while hidden: no marks for the time since
		auto selected = GetSelectedTag();
		Update(false);
		ShowRows(selected);
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CPoolTagsView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CPoolTagsView::OnRefresh(bool active) {
	if (!active)
		return;
	auto selected = GetSelectedTag();
	Update(true);
	ShowRows(selected);
	UpdateStatus();
}

int64_t CPoolTagsView::GetCounter(SYSTEM_POOLTAG const& tag, Column column) {
	switch (column) {
		case Column::PagedAllocs: return tag.PagedAllocs;
		case Column::PagedFrees: return tag.PagedFrees;
		case Column::PagedDiff: return (int64_t)tag.PagedAllocs - tag.PagedFrees;
		case Column::PagedUsage: return (int64_t)tag.PagedUsed;
		case Column::NonPagedAllocs: return tag.NonPagedAllocs;
		case Column::NonPagedFrees: return tag.NonPagedFrees;
		case Column::NonPagedDiff: return (int64_t)tag.NonPagedAllocs - tag.NonPagedFrees;
		case Column::NonPagedUsage: return (int64_t)tag.NonPagedUsed;
		case Column::TotalUsage: return (int64_t)(tag.PagedUsed + tag.NonPagedUsed);
	}
	return 0;
}

void CPoolTagsView::Update(bool marks) {
	auto tags = WinSys::SystemInfo::GetPoolTagInformation();
	if (tags.empty())
		return;	// keep what we have
	// non-elevated, Windows gives a single placeholder: tag "None", every counter zero
	auto& first = tags[0];
	m_Withheld = tags.size() == 1 && first.TagUlong == 'enoN' && first.PagedAllocs == 0 && first.PagedFrees == 0 && first.PagedUsed == 0
		&& first.NonPagedAllocs == 0 && first.NonPagedFrees == 0 && first.NonPagedUsed == 0;
	if (m_Withheld) {
		m_Rows.clear();
		m_All.clear();
		return;
	}
	std::unordered_map<ULONG, SYSTEM_POOLTAG const*> previous;
	for (auto& row : m_All)
		previous[row.Tag.TagUlong] = &row.Tag;
	// the rows point into m_All, which is replaced
	m_Rows.clear();
	std::vector<Row> rows;
	rows.reserve(tags.size());
	for (auto& tag : tags) {
		Row row{ tag, m_Descriptions.Find(tag.TagUlong) };
		if (auto it = previous.find(tag.TagUlong); marks && it != previous.end())
			for (int i = 0; i < CounterCount; i++) {
				auto now = GetCounter(tag, (Column)(FirstCounter + i)), before = GetCounter(*it->second, (Column)(FirstCounter + i));
				row.Change[i] = now > before ? 1 : now < before ? -1 : 0;
			}
		rows.push_back(row);
	}
	m_All = std::move(rows);
}

std::wstring CPoolTagsView::GetText(Row const& row, Column column) const {
	auto& tag = row.Tag;
	switch (column) {
		case Column::Tag: return WinSys::PoolTagDescriptions::TagText(tag.TagUlong);
		case Column::Value: return std::format(L"0x{:08X}", tag.TagUlong);
		case Column::PagedUsage: case Column::NonPagedUsage: case Column::TotalUsage:
			return FormatHelper::FormatNumber(GetCounter(tag, column));
		case Column::Binary: return row.Description ? row.Description->Binary : L"";
		case Column::Description: return row.Description ? row.Description->Description : L"";
	}
	if ((int)column >= FirstCounter && (int)column < FirstCounter + CounterCount)
		return FormatHelper::FormatNumber(GetCounter(tag, column));
	return L"";
}

bool CPoolTagsView::Matches(Row const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	return CFilterBox::Contains(WinSys::PoolTagDescriptions::TagText(row.Tag.TagUlong), filter) ||
		(row.Description && (CFilterBox::Contains(row.Description->Binary, filter) || CFilterBox::Contains(row.Description->Description, filter)));
}

std::optional<ULONG> CPoolTagsView::GetSelectedTag() const {
	int selected = m_List.GetSelectedIndex();
	if (selected < 0 || selected >= (int)m_Rows.size())
		return std::nullopt;
	return m_Rows[selected]->Tag.TagUlong;
}

void CPoolTagsView::ShowRows(std::optional<ULONG> selected) {
	m_Rows.clear();
	for (auto& row : m_All)
		if (Matches(row))
			m_Rows.push_back(&row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectTag(selected);
	m_List.Invalidate();
}

void CPoolTagsView::SelectTag(std::optional<ULONG> tag) {
	// the list view keeps the selection by row, which a new query and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (!tag)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Tag.TagUlong == *tag; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CPoolTagsView::UpdateStatus() {
	if (!m_Active)
		return;
	if (m_Withheld) {
		Frame()->SetStatusText(0, L"Pool tags need elevation: Windows doesn't give them to non-elevated processes");
		return;
	}
	uint64_t paged = 0, nonPaged = 0;
	for (auto& row : m_All) {
		paged += row.Tag.PagedUsed;
		nonPaged += row.Tag.NonPagedUsed;
	}
	auto text = std::format(L"{} tags; paged {}, non-paged {} in use", m_All.size(), FormatHelper::FormatSize((int64_t)paged),
		FormatHelper::FormatSize((int64_t)nonPaged));
	if (!m_Filter.GetText().empty())
		text += std::format(L"; {} match the filter", m_Rows.size());
	text += m_DescriptionsSource.empty() ? L"; no tag descriptions" : std::format(L"; descriptions: {}", m_DescriptionsSource);
	Frame()->SetStatusText(0, text.c_str());
}

CString CPoolTagsView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CPoolTagsView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by tag (ShowRows, DoSort)
	return -1;
}

void CPoolTagsView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedTag();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
		int result;
		if ((int)column >= FirstCounter && (int)column < FirstCounter + CounterCount)
			result = compare(GetCounter(r1->Tag, column), GetCounter(r2->Tag, column));
		else if (column == Column::Value)
			result = compare(r1->Tag.TagUlong, r2->Tag.TagUlong);
		else
			result = ::_wcsicmp(GetText(*r1, column).c_str(), GetText(*r2, column).c_str());
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectTag(selected);
}

LRESULT CPoolTagsView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_List) {
		bHandled = FALSE;
		return 0;
	}
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
			// the row's colors as the list chose them (selected, dark...): a cell set for a change goes back to them
			m_RowText = cd->clrText;
			m_RowTextBk = cd->clrTextBk;
			return CDRF_NOTIFYSUBITEMDRAW;

		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		{
			// the row's own colors (not CLR_DEFAULT: the system's, black text on a dark selection), unless the counter
			// changed since the previous refresh
			cd->clrTextBk = m_RowTextBk;
			cd->clrText = m_RowText;
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			int counter = (int)GetColumnManager(m_List)->GetColumnTag<Column>(cd->iSubItem) - FirstCounter;
			if (counter < 0 || counter >= CounterCount || m_Rows[index]->Change[counter] == 0)
				break;
			// the Object Types tab's colors
			bool dark = WTLHelper::IsDarkMode();
			bool up = m_Rows[index]->Change[counter] > 0;
			cd->clrTextBk = up ? (dark ? RGB(0, 96, 0) : RGB(160, 255, 160)) : (dark ? RGB(128, 0, 0) : RGB(255, 160, 160));
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CPoolTagsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

LRESULT CPoolTagsView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_List.GetSelectedIndex();
	if (selected < 0 || selected >= (int)m_Rows.size())
		return 0;
	// the shown columns in display order, tab separated
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetText(*m_Rows[selected], (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CPoolTagsView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CPoolTagsView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CPoolTagsView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CPoolTagsView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CPoolTagsView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CPoolTagsView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.PoolTagColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CPoolTagsView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedTag());
	}
	return 0;
}

LRESULT CPoolTagsView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CPoolTagsView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	auto selected = GetSelectedTag();
	SetColumns(columns);
	SaveColumns();
	ShowRows(selected);
	return 0;
}
