#include "pch.h"
#include <DpiHelper.h>
#include "ProcessInfoEx.h"
#include "Interfaces.h"
#include "HandlesPage.h"
#include <WinSys/AccessMask.h>
#include "AppSettings.h"
#include "ViewBase.h"
#include "ListExport.h"
#include <ClipboardHelper.h>
#include "HandleIcons.h"
#include "HandleSheet.h"
#include "Highlight.h"
#include <WinSys/Helpers.h>
#include <WinSys/Sid.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ToolbarHelper.h>

using namespace WinSys;

namespace {
	// in CHandlesPage::Column order; the keys are stored in the HandleColumns setting
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Handle", L"Handle", 80, LVCFMT_RIGHT, true, true },
		{ L"Type", L"Type", 120, LVCFMT_LEFT, true },
		{ L"Name / Details", L"Name", 400, LVCFMT_LEFT, true },
		{ L"Address", L"Address", 140, LVCFMT_RIGHT, false },	// of the kernel object; the system handle table: costly
		{ L"Access", L"Access", 90, LVCFMT_RIGHT, true },
		{ L"Decoded Access", L"DecodedAccess", 300, LVCFMT_LEFT, true },
		{ L"Attributes", L"Attributes", 110, LVCFMT_LEFT, true },
	};

	// when this many name queries have hung (their workers are stuck), files aren't queried anymore
	const int MaxHungQueries = 4;

	// the toolbar right of the filter box
	const ToolBarButtonInfo s_BarButtons[] = {
		{ ID_HANDLE_BYTYPE, IDI_OBJECT, BTNS_CHECK | BTNS_AUTOSIZE, L"By Type" },
		{ ID_VIEW_SHOWUNNAMEDHANDLES, IDI_HANDLES, BTNS_CHECK | BTNS_AUTOSIZE, L"Unnamed Handles" },
	};
	const int BarImageSize = 16;	// at 96 DPI
}

LRESULT CHandlesPage::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	// the type icons, in the leftmost column
	ResetImageList();
	SuspendHookWhilePainting(m_List);
	// the options, right of the filter: check buttons with an icon and text, sending the context menu's commands
	m_Bar = ToolbarHelper::CreateAndInitToolBar(m_hWnd, s_BarButtons, _countof(s_BarButtons), BarImageSize);
	SetBarFont();
	m_Filter.Create(m_hWnd, IDC_PAGE_FILTER, 200, L"Filter (Ctrl+F)", m_List, [this](std::wstring const&) {
		if (m_ByType) {
			ShowTypes();
			return;
		}
		int selected = m_List.GetSelectedIndex();
		ShowRows(selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : static_cast<uint64_t>(0));
		});
	// By Type's summary, hidden until it's chosen
	m_Types.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS
		| LVS_SHAREIMAGELISTS, 0, TypesId);
	m_Types.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
	{
		static const struct {
			PCWSTR Name;
			int Width, Format;
		} columns[] = {
			{ L"Type", 180, LVCFMT_LEFT }, { L"Handles", 80, LVCFMT_RIGHT }, { L"Change", 80, LVCFMT_RIGHT },
			{ L"Since Baseline", 100, LVCFMT_RIGHT }, { L"Peak", 80, LVCFMT_RIGHT },
		};
		static_assert(_countof(columns) == (int)TypeColumn::_Count);
		auto dpi = DpiHelper::GetDpi(m_hWnd);
		for (int i = 0; i < _countof(columns); i++)
			m_Types.InsertColumn(i, columns[i].Name, columns[i].Format, DpiHelper::Scale(columns[i].Width, USER_DEFAULT_SCREEN_DPI, dpi));
		ListViewHelper::EnableColumnDpiScaling(m_Types);
	}
	ResetImageList();
	SetColumns(ColumnLayout::Parse(m_Columns ? *m_Columns : AppSettings::Get().HandleColumns(), s_Columns));
	if (!m_OwnShowUnnamed)
		m_ShowUnnamed = AppSettings::Get().ShowUnnamedHandles();
	UpdateBar();
	return 0;
}

void CHandlesPage::SetBarFont() {
	m_BarFont.Apply(m_Bar);
}

void CHandlesPage::UpdateBar() {
	if (!m_Bar)
		return;
	m_Bar.CheckButton(ID_HANDLE_BYTYPE, m_ByType);
	m_Bar.CheckButton(ID_VIEW_SHOWUNNAMEDHANDLES, m_ShowUnnamed);
}

LRESULT CHandlesPage::OnDpiChangedAfterParent(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the filter box follows by itself (and its parent's layout through a WM_SIZE); the bar's icons and font are ours
	ToolbarHelper::UpdateToolBarImages(m_Bar, s_BarButtons, _countof(s_BarButtons), BarImageSize);
	SetBarFont();
	Layout();
	bHandled = FALSE;
	return 0;
}

LRESULT CHandlesPage::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

LRESULT CHandlesPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	Layout();
	return 0;
}

void CHandlesPage::Layout() {
	if (!m_List)
		return;
	CRect client;
	GetClientRect(&client);
	int cx = client.Width(), cy = client.Height();
	// the filter box at its width and height, the bar right of it, centered on the row; the list takes the rest
	CWindow filter(m_Filter.GetHwnd());
	CRect rc;
	filter.GetWindowRect(&rc);
	int filterWidth = (std::min)(m_Filter.GetWidth(), cx);
	SIZE bar{};
	if (m_Bar)
		m_Bar.GetMaxSize(&bar);
	int row = (std::max)(rc.Height(), (int)bar.cy);
	m_RowHeight = row;
	filter.MoveWindow(0, (row - rc.Height()) / 2, filterWidth, rc.Height());
	if (m_Bar) {
		// to the right edge, so its background covers the row; the gap from the filter as its indent
		m_Bar.SetIndent(DpiHelper::Scale(4, USER_DEFAULT_SCREEN_DPI, DpiHelper::GetDpi(m_hWnd)));
		m_Bar.MoveWindow(filterWidth, 0, (std::max)(0, cx - filterWidth), row);
	}
	m_List.MoveWindow(0, row, cx, (std::max)(cy - row, 0));
	if (m_Types)
		m_Types.MoveWindow(0, row, cx, (std::max)(cy - row, 0));
}

LRESULT CHandlesPage::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	(m_ByType ? m_Types : m_List).SetFocus();
	return 0;
}

LRESULT CHandlesPage::OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the list covers the page below the filter's row; the row's gaps (around a shorter filter box) get the window's
	// color, which WTLHelper's dark mode makes dark
	CRect rc;
	GetClientRect(&rc);
	rc.bottom = m_RowHeight;
	::FillRect((HDC)wParam, &rc, ::GetSysColorBrush(COLOR_WINDOW));
	return 1;
}

void CHandlesPage::SetProcess(std::shared_ptr<ProcessInfoEx> process, bool enumerate) {
	if (process == m_Process)
		return;
	m_Process = std::move(process);
	Attach(m_Process ? m_Process->Id : 0, m_Process ? m_Process->GetImageName() : L"", enumerate);
}

void CHandlesPage::SetProcess(uint32_t pid, std::wstring imageName) {
	m_Process.reset();
	Attach(pid, std::move(imageName), false);
}

void CHandlesPage::Attach(uint32_t pid, std::wstring imageName, bool enumerate) {
	m_Pid = pid;
	m_ImageName = std::move(imageName);
	// the Idle process has no handles
	m_hProcess.reset(pid ? ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid) : nullptr);
	m_Handles.clear();
	m_Closed.clear();
	m_FirstEnum = true;
	// By Type's baseline is the new process's first count
	m_TypeBaseline.clear();
	m_HasBaseline = false;
	m_TypePrevious.clear();
	m_TypePeak.clear();
	m_TypeRows.clear();
	m_OnlyType.clear();
	if (m_Types)
		m_Types.DeleteAllItems();
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (enumerate)
		Refresh();
	else {
		m_Rows.clear();
		m_AllRows.clear();
		m_List.SetItemCount(0);
	}
	if (m_List.GetItemCount() > 0)
		m_List.EnsureVisible(0, FALSE);
}

bool CHandlesPage::ShowsAddresses() const {
	return std::ranges::contains(GetShownColumns(), (int)Column::Address, &ColumnLayout::ShownColumn::Id);
}

void CHandlesPage::Refresh() {
	int selected = m_List.GetSelectedIndex();
	uint64_t selectedValue = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : 0;

	m_Rows.clear();
	if (m_Pid) {
		auto handles = ShowsAddresses() ? m_Enumerator.EnumProcessHandlesWithAddresses(m_Pid)
			: HandleEnumerator::EnumProcessHandles(m_Pid);
		if (handles) {
			decltype(m_Handles) current;
			current.reserve(handles->size());
			m_Rows.reserve(handles->size());
			for (auto& h : *handles) {
				// the same handle as before, unless the value was reused (for another object, or with other access)
				std::shared_ptr<HandleData> data;
				if (auto it = m_Handles.find(h.Value); it != m_Handles.end() && it->second->Entry.TypeIndex == h.TypeIndex
					&& it->second->Entry.Access == h.Access && (!h.Object || !it->second->Entry.Object || h.Object == it->second->Entry.Object)) {
					data = std::move(it->second);
					m_Handles.erase(it);
					data->Entry.Attributes = h.Attributes;
					if (h.Object)
						data->Entry.Object = h.Object;
				}
				else {
					data = std::make_shared<HandleData>(h);
					if (!m_FirstEnum)
						data->NewTicks = Highlight::NewTicks();
				}
				HandleRow row{ data };
				if (data->NewTicks > 0) {
					row.New = true;
					data->NewTicks--;
				}
				current.insert({ h.Value, data });
				m_Rows.push_back(std::move(row));
			}
			// the handles left were closed: shown for a while
			for (auto& [value, data] : m_Handles)
				m_Closed.push_back({ data, Highlight::ExitedTicks() });
			std::erase_if(m_Closed, [](auto const& c) { return c.second <= 0; });
			for (auto& [data, ticks] : m_Closed) {
				m_Rows.push_back({ data, false, true });
				ticks--;
			}
			m_Handles = std::move(current);
			m_FirstEnum = false;
		}
	}

	m_AllRows = std::move(m_Rows);
	CountTypes();
	ShowRows(selectedValue);
	if (m_ByType)
		ShowTypes();
}

CHandlesPage::HandleRow const* CHandlesPage::GetSelectedRow() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() && !m_Rows[selected].Closed ? &m_Rows[selected] : nullptr;
}

bool CHandlesPage::CanCloseSelected() const {
	// closing the app's own handles could break it
	return m_hProcess && m_Pid != ::GetCurrentProcessId() && GetSelectedRow();
}

void CHandlesPage::CloseSelected() {
	auto row = GetSelectedRow();
	if (!row || !CanCloseSelected())
		return;
	auto& entry = row->Data->Entry;
	auto type = ObjectTypes::GetName(entry.TypeIndex);
	auto name = GetName(*row->Data, false);
	auto question = std::format(L"Close handle 0x{:X} ({}{}{}) in {} ({})?\n\nThe process may fail when it uses the handle.",
		entry.Value, type, name.empty() ? L"" : L": ", name, m_ImageName, m_Pid);
	if (AtlMessageBox(m_hWnd, question.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return;

	// the handle value may have been closed and reused since the last refresh: checked against the type
	if (!HandleNames::CloseInProcess(m_hProcess.get(), entry.Value, type)) {
		auto error = ::GetLastError();
		if (error == ERROR_INVALID_HANDLE) {
			AtlMessageBox(m_hWnd, L"The handle isn't there anymore (or is another object's now).", IDR_MAINFRAME, MB_ICONWARNING);
			Refresh();
		}
		else
			AtlMessageBox(m_hWnd, std::format(L"Failed to close the handle: {}", WinSys::Helpers::GetErrorText(error)).c_str(),
				IDR_MAINFRAME, MB_ICONERROR);
		return;
	}
	// shown as closed
	Refresh();
}

bool CHandlesPage::CanShowProperties() const {
	return m_hProcess && GetSelectedRow();
}

void CHandlesPage::ShowProperties() {
	auto row = GetSelectedRow();
	if (!row || !CanShowProperties())
		return;
	// the row may go with a refresh while the dialog runs: copies
	auto data = row->Data;
	auto name = GetName(*data, false);
	CHandleSheet sheet(m_hProcess.get(), m_Pid, m_ImageName, data->Entry, name,
		[this](DWORD pid) { return m_ProcessNames ? m_ProcessNames(pid) : std::wstring(); });
	sheet.DoModal(m_hWnd);
}

LRESULT CHandlesPage::OnDoubleClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_List) {
		bHandled = FALSE;
		return 0;
	}
	// posted, so the dialog doesn't run inside the list's mouse handling
	if (CanShowProperties())
		PostMessage(WM_COMMAND, ID_HANDLE_PROPERTIES);
	return 0;
}

void CHandlesPage::SetShowUnnamed(bool show) {
	if (show == m_ShowUnnamed)
		return;
	m_ShowUnnamed = show;
	if (!m_OwnShowUnnamed) {
		auto& settings = AppSettings::Get();
		settings.ShowUnnamedHandles(show);
		settings.Save();
	}
	UpdateBar();
	int selected = m_List.GetSelectedIndex();
	ShowRows(selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : static_cast<uint64_t>(0));
}

LRESULT CHandlesPage::OnToggleUnnamed(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SetShowUnnamed(!m_ShowUnnamed);
	return 0;
}

LRESULT CHandlesPage::OnHandleCommand(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == ID_HANDLE_PROPERTIES)
		ShowProperties();
	else
		CloseSelected();
	return 0;
}

void CHandlesPage::SelectHandle(uint64_t value) {
	// the list view keeps the selection by index, which the new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (value == 0)
		return;
	auto it = std::ranges::find_if(m_Rows, [=](auto& r) { return r.Data->Entry.Value == value && !r.Closed; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

std::wstring const& CHandlesPage::GetName(HandleData& data, bool closed) const {
	if (!data.Name)
		data.Name = closed ? L"" : QueryName(data.Entry);
	return *data.Name;
}

std::wstring CHandlesPage::QueryName(HandleEntry const& entry) const {
	// when name queries have hung (their workers are stuck), files aren't queried anymore
	return HandleNames::QueryName(m_hProcess.get(), entry, m_NameQuery, m_DosNames,
		[this](DWORD pid) { return m_ProcessNames ? m_ProcessNames(pid) : std::wstring(); },
		m_NameQuery.GetAbandonedCount() < MaxHungQueries);
}

CString CHandlesPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

std::wstring CHandlesPage::GetText(HandleRow const& row, Column column) const {
	auto& h = row.Data->Entry;
	switch (column) {
		case Column::Handle: return std::format(L"0x{:X}", h.Value);
		case Column::Type: return ObjectTypes::GetName(h.TypeIndex);
		case Column::Name: return GetName(*row.Data, row.Closed);
		case Column::Address: return h.Object ? std::format(L"0x{:X}", (ULONG_PTR)h.Object) : L"";
		case Column::Access: return std::format(L"0x{:08X}", h.Access);
		case Column::DecodedAccess: return AccessMaskDecoder::Decode(ObjectTypes::GetName(h.TypeIndex), h.Access);
		case Column::Attributes:
		{
			std::wstring text;
			if (h.Attributes & OBJ_INHERIT)
				text = L"Inherit";
			if (h.Attributes & 0x1)	// OBJ_PROTECT_CLOSE
				text += text.empty() ? L"Protect" : L", Protect";
			return text;
		}
	}
	return L"";
}

int CHandlesPage::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || !m_Icons || !ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	auto& r = m_Rows[row];
	auto& type = ObjectTypes::GetName(r.Data->Entry.TypeIndex);
	// only a file's icon depends on its name
	return m_Icons->GetIconIndex(GetHandleIcon(type, type == L"File" ? GetName(*r.Data, r.Closed) : std::wstring()));
}

int CHandlesPage::GetSaveColumnRange(HWND, int&) const {
	// Refresh restores the selection (by handle value)
	return -1;
}

void CHandlesPage::DoSort(SortInfo const* si) {
	int selected = m_List.GetSelectedIndex();
	uint64_t selectedValue = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : 0;

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](HandleRow const& r1, HandleRow const& r2) {
		auto& h1 = r1.Data->Entry;
		auto& h2 = r2.Data->Entry;
		int result;
		switch (column) {
			case Column::Handle: result = compare(h1.Value, h2.Value); break;
			case Column::Address: result = compare((ULONG_PTR)h1.Object, (ULONG_PTR)h2.Object); break;
			case Column::Access: result = compare(h1.Access, h2.Access); break;
			case Column::Attributes: result = compare(h1.Attributes, h2.Attributes); break;
			default: result = ::_wcsicmp(GetText(r1, column).c_str(), GetText(r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectHandle(selectedValue);
}

LRESULT CHandlesPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			auto& row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and exited processes in the process list
			if (row.Closed) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.New) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

std::vector<ColumnLayout::ShownColumn> CHandlesPage::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CHandlesPage::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CHandlesPage::SaveColumns() const {
	if (m_Columns)
		return;
	auto& settings = AppSettings::Get();
	settings.HandleColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CHandlesPage::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if ((HWND)wParam == m_Types) {
		ShowTypesContextMenu(pt);
		return 0;
	}
	auto column = ColumnLayout::HeaderColumnAt(m_List, GetColumnManager(m_List), pt);
	if (!column) {
		// a row's, or the list's without a selected row (only the option then)
		bool row = GetSelectedRow() != nullptr;
		if (pt.x == -1 && pt.y == -1) {
			// from the keyboard: at the selected row
			CRect rc;
			if (row)
				m_List.GetItemRect(m_List.GetSelectedIndex(), &rc, LVIR_LABEL);
			else
				m_List.GetClientRect(&rc);
			m_List.ClientToScreen(&rc);
			pt = row ? CPoint(rc.left, rc.bottom) : rc.TopLeft();
		}
		CMenu menu;
		menu.CreatePopupMenu();
		if (row) {
			menu.AppendMenu(CanShowProperties() ? MF_STRING : MF_STRING | MF_GRAYED, ID_HANDLE_PROPERTIES, L"&Properties...");
			menu.SetMenuDefaultItem(ID_HANDLE_PROPERTIES);
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(CanCloseSelected() ? MF_STRING : MF_STRING | MF_GRAYED, ID_HANDLE_CLOSE, L"&Close Handle");
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, ID_PAGE_COPY, L"C&opy");
			menu.AppendMenu(MF_SEPARATOR);
		}
		menu.AppendMenu(m_ShowUnnamed ? MF_STRING | MF_CHECKED : MF_STRING, ID_VIEW_SHOWUNNAMEDHANDLES, L"Show &Unnamed Handles");
		menu.AppendMenu(MF_STRING, ID_HANDLE_BYTYPE, L"By &Type");
		MenuItemData const icons[] = {
			{ ID_HANDLE_CLOSE, IDI_DELETE },
			{ ID_PAGE_COPY, IDI_COPY },
		};
		WTLHelper::InitMenu(menu.m_hMenu, icons, _countof(icons), DpiHelper::SmallIconSize(m_hWnd));
		::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
		return 0;
	}
	m_MenuColumn = *column;
	CMenu menu;
	// the commands are posted to this page (see CThreadsPage::OnContextMenu)
	::TrackPopupMenuEx(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return 0;
}

LRESULT CHandlesPage::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CHandlesPage::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the table's defaults (Parse of no layout), in their order and widths
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CHandlesPage::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		// the addresses come from another source
		Refresh();
	}
	return 0;
}

void CHandlesPage::ShowRows(uint64_t selected) {
	m_Rows.clear();
	auto columns = m_Filter.GetText().empty() ? std::vector<ColumnLayout::ShownColumn>() : GetShownColumns();
	for (auto& row : m_AllRows)
		if (Matches(row, columns))
			m_Rows.push_back(row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectHandle(selected);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

bool CHandlesPage::Matches(HandleRow const& row, std::vector<ColumnLayout::ShownColumn> const& columns) const {
	// the name queried now, if it wasn't yet (a closed handle's can't be anymore)
	if (!m_ShowUnnamed && GetName(*row.Data, row.Closed).empty())
		return false;
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	// from By Type: that type, exactly, while the filter is its name
	if (!m_OnlyType.empty() && ::_wcsicmp(filter.c_str(), m_OnlyType.c_str()) == 0)
		return ::_wcsicmp(ObjectTypes::GetName(row.Data->Entry.TypeIndex).c_str(), m_OnlyType.c_str()) == 0;
	for (auto& column : columns)
		if (CFilterBox::Contains(GetText(row, (Column)column.Id), filter))
			return true;
	return false;
}

LRESULT CHandlesPage::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CHandlesPage::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_ByType) {
		if (int selected = m_Types.GetNextItem(-1, LVNI_SELECTED); selected >= 0) {
			std::wstring text;
			for (int c = 0; c < (int)TypeColumn::_Count; c++)
				text += (c ? L"\t" : L"") + GetTypeText((int)m_Types.GetItemData(selected), c);
			ClipboardHelper::CopyText(m_hWnd, text.c_str());
		}
		return 0;
	}
	if (int selected = m_List.GetSelectedIndex(); selected >= 0)
		ClipboardHelper::CopyText(m_hWnd, ListExport::RowText(m_List, selected).c_str());
	return 0;
}

void CHandlesPage::CountTypes() {
	std::unordered_map<std::wstring, int> counts;
	for (auto& row : m_AllRows)
		if (!row.Closed)
			counts[ObjectTypes::GetName(row.Data->Entry.TypeIndex)]++;
	// the first count after the process was selected (or the baseline reset) is the baseline
	bool first = !m_HasBaseline;
	if (first) {
		m_TypeBaseline = counts;
		m_HasBaseline = true;
	}
	// every type seen since the baseline, so a type whose handles all closed shows as 0
	std::unordered_set<std::wstring> types;
	for (auto& [type, n] : counts)
		types.insert(type);
	for (auto& [type, n] : m_TypeBaseline)
		types.insert(type);
	for (auto& [type, n] : m_TypePrevious)
		types.insert(type);
	m_TypeRows.clear();
	for (auto& type : types) {
		auto count = counts.contains(type) ? counts[type] : 0;
		auto previous = m_TypePrevious.contains(type) ? m_TypePrevious[type] : (first ? count : 0);
		auto baseline = m_TypeBaseline.contains(type) ? m_TypeBaseline[type] : 0;
		auto& peak = m_TypePeak[type];
		peak = (std::max)(peak, count);
		m_TypeRows.push_back({ type, count, count - previous, count - baseline, peak });
	}
	m_TypePrevious = std::move(counts);
}

std::wstring CHandlesPage::GetTypeText(int row, int column) const {
	if (row < 0 || row >= (int)m_TypeRows.size())
		return L"";
	auto& r = m_TypeRows[row];
	auto signed_ = [](int n) { return n > 0 ? std::format(L"+{}", n) : n < 0 ? std::to_wstring(n) : std::wstring(); };
	switch ((TypeColumn)column) {
		case TypeColumn::Type: return r.Type;
		case TypeColumn::Handles: return std::to_wstring(r.Count);
		case TypeColumn::Change: return signed_(r.Change);
		case TypeColumn::SinceBaseline: return signed_(r.SinceBaseline);
		case TypeColumn::Peak: return std::to_wstring(r.Peak);
	}
	return L"";
}

void CHandlesPage::ShowTypes() {
	// the selection and the scroll position, by type
	std::wstring selected;
	// by its text: the item data indexes the rows of the previous count, which CountTypes replaced
	if (int sel = m_Types.GetNextItem(-1, LVNI_SELECTED); sel >= 0) {
		WCHAR text[128];
		if (m_Types.GetItemText(sel, 0, text, _countof(text)))
			selected = text;
	}
	int top = m_Types.GetTopIndex();

	std::vector<int> rows;
	auto& filter = m_Filter.GetText();
	for (int i = 0; i < (int)m_TypeRows.size(); i++)
		if (filter.empty() || CFilterBox::Contains(m_TypeRows[i].Type, filter))
			rows.push_back(i);
	auto column = (TypeColumn)m_TypeSortColumn;
	std::ranges::sort(rows, [&](int i1, int i2) {
		auto& a = m_TypeRows[i1];
		auto& b = m_TypeRows[i2];
		int result = 0;
		switch (column) {
			case TypeColumn::Handles: result = a.Count - b.Count; break;
			case TypeColumn::Change: result = a.Change - b.Change; break;
			case TypeColumn::SinceBaseline: result = a.SinceBaseline - b.SinceBaseline; break;
			case TypeColumn::Peak: result = a.Peak - b.Peak; break;
		}
		if (result == 0)
			result = ::_wcsicmp(a.Type.c_str(), b.Type.c_str()) * (m_TypeSortAscending ? 1 : -1);
		return m_TypeSortAscending ? result < 0 : result > 0;
		});

	m_Types.SetRedraw(FALSE);
	m_Types.DeleteAllItems();
	int select = -1;
	for (int i = 0; i < (int)rows.size(); i++) {
		auto& r = m_TypeRows[rows[i]];
		int image = m_Icons ? m_Icons->GetIconIndex(GetHandleIcon(r.Type, std::wstring())) : -1;
		m_Types.InsertItem(LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM, i, r.Type.c_str(), 0, 0, image, rows[i]);
		for (int c = 1; c < (int)TypeColumn::_Count; c++)
			m_Types.SetItemText(i, c, GetTypeText(rows[i], c).c_str());
		if (r.Type == selected)
			select = i;
	}
	if (select >= 0)
		m_Types.SetItemState(select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	if (top > 0 && top < m_Types.GetItemCount()) {
		CRect rc;
		if (m_Types.GetItemRect(0, &rc, LVIR_BOUNDS))
			m_Types.Scroll(CSize(0, top * rc.Height()));
	}
	m_Types.SetRedraw(TRUE);
	m_Types.Invalidate();
}

LRESULT CHandlesPage::OnTypesNotify(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	switch (hdr->code) {
		case NM_CUSTOMDRAW:
			return OnTypesCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(hdr));
		case LVN_COLUMNCLICK:
		{
			auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
			if (column == m_TypeSortColumn)
				m_TypeSortAscending = !m_TypeSortAscending;
			else {
				m_TypeSortColumn = column;
				// names A to Z, numbers largest first
				m_TypeSortAscending = column == (int)TypeColumn::Type;
			}
			ShowTypes();
			break;
		}
		case NM_DBLCLK:
			PostMessage(WM_COMMAND, ID_HANDLE_SHOWTYPE);
			break;
	}
	return 0;
}

LRESULT CHandlesPage::OnTypesCustomDraw(NMLVCUSTOMDRAW* cd) {
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
			// the colors the list chose for the row (selected or not), for the cells that aren't changes
			m_TypeRowText = cd->clrText;
			m_TypeRowBack = cd->clrTextBk;
			return CDRF_NOTIFYSUBITEMDRAW;
		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		{
			cd->clrText = m_TypeRowText;
			cd->clrTextBk = m_TypeRowBack;
			auto row = (int)cd->nmcd.lItemlParam;
			if (row < 0 || row >= (int)m_TypeRows.size())
				break;
			int change = 0;
			if (cd->iSubItem == (int)TypeColumn::Change)
				change = m_TypeRows[row].Change;
			else if (cd->iSubItem == (int)TypeColumn::SinceBaseline)
				change = m_TypeRows[row].SinceBaseline;
			if (change != 0) {
				// as the Object Types tab: green up, red down
				bool dark = WTLHelper::IsDarkMode();
				cd->clrTextBk = change > 0 ? (dark ? RGB(0, 96, 0) : RGB(160, 255, 160)) : (dark ? RGB(128, 0, 0) : RGB(255, 160, 160));
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

void CHandlesPage::ShowTypesContextMenu(CPoint pt) {
	int selected = m_Types.GetNextItem(-1, LVNI_SELECTED);
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		if (selected >= 0)
			m_Types.GetItemRect(selected, &rc, LVIR_LABEL);
		else
			m_Types.GetClientRect(&rc);
		m_Types.ClientToScreen(&rc);
		pt = selected >= 0 ? CPoint(rc.left, rc.bottom) : rc.TopLeft();
	}
	CMenu menu;
	menu.CreatePopupMenu();
	if (selected >= 0) {
		menu.AppendMenu(MF_STRING, ID_HANDLE_SHOWTYPE, L"&Show Handles");
		menu.SetMenuDefaultItem(ID_HANDLE_SHOWTYPE);
		menu.AppendMenu(MF_STRING, ID_PAGE_COPY, L"C&opy");
		menu.AppendMenu(MF_SEPARATOR);
	}
	menu.AppendMenu(MF_STRING, ID_HANDLE_RESETBASELINE, L"&Reset Baseline");
	menu.AppendMenu(MF_STRING | MF_CHECKED, ID_HANDLE_BYTYPE, L"By &Type");
	MenuItemData const icons[] = {
		{ ID_PAGE_COPY, IDI_COPY },
	};
	WTLHelper::InitMenu(menu.m_hMenu, icons, _countof(icons), DpiHelper::SmallIconSize(m_hWnd));
	::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
}

LRESULT CHandlesPage::OnToggleByType(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_ByType = !m_ByType;
	// the filter goes: a handle filter makes no sense for types, nor the other way
	m_OnlyType.clear();
	if (!m_Filter.GetText().empty())
		m_Filter.Clear();
	if (m_ByType)
		ShowTypes();
	m_Types.ShowWindow(m_ByType ? SW_SHOW : SW_HIDE);
	m_List.ShowWindow(m_ByType ? SW_HIDE : SW_SHOW);
	(m_ByType ? m_Types : m_List).SetFocus();
	UpdateBar();
	return 0;
}

LRESULT CHandlesPage::OnResetBaseline(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the current counts become the baseline, the peaks start over
	m_TypeBaseline = m_TypePrevious;
	m_HasBaseline = true;
	m_TypePeak.clear();
	for (auto& r : m_TypeRows) {
		r.SinceBaseline = 0;
		r.Peak = r.Count;
		m_TypePeak[r.Type] = r.Count;
	}
	ShowTypes();
	return 0;
}

LRESULT CHandlesPage::OnShowType(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_Types.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return 0;
	auto type = m_TypeRows[m_Types.GetItemData(selected)].Type;
	// back to the handles, those of the type (exactly: the filter alone would match names too)
	m_ByType = false;
	UpdateBar();
	m_Types.ShowWindow(SW_HIDE);
	m_List.ShowWindow(SW_SHOW);
	m_OnlyType = type;
	m_Filter.SetText(type);
	m_List.SetFocus();
	return 0;
}
