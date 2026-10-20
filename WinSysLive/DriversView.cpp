#include "pch.h"
#include "DriversView.h"
#include "FormatHelper.h"
#include "VersionInfo.h"
#include "Symbols.h"
#include <WinSys/SecurityHelper.h>
#include <ClipboardHelper.h>
#include <ShlObj.h>

CDriversView::~CDriversView() {
	// the worker may be verifying a file: it finishes that one and exits (the jthread joins it)
	m_Verifier.request_stop();
	m_Signatures->Wake.notify_all();
}

CString CDriversView::GetTitle() const {
	return L"Drivers";
}

LRESULT CDriversView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);
	m_Signatures->Notify = m_hWnd;

	auto cm = GetColumnManager(m_List);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	cm->AddColumn(L"Name", LVCFMT_LEFT, width(160), Column::Name);
	cm->AddColumn(L"Base", LVCFMT_RIGHT, width(130), Column::Base);
	cm->AddColumn(L"Size", LVCFMT_RIGHT, width(80), Column::Size);
	cm->AddColumn(L"Load Order", LVCFMT_RIGHT, width(75), Column::LoadOrder);
	cm->AddColumn(L"Description", LVCFMT_LEFT, width(250), Column::Description);
	cm->AddColumn(L"Company", LVCFMT_LEFT, width(170), Column::Company);
	cm->AddColumn(L"Version", LVCFMT_LEFT, width(130), Column::Version);
	cm->AddColumn(L"Signature", LVCFMT_LEFT, width(280), Column::Signature);
	cm->AddColumn(L"Path", LVCFMT_LEFT, width(380), Column::Path);

	int filterWidth = width(360);
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, filterWidth, L"Filter: name, path, description or company (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, filterWidth);

	OnRefresh(false);
	return 0;
}

void CDriversView::OnActivate(bool active) {
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

void CDriversView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CDriversView::OnRefresh(bool active) {
	auto current = WinSys::KernelModules::Enum();
	if (current.empty())
		return;	// keep what we have
	// the rows point into m_All, which the merge replaces: the selection is taken first, and the rows are rebuilt
	auto selected = GetSelectedKey();
	m_Rows.clear();
	Merge(std::move(current));
	m_Loaded = true;
	ShowRows(selected);
	if (!m_Queued.empty())
		VerifySignatures();	// the new ones
	if (active) {
		UpdateStatus();
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}
}

void CDriversView::Merge(std::vector<WinSys::KernelModule> current) {
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
		else if (m_Loaded)
			// on the first read, they're all new to the view, not loaded
			row.NewTicks = 2;
		merged.push_back(std::move(row));
	}
	for (auto& [key, row] : previous) {
		auto unloaded = std::move(*row);
		unloaded.Unloaded = true;
		unloaded.NewTicks = 0;
		merged.push_back(std::move(unloaded));
	}
	m_All = std::move(merged);
}

std::wstring CDriversView::Lowercase(std::wstring text) {
	::CharLowerBuff(text.data(), (DWORD)text.size());
	return text;
}

void CDriversView::VerifySignatures() {
	std::vector<std::wstring> paths;
	for (auto& row : m_All)
		if (!row.Unloaded && m_Queued.insert(Lowercase(row.Module.Path)).second)
			paths.push_back(row.Module.Path);
	if (!paths.empty()) {
		std::lock_guard lock(m_Signatures->Lock);
		for (auto& path : paths)
			m_Signatures->Pending.push_back(std::move(path));
	}
	if (!m_Verifier.joinable())
		m_Verifier = std::jthread(VerifyThread, m_Signatures);
	m_Signatures->Wake.notify_one();
}

void CDriversView::VerifyThread(std::stop_token stop, std::shared_ptr<Signatures> signatures) {
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
		::PostMessage(signatures->Notify, WM_DRIVER_SIGNED, 0, 0);
	}
	::CoUninitialize();
}

LRESULT CDriversView::OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// several may have arrived: the list repaints once (and sorts again, if it's sorted by signature)
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_DRIVER_SIGNED, WM_DRIVER_SIGNED, PM_REMOVE))
		;
	if (auto si = GetSortInfo(m_List); si && GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn) == Column::Signature)
		DoSort(si);
	m_List.Invalidate();
	return 0;
}

std::wstring CDriversView::GetSignatureText(std::wstring const& path) const {
	std::lock_guard lock(m_Signatures->Lock);
	auto it = m_Signatures->Results.find(Lowercase(path));
	if (it != m_Signatures->Results.end())
		return FormatSignature(it->second);
	return m_Verifier.joinable() ? L"Verifying..." : L"";
}

bool CDriversView::Matches(DriverRow const& row) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Name, Column::Path, Column::Description, Column::Company })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

std::wstring CDriversView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? row->Key : L"";
}

void CDriversView::ShowRows(std::wstring const& selected) {
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

void CDriversView::UpdateStatus() {
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

std::wstring CDriversView::GetText(DriverRow const& row, Column column) const {
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

CString CDriversView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CDriversView::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || GetColumnManager(m_List)->GetColumnTag<Column>(col) != Column::Name)
		return -1;
	return Frame()->GetIconIndex(IDI_DEVICE);
}

int CDriversView::GetSaveColumnRange(HWND, int&) const {
	// the selection is kept by driver (ShowRows, DoSort)
	return -1;
}

void CDriversView::DoSort(SortInfo const* si) {
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

CDriversView::DriverRow const* CDriversView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

void CDriversView::SelectDriver(std::wstring const& key) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (key.empty())
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Key == key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

LRESULT CDriversView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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

bool CDriversView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_DRIVER_PROPERTIES, L"&Properties...");
	menu.AppendMenu(MF_STRING, ID_DRIVER_GOTOFILELOCATION, L"Open File &Location");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.SetMenuDefaultItem(ID_DRIVER_PROPERTIES);
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CDriversView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	::SHObjectProperties(m_hWnd, SHOP_FILEPATH, m_Rows[row]->Module.Path.c_str(), nullptr);
	return true;
}

LRESULT CDriversView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		::SHObjectProperties(m_hWnd, SHOP_FILEPATH, row->Module.Path.c_str(), nullptr);
	return 0;
}

LRESULT CDriversView::OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected())
		::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", row->Module.Path).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CDriversView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row)
		return 0;
	// the columns, tab separated
	std::wstring text;
	for (int column = 0; column < (int)Column::_Count; column++) {
		if (column > 0)
			text += L'\t';
		text += GetText(*row, (Column)column);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CDriversView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CDriversView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}
