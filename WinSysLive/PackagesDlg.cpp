#include "pch.h"
#include "PackagesDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

namespace {
	// the package name in a full name (the part before the first '_')
	std::wstring NameOf(std::wstring const& fullName) {
		return fullName.substr(0, fullName.find(L'_'));
	}
}

LRESULT CPackagesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_PACKAGE, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_PACKAGE, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_PK_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Name", 200, LVCFMT_LEFT }, { L"Package", 220, LVCFMT_LEFT }, { L"Version", 110, LVCFMT_LEFT },
		{ L"Architecture", 75, LVCFMT_LEFT }, { L"Kind", 70, LVCFMT_LEFT }, { L"Publisher", 150, LVCFMT_LEFT },
		{ L"Signature", 70, LVCFMT_LEFT }, { L"Status", 70, LVCFMT_LEFT }, { L"Installed", 130, LVCFMT_LEFT },
		{ L"Processes", 70, LVCFMT_RIGHT }, { L"Location", 300, LVCFMT_LEFT }, { L"Dependencies", 250, LVCFMT_LEFT },
		{ L"Full Name", 350, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	::SendDlgItemMessage(m_hWnd, IDC_PK_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name, publisher or location");
	CheckDlgButton(IDC_PK_FRAMEWORKS, BST_CHECKED);
	// every user's packages need elevation
	GetDlgItem(IDC_PK_ALLUSERS).EnableWindow(SecurityHelper::IsRunningElevated());

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CPackagesDlg::Load() {
	if (m_Load)
		return;
	bool allUsers = IsDlgButtonChecked(IDC_PK_ALLUSERS) == BST_CHECKED;
	GetDlgItem(IDC_PK_REFRESH).EnableWindow(FALSE);
	GetDlgItem(IDC_PK_ALLUSERS).EnableWindow(FALSE);
	SetDlgItemText(IDC_PK_STATUS, L"Reading the installed packages...");
	// the thread holds the state, not the dialog, so closing needn't wait for it
	m_Load = std::make_shared<LoadState>();
	std::thread([state = m_Load, hWnd = m_hWnd, allUsers] {
		// the package manager is a Windows Runtime class
		auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		auto packages = Packages::Enum(allUsers);
		auto error = ::GetLastError();
		if (SUCCEEDED(com))
			::CoUninitialize();
		{
			std::lock_guard lock(state->Lock);
			state->Result = std::move(packages);
			state->Error = error;
		}
		if (::IsWindow(hWnd))
			::PostMessage(hWnd, WM_PACKAGES_READ, allUsers, 0);
		}).detach();
}

LRESULT CPackagesDlg::OnPackagesRead(UINT /*uMsg*/, WPARAM allUsers, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (!m_Load)
		return 0;
	{
		std::lock_guard lock(m_Load->Lock);
		m_Error = m_Load->Result ? 0 : m_Load->Error;
		if (m_Load->Result)
			m_Packages = std::move(*m_Load->Result);
		else
			m_Packages.clear();
	}
	m_Load.reset();
	m_AllUsersShown = allUsers != 0;
	m_Processes = m_Running ? m_Running() : decltype(m_Processes){};
	GetDlgItem(IDC_PK_REFRESH).EnableWindow(TRUE);
	GetDlgItem(IDC_PK_ALLUSERS).EnableWindow(SecurityHelper::IsRunningElevated());
	ShowRows();
	return 0;
}

std::vector<uint32_t> const* CPackagesDlg::GetProcesses(PackageInfo const& p) const {
	auto it = m_Processes.find(p.FullName);
	return it == m_Processes.end() ? nullptr : &it->second;
}

std::wstring CPackagesDlg::GetText(PackageInfo const& p, Column column) const {
	switch (column) {
		case Column::DisplayName: return p.DisplayName.empty() ? p.Name : p.DisplayName;
		case Column::Name: return p.Name;
		case Column::Version: return Packages::VersionText(p.Version);
		case Column::Architecture: return Packages::ArchitectureText(p.Architecture);
		case Column::Kind: return Packages::KindText(p) + std::wstring(p.DevelopmentMode ? L" (development)" : L"");
		case Column::Publisher: return p.PublisherDisplayName.empty() ? p.Publisher : p.PublisherDisplayName;
		case Column::Signature: return Packages::SignatureText(p.Signature);
		case Column::Status: return Packages::ProblemsText(p.Problems);
		case Column::Installed: return p.InstalledDate ? FormatHelper::FormatDateTime(p.InstalledDate) : L"";
		case Column::Processes:
		{
			auto processes = GetProcesses(p);
			return processes ? std::to_wstring(processes->size()) : L"";
		}
		case Column::Location: return p.InstalledPath;
		case Column::Dependencies:
		{
			std::wstring text;
			for (auto& d : p.Dependencies)
				text += (text.empty() ? L"" : L", ") + NameOf(d);
			return text;
		}
		case Column::FullName: return p.FullName;
	}
	return L"";
}

void CPackagesDlg::ShowRows() {
	bool frameworks = IsDlgButtonChecked(IDC_PK_FRAMEWORKS) == BST_CHECKED;
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int row = 0, main = 0, running = 0, problems = 0;
	for (int i = 0; i < (int)m_Packages.size(); i++) {
		auto& p = m_Packages[i];
		bool isMain = !p.Framework && !p.Resource;
		if (isMain)
			main++;
		if (GetProcesses(p))
			running++;
		if (p.Problems != PackageProblem::None)
			problems++;
		if (!frameworks && !isMain)
			continue;
		if (!m_Filter.empty()) {
			auto text = p.DisplayName + L"\n" + p.Name + L"\n" + p.PublisherDisplayName + L"\n" + p.Publisher + L"\n" + p.FullName + L"\n" + p.InstalledPath;
			::CharLowerBuff(text.data(), (DWORD)text.size());
			if (text.find(m_Filter) == std::wstring::npos)
				continue;
		}
		m_List.InsertItem(row, GetText(p, Column::DisplayName).c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(row, c, GetText(p, (Column)c).c_str());
		m_List.SetItemData(row, i);
		row++;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	m_List.SetRedraw(TRUE);

	std::wstring status;
	if (m_Error)
		status = L"Can't read the packages: " + Helpers::GetErrorText(m_Error);
	else {
		status = std::format(L"{} packages{} ({} main, {} frameworks and resources), {} running", m_Packages.size(),
			m_AllUsersShown ? L" of all users" : L"", main, m_Packages.size() - main, running);
		if (problems)
			status += std::format(L", {} with problems", problems);
		if (row != (int)m_Packages.size())
			status += std::format(L"; {} shown", row);
	}
	SetDlgItemText(IDC_PK_STATUS, status.c_str());
}

int CALLBACK CPackagesDlg::Compare(LPARAM index1, LPARAM index2, LPARAM param) {
	auto self = reinterpret_cast<CPackagesDlg*>(param);
	auto& a = self->m_Packages[index1];
	auto& b = self->m_Packages[index2];
	auto compare = [](auto const& x, auto const& y) { return x < y ? -1 : y < x ? 1 : 0; };
	auto column = (Column)self->m_SortColumn;
	int result;
	switch (column) {
		case Column::Version: result = compare(a.Version, b.Version); break;
		case Column::Installed: result = compare(a.InstalledDate, b.InstalledDate); break;
		case Column::Processes:
		{
			auto pa = self->GetProcesses(a), pb = self->GetProcesses(b);
			result = compare(pa ? pa->size() : 0, pb ? pb->size() : 0);
			break;
		}
		default: result = ::_wcsicmp(self->GetText(a, column).c_str(), self->GetText(b, column).c_str()); break;
	}
	if (result == 0)
		result = ::_wcsicmp(a.FullName.c_str(), b.FullName.c_str());
	return self->m_SortAscending ? result : -result;
}

LRESULT CPackagesDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		// the newest and the busiest first
		m_SortAscending = (Column)m_SortColumn != Column::Installed && (Column)m_SortColumn != Column::Processes;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	return 0;
}

LRESULT CPackagesDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (size_t)cd->nmcd.lItemlParam;
			if (index < m_Packages.size() && m_Packages[index].Problems != PackageProblem::None) {
				bool dark = WTLHelper::IsDarkMode();
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

PackageInfo const* CPackagesDlg::GetSelected() const {
	int row = m_List.GetNextItem(-1, LVNI_FOCUSED | LVNI_SELECTED);
	if (row < 0)
		row = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (row < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(row);
	return index < m_Packages.size() ? &m_Packages[index] : nullptr;
}

LRESULT CPackagesDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the focused row
		RECT rc{};
		int row = m_List.GetNextItem(-1, LVNI_FOCUSED);
		if (row >= 0 && m_List.GetItemRect(row, &rc, LVIR_LABEL))
			pt = { rc.left, rc.bottom };
		m_List.ClientToScreen(&pt);
	}
	auto p = GetSelected();
	auto processes = p ? GetProcesses(*p) : nullptr;
	// the package's applications (none for frameworks and resources, nor for another user's package): one Launch
	// item, or a submenu of them by their app IDs; their items are returned (TPM_RETURNCMD), not commands
	auto apps = p ? Packages::GetApplicationIds(p->FullName) : std::vector<std::wstring>{};
	const UINT FirstApp = 0x7000;
	CMenu menu;
	menu.CreatePopupMenu();
	if (apps.size() > 1) {
		CMenu launch;
		launch.CreatePopupMenu();
		for (UINT i = 0; i < (UINT)apps.size() && i < 64; i++)
			launch.AppendMenu(MF_STRING, FirstApp + i, Packages::AppIdOf(apps[i]).c_str());
		menu.AppendMenu(MF_POPUP, launch.Detach(), L"&Launch");
	}
	else
		menu.AppendMenu(MF_STRING | (apps.empty() ? MF_GRAYED : 0), FirstApp, L"&Launch");
	menu.AppendMenu(MF_STRING | (p && !p->InstalledPath.empty() ? 0 : MF_GRAYED), ID_PACKAGES_OPENFOLDER, L"&Open Folder");
	menu.AppendMenu(MF_STRING | (processes && m_GoToProcess ? 0 : MF_GRAYED), ID_PACKAGES_GOTOPROCESS, L"&Go to Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_PK_COPY, m_List.GetSelectedCount() ? L"&Copy" : L"&Copy All");
	menu.AppendMenu(MF_STRING | (p ? 0 : MF_GRAYED), ID_PACKAGES_COPYFULLNAME, L"Copy &Full Name");
	auto id = (UINT)::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd, nullptr);
	if (id >= FirstApp && id - FirstApp < apps.size())
		Launch(apps[id - FirstApp]);
	else if (id)
		PostMessage(WM_COMMAND, id);
	return 0;
}

void CPackagesDlg::Launch(std::wstring const& appUserModelId) {
	CWaitCursor wait;
	auto pid = Packages::Launch(appUserModelId);
	if (!pid) {
		auto error = ::GetLastError();
		AtlMessageBox(m_hWnd, std::format(L"Can't launch {}: {}", appUserModelId, Helpers::GetErrorText(error)).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return;
	}
	SetDlgItemText(IDC_PK_STATUS, std::format(L"Launched {} (PID {})", appUserModelId, pid).c_str());
}

LRESULT CPackagesDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnOpenFolder(0, 0, nullptr, handled);
}

LRESULT CPackagesDlg::OnOpenFolder(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto p = GetSelected();
	if (!p || p->InstalledPath.empty())
		return 0;
	if ((INT_PTR)::ShellExecute(m_hWnd, L"open", p->InstalledPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
		AtlMessageBox(m_hWnd, std::format(L"Can't open {}: {}", p->InstalledPath, Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
	return 0;
}

LRESULT CPackagesDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto p = GetSelected();
	auto processes = p ? GetProcesses(*p) : nullptr;
	if (!processes || processes->empty() || !m_GoToProcess)
		return 0;
	auto pid = processes->front();
	EndDialog(IDOK);
	m_GoToProcess(pid);
	return 0;
}

LRESULT CPackagesDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_PK_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CPackagesDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CPackagesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the selected rows, else every row shown; the column names first
	bool selected = m_List.GetSelectedCount() > 0;
	std::wstring text;
	for (int c = 0; c < (int)Column::_Count; c++) {
		WCHAR name[64]{};
		LVCOLUMN column{ LVCF_TEXT };
		column.pszText = name;
		column.cchTextMax = _countof(name);
		m_List.GetColumn(c, &column);
		text += (c ? L"\t" : L"") + std::wstring(name);
	}
	text += L"\r\n";
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& p = m_Packages[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(p, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CPackagesDlg::OnCopyFullName(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED))
		text += m_Packages[m_List.GetItemData(i)].FullName + L"\r\n";
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CPackagesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
