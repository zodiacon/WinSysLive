#include "pch.h"
#include "KernelCallbacksDlg.h"
#include <WinSys/FileVersion.h>
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

namespace {
	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	std::wstring Wide(std::string const& text) {
		return std::wstring(text.begin(), text.end());	// the import names are ASCII
	}

	// what watches the system's activity, as opposed to Plug and Play, power, bug checks and the like
	constexpr auto RelevantKinds = CallbackKind::Process | CallbackKind::Thread | CallbackKind::ImageLoad | CallbackKind::Registry
		| CallbackKind::Object | CallbackKind::FileSystem | CallbackKind::Network | CallbackKind::BootDriver;
}

LRESULT CKernelCallbacksDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_SYSTEM_DRIVER, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_SYSTEM_DRIVER, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_Tabs.Attach(GetDlgItem(IDC_KC_TABS));
	// the tabs' icons, in the window's small icon size
	int size = ::GetSystemMetricsForDpi(SM_CXSMICON, ::GetDpiForWindow(m_hWnd));
	m_TabImages.Create(size, size, ILC_COLOR32 | ILC_MASK, 2, 0);
	for (auto id : { IDI_FILTER, IDI_CALLBACK })
		m_TabImages.AddIcon((HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(id), IMAGE_ICON, size, size, LR_SHARED));
	m_Tabs.SetImageList(m_TabImages);
	m_Tabs.InsertItem(0, TCIF_TEXT | TCIF_IMAGE, L"Minifilters", 0, 0);
	m_Tabs.InsertItem(1, TCIF_TEXT | TCIF_IMAGE, L"Callback Registrations", 1, 0);
	m_List.Attach(GetDlgItem(IDC_KC_LIST));
	m_Details.Attach(GetDlgItem(IDC_KC_DETAILS));
	// above the tab control, which would otherwise paint over them (see the Network Information dialog)
	for (auto id : { IDC_KC_FILTER, IDC_KC_RELEVANT, IDC_KC_LIST, IDC_KC_DETAILS })
		GetDlgItem(id).SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	for (auto list : { &m_List, &m_Details }) {
		list->SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		ListViewHelper::EnableColumnDpiScaling(*list);
	}
	::SendDlgItemMessage(m_hWnd, IDC_KC_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by any column, or a registration function");
	CheckDlgButton(IDC_KC_RELEVANT, BST_CHECKED);

	auto& filters = m_Tables[(int)Tab::Minifilters];
	filters.Columns = { { L"Filter", 140 }, { L"Altitude", 80, true }, { L"Instances", 70, true }, { L"Frame", 50, true },
		{ L"Type", 80 }, { L"Company", 160 }, { L"Description", 220 }, { L"Image Path", 320 } };
	filters.SortColumn = 1;
	filters.SortAscending = false;	// as the filter manager stacks them: the highest first
	m_DetailTables[(int)Tab::Minifilters].Columns = { { L"Volume", 70 }, { L"Device", 230 }, { L"File System", 90 },
		{ L"Instance", 200 }, { L"Altitude", 80, true }, { L"State", 80 } };
	auto& drivers = m_Tables[(int)Tab::Callbacks];
	drivers.Columns = { { L"Driver", 150 }, { L"Registers For", 300 }, { L"Imports", 60, true }, { L"Company", 160 },
		{ L"Description", 220 }, { L"Path", 320 }, { L"Load Order", 70, true } };
	drivers.SortColumn = 6;
	m_DetailTables[(int)Tab::Callbacks].Columns = { { L"Function", 260 }, { L"Library", 110 }, { L"Kind", 110 }, { L"What It Hooks", 440 } };
	m_CopyList = m_List;

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	ShowTab();
	return TRUE;
}

void CKernelCallbacksDlg::Load() {
	CWaitCursor wait;
	m_FilterError = 0;
	m_Filters.clear();
	if (auto filters = KernelCallbacks::EnumMinifilters())
		m_Filters = std::move(*filters);
	else
		m_FilterError = ::GetLastError();
	auto& filterRows = m_Tables[(int)Tab::Minifilters].Rows;
	filterRows.clear();
	for (auto& f : m_Filters) {
		auto version = f.ImagePath.empty() ? FileVersionInfo() : FileVersion::Read(f.ImagePath);
		filterRows.push_back({ f.Name, f.Altitude, std::to_wstring(f.InstanceCount), f.Legacy ? L"" : std::to_wstring(f.FrameId),
			f.Legacy ? L"Legacy" : L"Minifilter", version.Company, version.Description, f.ImagePath });
	}

	m_Drivers = KernelCallbacks::EnumCallbackDrivers();
	auto& driverRows = m_Tables[(int)Tab::Callbacks].Rows;
	driverRows.clear();
	m_SearchText.clear();
	for (auto& d : m_Drivers.Drivers) {
		auto version = FileVersion::Read(d.Path);
		driverRows.push_back({ d.Name, KernelCallbacks::KindsText(d.Kinds), std::to_wstring(d.Imports.size()), version.Company,
			version.Description, d.Path, std::to_wstring(d.LoadOrder) });
		std::wstring search;
		for (auto& i : d.Imports)
			search += Lowercase(Wide(i.Function)) + L"\n";
		m_SearchText.push_back(std::move(search));
	}
}

CKernelCallbacksDlg::Tab CKernelCallbacksDlg::CurrentTab() const {
	return m_Tabs.GetCurSel() == 1 ? Tab::Callbacks : Tab::Minifilters;
}

void CKernelCallbacksDlg::InitColumns(CListViewCtrl& list, Table const& table) {
	list.SetRedraw(FALSE);
	list.DeleteAllItems();
	while (list.DeleteColumn(0))
		;
	auto dpi = ::GetDpiForWindow(list);
	for (int i = 0; i < (int)table.Columns.size(); i++) {
		auto& c = table.Columns[i];
		list.InsertColumn(i, c.Name, c.Numeric ? LVCFMT_RIGHT : LVCFMT_LEFT, ::MulDiv(c.Width, dpi, 96));
	}
	list.SetRedraw(TRUE);
}

void CKernelCallbacksDlg::ShowTab() {
	auto tab = (int)CurrentTab();
	m_Filling = true;
	InitColumns(m_List, m_Tables[tab]);
	InitColumns(m_Details, m_DetailTables[tab]);
	m_Filling = false;
	GetDlgItem(IDC_KC_RELEVANT).ShowWindow(CurrentTab() == Tab::Callbacks ? SW_SHOW : SW_HIDE);
	ShowRows();
}

bool CKernelCallbacksDlg::Keep(int row) const {
	auto tab = CurrentTab();
	if (tab == Tab::Callbacks && IsDlgButtonChecked(IDC_KC_RELEVANT) == BST_CHECKED
		&& (m_Drivers.Drivers[row].Kinds & RelevantKinds) == CallbackKind::None)
		return false;
	if (m_Filter.empty())
		return true;
	for (auto& cell : m_Tables[(int)tab].Rows[row])
		if (Lowercase(cell).find(m_Filter) != std::wstring::npos)
			return true;
	return tab == Tab::Callbacks && m_SearchText[row].find(m_Filter) != std::wstring::npos;
}

void CKernelCallbacksDlg::ShowRows() {
	auto& table = m_Tables[(int)CurrentTab()];
	table.Shown.clear();
	for (int i = 0; i < (int)table.Rows.size(); i++)
		if (Keep(i))
			table.Shown.push_back(i);
	m_Filling = true;
	Fill(m_List, table);
	if (m_List.GetItemCount() > 0 && m_List.GetNextItem(-1, LVNI_SELECTED) < 0)
		m_List.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_Filling = false;
	ShowDetails();
}

void CKernelCallbacksDlg::ShowDetails() {
	auto tab = CurrentTab();
	auto& table = m_DetailTables[(int)tab];
	table.Rows.clear();
	if (int row = GetSelectedRow(); row >= 0) {
		if (tab == Tab::Minifilters) {
			for (auto& i : m_Filters[row].Instances)
				table.Rows.push_back({ i.DosVolume, i.Volume, i.Legacy ? L"" : KernelCallbacks::FileSystemName(i.FileSystem), i.Name,
					i.Altitude, i.Detached ? L"Detached" : L"" });
		}
		else {
			for (auto& i : m_Drivers.Drivers[row].Imports)
				table.Rows.push_back({ Wide(i.Function), Wide(i.Library), KernelCallbacks::KindsText(i.Kind),
					KernelCallbacks::Describe(i.Function) });
		}
	}
	table.Shown.clear();
	for (int i = 0; i < (int)table.Rows.size(); i++)
		table.Shown.push_back(i);
	Fill(m_Details, table);
	UpdateStatus();
}

void CKernelCallbacksDlg::Sort(Table& table) {
	auto column = table.SortColumn;
	bool numeric = column < (int)table.Columns.size() && table.Columns[column].Numeric;
	std::ranges::stable_sort(table.Shown, [&](int a, int b) {
		auto& x = table.Rows[a][column];
		auto& y = table.Rows[b][column];
		int result;
		if (numeric) {
			// altitudes may have a fraction ("385250.5")
			auto nx = std::wcstod(x.c_str(), nullptr), ny = std::wcstod(y.c_str(), nullptr);
			result = nx < ny ? -1 : ny < nx ? 1 : 0;
		}
		else
			result = ::_wcsicmp(x.c_str(), y.c_str());
		return table.SortAscending ? result < 0 : result > 0;
		});
}

void CKernelCallbacksDlg::Fill(CListViewCtrl& list, Table& table) {
	Sort(table);
	// the selection by its first cell
	std::wstring selected;
	if (int sel = list.GetNextItem(-1, LVNI_SELECTED); sel >= 0) {
		WCHAR text[512];
		if (list.GetItemText(sel, 0, text, _countof(text)))
			selected = text;
	}
	list.SetRedraw(FALSE);
	list.DeleteAllItems();
	int select = -1;
	for (int i = 0; i < (int)table.Shown.size(); i++) {
		auto& cells = table.Rows[table.Shown[i]];
		list.InsertItem(i, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			list.SetItemText(i, c, cells[c].c_str());
		list.SetItemData(i, table.Shown[i]);
		if (select < 0 && !selected.empty() && cells[0] == selected)
			select = i;
	}
	if (select >= 0) {
		list.SetItemState(select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		list.EnsureVisible(select, FALSE);
	}
	list.SetRedraw(TRUE);
	list.Invalidate();
}

int CKernelCallbacksDlg::GetSelectedRow() const {
	int sel = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (sel < 0)
		return -1;
	auto row = (int)m_List.GetItemData(sel);
	auto count = CurrentTab() == Tab::Minifilters ? m_Filters.size() : m_Drivers.Drivers.size();
	return row < (int)count ? row : -1;
}

std::wstring CKernelCallbacksDlg::GetSelectedPath() const {
	auto row = GetSelectedRow();
	if (row < 0)
		return L"";
	return CurrentTab() == Tab::Minifilters ? m_Filters[row].ImagePath : m_Drivers.Drivers[row].Path;
}

void CKernelCallbacksDlg::UpdateStatus() {
	auto& table = m_Tables[(int)CurrentTab()];
	std::wstring text;
	if (CurrentTab() == Tab::Minifilters) {
		if (m_FilterError)
			text = m_FilterError == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated()
				? L"The minifilters need administrator rights"
				: L"The minifilters can't be read: " + Helpers::GetErrorText(m_FilterError);
		else {
			text = std::to_wstring(m_Filters.size()) + L" filters";
			if (table.Shown.size() != table.Rows.size())
				text += L" (" + std::to_wstring(table.Shown.size()) + L" match)";
		}
	}
	else {
		// the registered callbacks themselves are in kernel memory, which needs a driver to read
		text = std::to_wstring(table.Shown.size()) + L" of " + std::to_wstring(m_Drivers.Modules) + L" modules";
		if (!m_Drivers.Unreadable.empty())
			text += L" (" + std::to_wstring(m_Drivers.Unreadable.size()) + L" unreadable)";
		text += L". Imports: what a driver can register, not what it did";
	}
	SetDlgItemText(IDC_KC_STATUS, text.c_str());
	GetDlgItem(IDC_KC_GOTO).EnableWindow(!GetSelectedPath().empty());
}

LRESULT CKernelCallbacksDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto hWnd = (HWND)wParam;
	if (hWnd != m_List && hWnd != m_Details) {
		bHandled = FALSE;
		return 0;
	}
	CListViewCtrl list(hWnd);
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		if (int sel = list.GetNextItem(-1, LVNI_SELECTED); sel >= 0)
			list.GetItemRect(sel, &rc, LVIR_LABEL);
		pt = rc.CenterPoint();
		list.ClientToScreen(&pt);
	}
	m_CopyList = hWnd;
	CMenu menu;
	menu.CreatePopupMenu();
	if (hWnd == m_List) {
		menu.AppendMenu(MF_STRING | (GetSelectedPath().empty() ? MF_GRAYED : 0), IDC_KC_GOTO, L"&Go to Driver");
		menu.AppendMenu(MF_SEPARATOR);
	}
	menu.AppendMenu(MF_STRING, IDC_KC_COPY, L"&Copy");
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CKernelCallbacksDlg::OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	m_CopyList = m_List;
	ShowTab();
	return 0;
}

LRESULT CKernelCallbacksDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (!m_Filling && (lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
		ShowDetails();
	return 0;
}

LRESULT CKernelCallbacksDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnGoTo(0, IDC_KC_GOTO, nullptr, handled);
}

LRESULT CKernelCallbacksDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	bool upper = hdr->hwndFrom == m_List;
	auto& table = upper ? m_Tables[(int)CurrentTab()] : m_DetailTables[(int)CurrentTab()];
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == table.SortColumn)
		table.SortAscending = !table.SortAscending;
	else {
		table.SortColumn = column;
		// names A to Z, numbers largest first
		table.SortAscending = !table.Columns[column].Numeric;
	}
	m_Filling = upper;
	Fill(upper ? m_List : m_Details, table);
	m_Filling = false;
	return 0;
}

LRESULT CKernelCallbacksDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_List || hdr->hwndFrom == m_Details)
		m_CopyList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CKernelCallbacksDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_KC_FILTER, text);
	m_Filter = Lowercase((PCWSTR)text);
	ShowRows();
	return 0;
}

LRESULT CKernelCallbacksDlg::OnRelevant(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ShowRows();
	return 0;
}

LRESULT CKernelCallbacksDlg::OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_GoTo = GetSelectedPath();
	if (!m_GoTo.empty())
		EndDialog(IDC_KC_GOTO);
	return 0;
}

LRESULT CKernelCallbacksDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	ShowRows();
	return 0;
}

LRESULT CKernelCallbacksDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CListViewCtrl list(m_CopyList);
	auto& table = m_CopyList == m_Details ? m_DetailTables[(int)CurrentTab()] : m_Tables[(int)CurrentTab()];
	std::wstring text;
	for (int c = 0; c < (int)table.Columns.size(); c++)
		text += (c ? L"\t" : L"") + std::wstring(table.Columns[c].Name);
	text += L"\r\n";
	bool selected = list.GetSelectedCount() > 0;
	for (int i = 0; i < list.GetItemCount(); i++) {
		if (selected && !(list.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& cells = table.Rows[list.GetItemData(i)];
		for (int c = 0; c < (int)cells.size(); c++)
			text += (c ? L"\t" : L"") + cells[c];
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CKernelCallbacksDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
