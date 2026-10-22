#include "pch.h"
#include "ObjectTypesView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include "HandleIcons.h"
#include <WinSys/AccessMask.h>
#include <ClipboardHelper.h>

namespace {
	// in CObjectTypesView::Column order; the keys are stored in the ObjectTypeColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Name", L"Name", 200, LVCFMT_LEFT, true, true },
		{ L"Index", L"Index", 55, LVCFMT_RIGHT, true },
		{ L"Objects", L"Objects", 90, LVCFMT_RIGHT, true },
		{ L"Handles", L"Handles", 90, LVCFMT_RIGHT, true },
		{ L"Peak Objects", L"PeakObjects", 95, LVCFMT_RIGHT, true },
		{ L"Peak Handles", L"PeakHandles", 95, LVCFMT_RIGHT, true },
		{ L"Pool Type", L"PoolType", 115, LVCFMT_LEFT, true },
		{ L"Default Paged Charge", L"PagedCharge", 150, LVCFMT_RIGHT, true },
		{ L"Default NP Charge", L"NonPagedCharge", 130, LVCFMT_RIGHT, true },
		{ L"Valid Access Mask", L"ValidAccess", 125, LVCFMT_RIGHT, true },
		{ L"Generic Read", L"GenericRead", 260, LVCFMT_LEFT, false },
		{ L"Generic Write", L"GenericWrite", 260, LVCFMT_LEFT, false },
		{ L"Generic Execute", L"GenericExecute", 260, LVCFMT_LEFT, false },
		{ L"Generic All", L"GenericAll", 260, LVCFMT_LEFT, false },
		{ L"Invalid Attributes", L"InvalidAttributes", 220, LVCFMT_LEFT, false },
		{ L"Security Required", L"SecurityRequired", 110, LVCFMT_LEFT, false },
		{ L"Maintain Handle Count", L"MaintainHandleCount", 135, LVCFMT_LEFT, false },
	};

	// the columns with a change mark (Row::Change), in that order
	int ChangeIndex(int column) {
		return column >= 2 && column <= 5 ? column - 2 : -1;
	}
}

CString CObjectTypesView::GetTitle() const {
	return L"Object Types";
}

LRESULT CObjectTypesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	// the frame's image list, for the types' icons
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().ObjectTypeColumns(), s_Columns));

	int filterWidth = 200;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Type name (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedIndex());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	Update();
	ShowRows(-1);
	return 0;
}

void CObjectTypesView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CObjectTypesView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CObjectTypesView::OnRefresh(bool active) {
	// one query: also while hidden, so the change marks are right once shown
	auto selected = GetSelectedIndex();
	Update();
	if (!active)
		return;
	ShowRows(selected);
	UpdateStatus();
}

void CObjectTypesView::Update() {
	auto types = WinSys::ObjectTypes::Enum();
	if (types.empty())
		return;	// keep what we have
	// the rows point into m_All, which is replaced
	m_Rows.clear();
	std::vector<Row> rows;
	rows.reserve(types.size());
	for (auto& type : types) {
		Row row{ std::move(type) };
		auto previous = std::ranges::find_if(m_All, [&](auto& r) { return r.Type.Index == row.Type.Index; });
		if (previous != m_All.end()) {
			auto sign = [](uint32_t now, uint32_t before) -> int8_t { return now > before ? 1 : now < before ? -1 : 0; };
			row.Change[0] = sign(row.Type.Objects, previous->Type.Objects);
			row.Change[1] = sign(row.Type.Handles, previous->Type.Handles);
			row.Change[2] = sign(row.Type.PeakObjects, previous->Type.PeakObjects);
			row.Change[3] = sign(row.Type.PeakHandles, previous->Type.PeakHandles);
		}
		rows.push_back(std::move(row));
	}
	m_All = std::move(rows);
}

std::wstring CObjectTypesView::AttributesText(uint32_t attributes) {
	static const struct {
		uint32_t Value;
		PCWSTR Name;
	} names[] = {
		{ OBJ_INHERIT, L"Inherit" }, { OBJ_PERMANENT, L"Permanent" }, { OBJ_EXCLUSIVE, L"Exclusive" },
		{ OBJ_CASE_INSENSITIVE, L"Case Insensitive" }, { OBJ_OPENIF, L"Open If" }, { OBJ_OPENLINK, L"Open Link" },
		{ OBJ_KERNEL_HANDLE, L"Kernel Handle" }, { OBJ_FORCE_ACCESS_CHECK, L"Force Access Check" },
		{ OBJ_IGNORE_IMPERSONATED_DEVICEMAP, L"Ignore Impersonated Device Map" }, { OBJ_DONT_REPARSE, L"Don't Reparse" },
	};
	std::wstring text;
	for (auto& n : names)
		if (attributes & n.Value)
			text += (text.empty() ? L"" : L", ") + std::wstring(n.Name);
	return text;
}

std::wstring CObjectTypesView::GetText(Row const& row, Column column) const {
	auto& type = row.Type;
	auto mapped = [&](uint32_t access) {
		auto decoded = WinSys::AccessMaskDecoder::Decode(type.Name, access);
		return decoded.empty() ? std::format(L"0x{:08X}", access) : std::format(L"0x{:08X} ({})", access, decoded);
	};
	switch (column) {
		case Column::Name: return type.Name;
		case Column::Index: return std::to_wstring(type.Index);
		case Column::Objects: return FormatHelper::FormatNumber(type.Objects);
		case Column::Handles: return FormatHelper::FormatNumber(type.Handles);
		case Column::PeakObjects: return FormatHelper::FormatNumber(type.PeakObjects);
		case Column::PeakHandles: return FormatHelper::FormatNumber(type.PeakHandles);
		case Column::PoolType: return WinSys::ObjectTypes::PoolTypeName(type.PoolType);
		case Column::PagedCharge: return std::to_wstring(type.DefaultPagedPoolCharge);
		case Column::NonPagedCharge: return std::to_wstring(type.DefaultNonPagedPoolCharge);
		case Column::ValidAccess: return std::format(L"0x{:08X}", type.ValidAccessMask);
		case Column::GenericRead: return mapped(type.GenericMapping.GenericRead);
		case Column::GenericWrite: return mapped(type.GenericMapping.GenericWrite);
		case Column::GenericExecute: return mapped(type.GenericMapping.GenericExecute);
		case Column::GenericAll: return mapped(type.GenericMapping.GenericAll);
		case Column::InvalidAttributes: {
			auto names = AttributesText(type.InvalidAttributes);
			return names.empty() ? std::format(L"0x{:X}", type.InvalidAttributes) : std::format(L"0x{:X} ({})", type.InvalidAttributes, names);
		}
		case Column::SecurityRequired: return type.SecurityRequired ? L"Yes" : L"No";
		case Column::MaintainHandleCount: return type.MaintainHandleCount ? L"Yes" : L"No";
	}
	return L"";
}

int CObjectTypesView::GetSelectedIndex() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected]->Type.Index : -1;
}

void CObjectTypesView::ShowRows(int selected) {
	m_Rows.clear();
	auto& filter = m_Filter.GetText();
	for (auto& row : m_All)
		if (filter.empty() || CFilterBox::Contains(row.Type.Name, filter))
			m_Rows.push_back(&row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectType(selected);
	m_List.Invalidate();
}

void CObjectTypesView::SelectType(int index) {
	// the list view keeps the selection by row, which sorting shifts
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (index < 0)
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Type.Index == index; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CObjectTypesView::UpdateStatus() {
	if (!m_Active)
		return;
	uint64_t objects = 0, handles = 0;
	for (auto& row : m_All) {
		objects += row.Type.Objects;
		handles += row.Type.Handles;
	}
	auto text = std::format(L"{} object types; {} objects, {} handles", m_All.size(), FormatHelper::FormatNumber((int64_t)objects),
		FormatHelper::FormatNumber((int64_t)handles));
	if (!m_Filter.GetText().empty())
		text += std::format(L"; {} match the filter", m_Rows.size());
	Frame()->SetStatusText(0, text.c_str());
}

CString CObjectTypesView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CObjectTypesView::GetRowImage(HWND, int row, int) const {
	// the Handles page's icon for the type
	return row < (int)m_Rows.size() ? Frame()->GetIconIndex(GetHandleIcon(m_Rows[row]->Type.Name, L"")) : -1;
}

int CObjectTypesView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by type (ShowRows, DoSort)
	return -1;
}

void CObjectTypesView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedIndex();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
		auto& t1 = r1->Type;
		auto& t2 = r2->Type;
		int result = 0;
		switch (column) {
			case Column::Index: result = compare(t1.Index, t2.Index); break;
			case Column::Objects: result = compare(t1.Objects, t2.Objects); break;
			case Column::Handles: result = compare(t1.Handles, t2.Handles); break;
			case Column::PeakObjects: result = compare(t1.PeakObjects, t2.PeakObjects); break;
			case Column::PeakHandles: result = compare(t1.PeakHandles, t2.PeakHandles); break;
			case Column::PoolType: result = compare(t1.PoolType, t2.PoolType); break;
			case Column::PagedCharge: result = compare(t1.DefaultPagedPoolCharge, t2.DefaultPagedPoolCharge); break;
			case Column::NonPagedCharge: result = compare(t1.DefaultNonPagedPoolCharge, t2.DefaultNonPagedPoolCharge); break;
			case Column::ValidAccess: result = compare(t1.ValidAccessMask, t2.ValidAccessMask); break;
			case Column::GenericRead: result = compare(t1.GenericMapping.GenericRead, t2.GenericMapping.GenericRead); break;
			case Column::GenericWrite: result = compare(t1.GenericMapping.GenericWrite, t2.GenericMapping.GenericWrite); break;
			case Column::GenericExecute: result = compare(t1.GenericMapping.GenericExecute, t2.GenericMapping.GenericExecute); break;
			case Column::GenericAll: result = compare(t1.GenericMapping.GenericAll, t2.GenericMapping.GenericAll); break;
			case Column::InvalidAttributes: result = compare(t1.InvalidAttributes, t2.InvalidAttributes); break;
			case Column::SecurityRequired: result = compare(t1.SecurityRequired, t2.SecurityRequired); break;
			case Column::MaintainHandleCount: result = compare(t1.MaintainHandleCount, t2.MaintainHandleCount); break;
			default: result = ::_wcsicmp(t1.Name.c_str(), t2.Name.c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectType(selected);
}

LRESULT CObjectTypesView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			// the row's own colors (not CLR_DEFAULT: the system's, black text on a dark selection), unless the count
			// changed since the previous refresh
			cd->clrTextBk = m_RowTextBk;
			cd->clrText = m_RowText;
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto change = ChangeIndex((int)GetColumnManager(m_List)->GetColumnTag<Column>(cd->iSubItem));
			if (change < 0 || m_Rows[index]->Change[change] == 0)
				break;
			// the Network tab's colors for new (up) and closed (down) rows
			bool dark = WTLHelper::IsDarkMode();
			bool up = m_Rows[index]->Change[change] > 0;
			cd->clrTextBk = up ? (dark ? RGB(0, 96, 0) : RGB(160, 255, 160)) : (dark ? RGB(128, 0, 0) : RGB(255, 160, 160));
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CObjectTypesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

LRESULT CObjectTypesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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

LRESULT CObjectTypesView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CObjectTypesView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CObjectTypesView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CObjectTypesView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CObjectTypesView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CObjectTypesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ObjectTypeColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CObjectTypesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedIndex());
	}
	return 0;
}

LRESULT CObjectTypesView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CObjectTypesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	ShowRows(GetSelectedIndex());
	return 0;
}
