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
#include <WinSys/Helpers.h>
#include <WinSys/Sid.h>
#include <WTLHelper.h>

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

	// highlighting of opened handles lasts this many refreshes
	const int NewHandleTicks = 2;
	// when this many name queries have hung (their workers are stuck), files aren't queried anymore
	const int MaxHungQueries = 4;
}

LRESULT CHandlesPage::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	// the type icons, in the leftmost column
	ResetImageList();
	SuspendHookWhilePainting(m_List);
	m_Filter.Create(m_hWnd, IDC_PAGE_FILTER, 200, L"Filter (Ctrl+F)", m_List, [this](std::wstring const&) {
		int selected = m_List.GetSelectedIndex();
		ShowRows(selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : static_cast<uint64_t>(0));
		});
	SetColumns(ColumnLayout::Parse(m_Columns ? *m_Columns : AppSettings::Get().HandleColumns(), s_Columns));
	return 0;
}

LRESULT CHandlesPage::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

LRESULT CHandlesPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_List) {
		int cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
		// the filter box keeps its height, the list takes the rest
		CWindow filter(m_Filter.GetHwnd());
		CRect rc;
		filter.GetWindowRect(&rc);
		filter.MoveWindow(0, 0, cx, rc.Height());
		m_List.MoveWindow(0, rc.Height(), cx, (std::max)(cy - rc.Height(), 0));
	}
	return 0;
}

LRESULT CHandlesPage::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.SetFocus();
	return 0;
}

LRESULT CHandlesPage::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// the list covers the page
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
	m_FirstEnum = true;
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
						data->NewTicks = NewHandleTicks;
				}
				HandleRow row{ data };
				if (data->NewTicks > 0) {
					row.New = true;
					data->NewTicks--;
				}
				current.insert({ h.Value, data });
				m_Rows.push_back(std::move(row));
			}
			// the handles left were closed: shown once more
			for (auto& [value, data] : m_Handles)
				m_Rows.push_back({ data, false, true });
			m_Handles = std::move(current);
			m_FirstEnum = false;
		}
	}

	m_AllRows = std::move(m_Rows);
	ShowRows(selectedValue);
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

LRESULT CHandlesPage::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	auto column = ColumnLayout::HeaderColumnAt(m_List, GetColumnManager(m_List), pt);
	if (!column) {
		// a row's
		if (!GetSelectedRow()) {
			bHandled = FALSE;
			return 0;
		}
		if (pt.x == -1 && pt.y == -1) {
			// from the keyboard: at the selected row
			CRect rc;
			m_List.GetItemRect(m_List.GetSelectedIndex(), &rc, LVIR_LABEL);
			m_List.ClientToScreen(&rc);
			pt = { rc.left, rc.bottom };
		}
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(CanShowProperties() ? MF_STRING : MF_STRING | MF_GRAYED, ID_HANDLE_PROPERTIES, L"&Properties...");
		menu.SetMenuDefaultItem(ID_HANDLE_PROPERTIES);
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(CanCloseSelected() ? MF_STRING : MF_STRING | MF_GRAYED, ID_HANDLE_CLOSE, L"&Close Handle");
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_PAGE_COPY, L"C&opy");
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
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
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
	if (int selected = m_List.GetSelectedIndex(); selected >= 0)
		ClipboardHelper::CopyText(m_hWnd, ListExport::RowText(m_List, selected).c_str());
	return 0;
}
