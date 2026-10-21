#include "pch.h"
#include "KernelModulesView.h"
#include "AppSettings.h"

namespace {
	// in CKernelModulesView::Column order; the keys are stored in the KernelModuleColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Name", L"Name", 160, LVCFMT_LEFT, true, true },
		{ L"Base", L"Base", 130, LVCFMT_RIGHT, true },
		{ L"Size", L"Size", 80, LVCFMT_RIGHT, true },
		{ L"Load Order", L"LoadOrder", 75, LVCFMT_RIGHT, true },
		{ L"Description", L"Description", 250, LVCFMT_LEFT, true },
		{ L"Company", L"Company", 170, LVCFMT_LEFT, true },
		{ L"Version", L"Version", 130, LVCFMT_LEFT, true },
		{ L"Signature", L"Signature", 280, LVCFMT_LEFT, true },
		{ L"Path", L"Path", 380, LVCFMT_LEFT, true },
	};
}
#include "FormatHelper.h"
#include "VersionInfo.h"
#include "Symbols.h"
#include <WinSys/SecurityHelper.h>
#include <ClipboardHelper.h>
#include <ShlObj.h>

CKernelModulesView::~CKernelModulesView() {
	// the worker may be verifying a file, which can take seconds through the catalogs: it isn't waited for, but
	// finishes that one and exits without posting (it shares only m_Signatures)
	m_Verifier.request_stop();
	m_Signatures->Wake.notify_all();
	if (m_Verifier.joinable())
		m_Verifier.detach();
}

CString CKernelModulesView::GetTitle() const {
	return L"Kernel Modules";
}

LRESULT CKernelModulesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);
	m_Signatures->Notify = m_hWnd;

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	// the layout saved when this was the Drivers tab, until one is saved under the new name
	auto& settings = AppSettings::Get();
	auto layout = settings.KernelModuleColumns();
	SetColumns(ColumnLayout::Parse(layout.empty() ? settings.DriverColumns() : layout, s_Columns));

	int filterWidth = 360;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Filter: name, path, description or company (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	OnRefresh(false);
	return 0;
}

void CKernelModulesView::OnActivate(bool active) {
	// the Processes and Services views handle it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		// verified only once the tab is shown: hundreds of files, some of them through the system catalogs
		VerifySignatures();
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CKernelModulesView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CKernelModulesView::OnRefresh(bool active) {
	auto current = WinSys::KernelModules::Enum();
	if (current.empty())
		return;	// keep what we have
	// drivers rarely come and go: most refreshes have nothing to merge
	if (!IsUnchanged(current)) {
		// the rows point into m_All, which the merge replaces: the selection is taken first, and the rows are rebuilt
		auto selected = GetSelectedKey();
		m_Rows.clear();
		bool loaded = Merge(std::move(current));
		m_Loaded = true;
		ShowRows(selected);
		// verifying starts when the tab is first shown; after that, the new ones
		if (loaded && !m_Queued.empty())
			VerifySignatures();
	}
	if (active) {
		UpdateStatus();
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}
}

bool CKernelModulesView::IsUnchanged(std::vector<WinSys::KernelModule> const& current) const {
	// the merge keeps the modules' order, with the unloaded ones after them
	if (!m_Loaded || current.size() != m_All.size())
		return false;
	for (size_t i = 0; i < current.size(); i++) {
		auto& row = m_All[i];
		if (row.Unloaded || row.NewTicks > 0 || row.Module.Base != current[i].Base || row.Module.Path != current[i].Path)
			return false;
	}
	return true;
}

bool CKernelModulesView::Merge(std::vector<WinSys::KernelModule> current) {
	bool loaded = false;
	std::unordered_map<std::wstring, DriverRow*> previous;
	for (auto& row : m_All)
		if (!row.Unloaded)
			previous[row.Key] = &row;
	std::vector<DriverRow> merged;
	merged.reserve(current.size() + 8);
	for (auto& m : current) {
		DriverRow row{ std::move(m) };
		row.Key = Lowercase(row.Module.Path) + L"|" + std::to_wstring(row.Module.Base);
		if (auto it = previous.find(row.Key); it != previous.end()) {
			row.NewTicks = (std::max)(it->second->NewTicks - 1, 0);
			previous.erase(it);
		}
		else {
			loaded = true;
			if (m_Loaded)
				// on the first read, they're all new to the view, not loaded
				row.NewTicks = 2;
		}
		merged.push_back(std::move(row));
	}
	for (auto& [key, row] : previous) {
		auto unloaded = std::move(*row);
		unloaded.Unloaded = true;
		unloaded.NewTicks = 0;
		merged.push_back(std::move(unloaded));
	}
	m_All = std::move(merged);
	return loaded;
}

std::wstring CKernelModulesView::Lowercase(std::wstring text) {
	::CharLowerBuff(text.data(), (DWORD)text.size());
	return text;
}

void CKernelModulesView::VerifySignatures() {
	std::vector<std::wstring> paths;
	for (auto& row : m_All)
		if (!row.Unloaded && m_Queued.insert(Lowercase(row.Module.Path)).second)
			paths.push_back(row.Module.Path);
	if (paths.empty())
		return;
	{
		std::lock_guard lock(m_Signatures->Lock);
		for (auto& path : paths)
			m_Signatures->Pending.push_back(std::move(path));
	}
	if (!m_Verifier.joinable())
		m_Verifier = std::jthread(VerifyThread, m_Signatures);
	m_Signatures->Wake.notify_one();
}

void CKernelModulesView::VerifyThread(std::stop_token stop, std::shared_ptr<Signatures> signatures) {
	// WinVerifyTrust's catalog lookups may use COM
	::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	for (;;) {
		std::wstring path;
		{
			std::unique_lock lock(signatures->Lock);
			if (!signatures->Wake.wait(lock, stop, [&] { return !signatures->Pending.empty(); }))
				break;
			path = std::move(signatures->Pending.front());
			signatures->Pending.pop_front();
		}
		auto info = WinSys::FileSignature::Verify(path.c_str());
		if (stop.stop_requested())
			break;
		{
			std::lock_guard lock(signatures->Lock);
			signatures->Results[Lowercase(path)] = std::move(info);
		}
		::PostMessage(signatures->Notify, WM_KMODULE_SIGNED, 0, 0);
	}
	::CoUninitialize();
}

LRESULT CKernelModulesView::OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// several may have arrived: the list repaints once (and sorts again, if it's sorted by signature)
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_KMODULE_SIGNED, WM_KMODULE_SIGNED, PM_REMOVE))
		;
	if (auto si = GetSortInfo(m_List); si && GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn) == Column::Signature)
		DoSort(si);
	m_List.Invalidate();
	return 0;
}

std::wstring CKernelModulesView::GetSignatureText(std::wstring const& path) const {
	std::lock_guard lock(m_Signatures->Lock);
	auto it = m_Signatures->Results.find(Lowercase(path));
	if (it != m_Signatures->Results.end())
		return FormatSignature(it->second);
	return m_Verifier.joinable() ? L"Verifying..." : L"";
}

bool CKernelModulesView::Matches(DriverRow const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Name, Column::Path, Column::Description, Column::Company })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

std::wstring CKernelModulesView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? row->Key : L"";
}

void CKernelModulesView::ShowRows(std::wstring const& selected) {
	m_Rows.clear();
	for (auto& row : m_All)
		if (Matches(row))
			m_Rows.push_back(&row);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	SelectDriver(selected);
	m_List.Invalidate();
}

void CKernelModulesView::UpdateStatus() {
	if (!m_Active)
		return;
	auto count = std::ranges::count_if(m_All, [](auto& row) { return !row.Unloaded; });
	auto text = std::format(L"{} kernel modules", count);
	if (!m_Filter.GetText().empty())
		text += std::format(L"; {} match the filter", std::ranges::count_if(m_Rows, [](auto row) { return !row->Unloaded; }));
	if (!WinSys::SecurityHelper::IsRunningElevated())
		text += L" (their bases need elevation)";
	Frame()->SetStatusText(0, text.c_str());
}

std::wstring CKernelModulesView::GetText(DriverRow const& row, Column column) const {
	auto& m = row.Module;
	switch (column) {
		case Column::Name: return m.Name;
		case Column::Base: return m.Base ? FormatAddress(m.Base) : L"";
		case Column::Size: return FormatHelper::FormatSize(m.Size);
		case Column::LoadOrder: return std::to_wstring(m.LoadOrder);
		case Column::Description: return GetCachedVersionInfo(m.Path).Description;
		case Column::Company: return GetCachedVersionInfo(m.Path).Company;
		case Column::Version: return GetCachedVersionInfo(m.Path).Version;
		case Column::Signature: return GetSignatureText(m.Path);
		case Column::Path: return m.Path;
	}
	return L"";
}

CString CKernelModulesView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CKernelModulesView::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || GetColumnManager(m_List)->GetColumnTag<Column>(col) != Column::Name)
		return -1;
	return Frame()->GetIconIndex(IDI_DEVICE);
}

int CKernelModulesView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by driver (ShowRows, DoSort)
	return -1;
}

void CKernelModulesView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](DriverRow const* r1, DriverRow const* r2) {
		int result = 0;
		switch (column) {
			case Column::Base: result = compare(r1->Module.Base, r2->Module.Base); break;
			case Column::Size: result = compare(r1->Module.Size, r2->Module.Size); break;
			case Column::LoadOrder: result = compare(r1->Module.LoadOrder, r2->Module.LoadOrder); break;
			default: result = ::_wcsicmp(GetText(*r1, column).c_str(), GetText(*r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectDriver(selected);
}

CKernelModulesView::DriverRow const* CKernelModulesView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CKernelModulesView::SelectDriver(std::wstring const& key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (key.empty())
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Key == key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

LRESULT CKernelModulesView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			auto& row = *m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of the other tabs' new and gone rows
			if (row.Unloaded) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CKernelModulesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_KMODULE_PROPERTIES, L"&Properties...");
	menu.AppendMenu(MF_STRING, ID_KMODULE_GOTOFILELOCATION, L"Open File &Location");
	menu.AppendMenu(MF_STRING, ID_KMODULE_GOTODRIVER, L"Go to &Driver");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.SetMenuDefaultItem(ID_KMODULE_PROPERTIES);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CKernelModulesView::ShowKernelModule(std::wstring const& path) {
	auto loaded = std::ranges::find_if(m_All, [&](auto& row) { return !row.Unloaded && ::_wcsicmp(row.Module.Path.c_str(), path.c_str()) == 0; });
	if (loaded == m_All.end())
		return false;
	// a filter that hides it goes
	if (!Matches(*loaded))
		m_Filter.Clear();
	SelectDriver(loaded->Key);
	if (int selected = m_List.GetSelectedIndex(); selected >= 0)
		m_List.EnsureVisible(selected, FALSE);
	m_List.SetFocus();
	return true;
}

LRESULT CKernelModulesView::OnGoToDriver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		Frame()->GoToDriver(row->Module.Path);
	return 0;
}

bool CKernelModulesView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	::SHObjectProperties(m_hWnd, SHOP_FILEPATH, m_Rows[row]->Module.Path.c_str(), nullptr);
	return true;
}

LRESULT CKernelModulesView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		::SHObjectProperties(m_hWnd, SHOP_FILEPATH, row->Module.Path.c_str(), nullptr);
	return 0;
}

LRESULT CKernelModulesView::OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", row->Module.Path).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CKernelModulesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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

LRESULT CKernelModulesView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CKernelModulesView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CKernelModulesView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CKernelModulesView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CKernelModulesView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CKernelModulesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.KernelModuleColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CKernelModulesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CKernelModulesView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CKernelModulesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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
