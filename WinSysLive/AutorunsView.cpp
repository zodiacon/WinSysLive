#include "pch.h"
#include "AutorunsView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include "VersionInfo.h"
#include "Signature.h"
#include "VirusTotalDlg.h"
#include <WinSys/SecurityHelper.h>
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <ShlObj.h>

using namespace WinSys;

namespace {
	// in CAutorunsView::Column order; the keys are stored in the AutorunsColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Entry", L"Entry", 220, LVCFMT_LEFT, true, true },
		{ L"Category", L"Category", 100, LVCFMT_LEFT, true },
		{ L"Enabled", L"Enabled", 60, LVCFMT_LEFT, true },
		{ L"Description", L"Description", 220, LVCFMT_LEFT, true },
		{ L"Company", L"Company", 160, LVCFMT_LEFT, true },
		{ L"Version", L"Version", 120, LVCFMT_LEFT, false },
		{ L"Signature", L"Signature", 240, LVCFMT_LEFT, true },
		{ L"VirusTotal", L"VirusTotal", 90, LVCFMT_RIGHT, false },
		{ L"Image Path", L"ImagePath", 320, LVCFMT_LEFT, true },
		{ L"Command", L"Command", 320, LVCFMT_LEFT, true },
		{ L"Location", L"Location", 320, LVCFMT_LEFT, true },
		{ L"Details", L"Details", 220, LVCFMT_LEFT, false },
		{ L"Modified", L"Modified", 140, LVCFMT_LEFT, true },
		{ L"Task State", L"TaskState", 80, LVCFMT_LEFT, false },
		{ L"Triggers", L"Triggers", 160, LVCFMT_LEFT, false },
		{ L"Last Run", L"LastRun", 140, LVCFMT_LEFT, false },
		{ L"Last Result", L"LastResult", 90, LVCFMT_LEFT, false },
		{ L"Next Run", L"NextRun", 140, LVCFMT_LEFT, false },
		{ L"Run As", L"RunAs", 160, LVCFMT_LEFT, false },
		{ L"Author", L"Author", 160, LVCFMT_LEFT, false },
	};

	int64_t Now() {
		FILETIME ft;
		::GetSystemTimeAsFileTime(&ft);
		return ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	}

	bool IsExe(std::wstring const& path) {
		return path.size() > 4 && ::_wcsicmp(path.c_str() + path.size() - 4, L".exe") == 0;
	}

	PCWSTR RootFullName(void* root) {
		if (root == HKEY_LOCAL_MACHINE)
			return L"HKEY_LOCAL_MACHINE";
		if (root == HKEY_CURRENT_USER)
			return L"HKEY_CURRENT_USER";
		if (root == HKEY_CLASSES_ROOT)
			return L"HKEY_CLASSES_ROOT";
		return L"HKEY_USERS";
	}

	// what deleting it removes, for the confirmation
	std::wstring DeleteWhat(AutorunEntry const& e) {
		switch (e.Store) {
			case AutorunStore::RegistryValue: return std::format(L"the value \"{}\" of {}", e.ValueName, e.Location);
			case AutorunStore::StartupFile: return std::format(L"the file {}\\{}", e.Location, e.Name);
			case AutorunStore::Task: return std::format(L"the scheduled task {}", e.TaskPath);
		}
		return L"";
	}
}

CString CAutorunsView::GetTitle() const {
	return L"Autoruns";
}

LRESULT CAutorunsView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);
	m_Signatures.Init(m_hWnd, WM_AUTORUNS_SIGNED);
	VirusTotalChecker::Get().AddListener(m_hWnd);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	auto& settings = AppSettings::Get();
	SetColumns(ColumnLayout::Parse(settings.AutorunsColumns(), s_Columns));
	m_HideMicrosoft = settings.AutorunsHideMicrosoft() != 0;

	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 360, L"Filter: entry, category, path, command or company (Ctrl+F)",
		m_List, [this](auto&) {
			ShowRows(GetSelectedKey());
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	return 0;
}

LRESULT CAutorunsView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	VirusTotalChecker::Get().RemoveListener(m_hWnd);
	// a scan takes under a second; its result goes to members that are about to go
	if (m_Worker.joinable())
		m_Worker.join();
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

void CAutorunsView::OnActivate(bool active) {
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		if (!m_LastScan || ::GetTickCount64() - m_LastScan >= RescanAge)
			StartScan();
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CAutorunsView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CAutorunsView::StartScan() {
	if (m_Scanning)
		return;
	m_Scanning = true;
	m_LastScan = ::GetTickCount64();
	// the previous scan is done (m_Scanning was off): joining it doesn't wait
	m_Worker = std::jthread([this, hWnd = m_hWnd] {
		// the Task Scheduler and the startup folders' shortcuts are COM
		::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		auto entries = Autoruns::Enum();
		::CoUninitialize();
		{
			std::lock_guard lock(m_ResultLock);
			m_Result = std::move(entries);
		}
		::PostMessage(hWnd, WM_AUTORUNS_SCANNED, 0, 0);
		});
	UpdateStatus();
}

std::wstring CAutorunsView::KeyOf(AutorunEntry const& e) {
	return std::format(L"{}|{}|{}|{}", (int)e.Category, e.Location, e.Name, e.Command);
}

LRESULT CAutorunsView::OnScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Scanning = false;
	std::optional<std::vector<AutorunEntry>> result;
	{
		std::lock_guard lock(m_ResultLock);
		result = std::move(m_Result);
	}
	if (!result)
		return 0;
	// the rows point into m_All, which is replaced: the selection is taken first
	auto selected = GetSelectedKey();
	m_Rows.clear();
	m_All.clear();
	m_All.reserve(result->size());
	for (auto& e : *result) {
		auto key = KeyOf(e);
		m_All.push_back({ std::move(e), std::move(key) });
	}
	m_ScanTime = Now();
	for (auto& row : m_All)
		if (row.Entry.ImageFound)
			m_Signatures.Verify(row.Entry.ImagePath);
	ShowRows(selected);
	UpdateStatus();
	return 0;
}

LRESULT CAutorunsView::OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_AUTORUNS_SIGNED, WM_AUTORUNS_SIGNED, PM_REMOVE))
		;
	// hidden Microsoft entries go as they're verified
	if (m_HideMicrosoft) {
		ShowRows(GetSelectedKey());
		UpdateStatus();
	}
	else {
		if (auto si = GetSortInfo(m_List); si && GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn) == Column::Signature)
			DoSort(si);
		m_List.Invalidate();
	}
	return 0;
}

LRESULT CAutorunsView::OnVirusTotal(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, VirusTotalChecker::WM_VIRUSTOTAL, VirusTotalChecker::WM_VIRUSTOTAL, PM_REMOVE))
		;
	m_List.Invalidate();
	return 0;
}

std::wstring CAutorunsView::GetSignatureText(AutorunEntry const& e) const {
	if (!e.ImageFound)
		return L"";
	if (auto info = m_Signatures.GetResult(e.ImagePath))
		return FormatSignature(*info);
	return L"Verifying...";
}

bool CAutorunsView::IsMicrosoft(AutorunEntry const& e) const {
	if (!e.ImageFound)
		return false;
	auto info = m_Signatures.GetResult(e.ImagePath);
	return info && info->Status == SignatureStatus::Valid && CFilterBox::Contains(info->Signer, L"microsoft");
}

bool CAutorunsView::Matches(Row const& row) const {
	if (m_HideMicrosoft && IsMicrosoft(row.Entry))
		return false;
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto column : { Column::Entry, Column::Category, Column::ImagePath, Column::Command, Column::Location, Column::Description,
		Column::Company, Column::Details })
		if (CFilterBox::Contains(GetText(row, column), filter))
			return true;
	return false;
}

void CAutorunsView::ShowRows(std::wstring const& selected) {
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

void CAutorunsView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (m_Scanning && m_All.empty())
		text = L"Scanning...";
	else {
		text = std::format(L"{} entries", m_All.size());
		if (m_HideMicrosoft) {
			auto hidden = std::ranges::count_if(m_All, [&](auto& row) { return IsMicrosoft(row.Entry); });
			text += std::format(L"; {} Microsoft entries hidden", hidden);
		}
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
		if (m_Scanning)
			text += L"; scanning again...";
		else if (m_ScanTime)
			text += L"; scanned at " + FormatHelper::FormatDateTime(m_ScanTime);
	}
	if (!SecurityHelper::IsRunningElevated())
		text += L" (some tasks and changes need elevation)";
	Frame()->SetStatusText(0, text.c_str());
}

std::wstring CAutorunsView::GetText(Row const& row, Column column) const {
	auto& e = row.Entry;
	switch (column) {
		case Column::Entry: return e.Name;
		case Column::Category: return Autoruns::CategoryName(e.Category);
		case Column::Enabled: return e.Enabled ? (*e.Enabled ? L"Yes" : L"No") : L"";
		case Column::Description: return e.ImageFound ? GetCachedVersionInfo(e.ImagePath).Description : L"";
		case Column::Company: return e.ImageFound ? GetCachedVersionInfo(e.ImagePath).Company : L"";
		case Column::Version: return e.ImageFound ? GetCachedVersionInfo(e.ImagePath).Version : L"";
		case Column::Signature: return GetSignatureText(e);
		case Column::VirusTotal: return e.ImageFound ? VirusTotalChecker::Text(VirusTotalChecker::Get().Query(e.ImagePath)) : L"";
		case Column::ImagePath:
			if (e.ImagePath.empty())
				return e.Command.starts_with(L"COM handler") ? L"(COM handler not registered)" : L"";
			return e.ImageFound ? e.ImagePath : L"File not found: " + e.ImagePath;
		case Column::Command: return e.Command;
		case Column::Location: return e.Location;
		case Column::Details: return e.Details;
		case Column::Modified: return FormatHelper::FormatDateTime(e.LastModified);
		case Column::TaskState: return Autoruns::TaskStateName(e.TaskState);
		case Column::Triggers: return e.Triggers;
		case Column::LastRun: return FormatHelper::FormatDateTime(e.LastRun);
		case Column::LastResult:
			if (!e.LastResult)
				return L"";
			return *e.LastResult == 0 ? std::wstring(L"Success") : std::format(L"0x{:X}", (uint32_t)*e.LastResult);
		case Column::NextRun: return FormatHelper::FormatDateTime(e.NextRun);
		case Column::RunAs: return e.RunAs;
		case Column::Author: return e.Author;
	}
	return L"";
}

CString CAutorunsView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CAutorunsView::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || !ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	auto& e = m_Rows[row]->Entry;
	// a program's own icon; DLLs and drivers by their kind
	if (e.ImageFound && IsExe(e.ImagePath))
		return Frame()->GetProcessIcon(e.ImagePath);
	switch (e.Category) {
		case AutorunCategory::ScheduledTask: return Frame()->GetIconIndex(IDI_TIMER);
		case AutorunCategory::Service: return Frame()->GetIconIndex(IDI_SERVICES);
		case AutorunCategory::Driver: return Frame()->GetIconIndex(IDI_SYSTEM_DRIVER);
		case AutorunCategory::Lsa: case AutorunCategory::AppInit: return Frame()->GetIconIndex(IDI_MODULE_IMAGE);
	}
	return Frame()->GetIconIndex(IDI_RUN_COMMAND);
}

int CAutorunsView::GetSaveColumnRange(HWND, int&) const {
	return -1;
}

void CAutorunsView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKey();
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Row const* r1, Row const* r2) {
		auto& e1 = r1->Entry;
		auto& e2 = r2->Entry;
		int result = 0;
		switch (column) {
			case Column::Modified: result = compare(e1.LastModified, e2.LastModified); break;
			case Column::LastRun: result = compare(e1.LastRun, e2.LastRun); break;
			case Column::NextRun: result = compare(e1.NextRun, e2.NextRun); break;
			case Column::Category: result = compare(e1.Category, e2.Category); break;
			case Column::VirusTotal: {
				auto key = [](AutorunEntry const& e) { return e.ImageFound ? VirusTotalChecker::SortKey(VirusTotalChecker::Get().Query(e.ImagePath)) : -1; };
				result = compare(key(e1), key(e2));
				break;
			}
			default: result = ::_wcsicmp(GetText(*r1, column).c_str(), GetText(*r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectRow(selected);
}

CAutorunsView::Row const* CAutorunsView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

std::wstring CAutorunsView::GetSelectedKey() const {
	auto row = GetSelected();
	return row ? row->Key : L"";
}

CAutorunsView::Row* CAutorunsView::FindRow(std::wstring const& key) {
	auto it = std::ranges::find(m_All, key, &Row::Key);
	return it == m_All.end() ? nullptr : &*it;
}

void CAutorunsView::SelectRow(std::wstring const& key) {
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (key.empty())
		return;
	auto it = std::ranges::find_if(m_Rows, [&](auto row) { return row->Key == key; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

LRESULT CAutorunsView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			auto& e = m_Rows[index]->Entry;
			bool dark = WTLHelper::IsDarkMode();
			// as Autoruns: a missing image stands out; a disabled entry is grayed
			if (!e.ImagePath.empty() && !e.ImageFound) {
				cd->clrTextBk = dark ? RGB(96, 80, 0) : RGB(255, 240, 150);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (e.Enabled && !*e.Enabled)
				cd->clrText = dark ? RGB(128, 128, 128) : RGB(140, 140, 140);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CAutorunsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	CMenu menu;
	menu.CreatePopupMenu();
	if (row >= 0 && row < (int)m_Rows.size()) {
		auto& e = m_Rows[row]->Entry;
		menu.AppendMenu(MF_STRING | (e.Enabled ? 0 : MF_GRAYED) | (e.Enabled.value_or(true) ? MF_CHECKED : 0), ID_AUTORUNS_ENABLE, L"&Enabled");
		menu.AppendMenu(MF_STRING | (e.Deletable ? 0 : MF_GRAYED), ID_AUTORUNS_DELETE, L"&Delete...");
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_AUTORUNS_JUMP, L"&Jump to Entry");
		menu.AppendMenu(MF_STRING | (e.ImageFound ? 0 : MF_GRAYED), ID_AUTORUNS_PROPERTIES, L"&Properties...");
		menu.AppendMenu(MF_STRING | (e.ImageFound ? 0 : MF_GRAYED), ID_AUTORUNS_OPENLOCATION, L"Open File &Location");
		VirusTotalCommands::AppendMenu(menu, e.ImageFound);
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
		menu.AppendMenu(MF_SEPARATOR);
		menu.SetMenuDefaultItem(ID_AUTORUNS_PROPERTIES);
	}
	menu.AppendMenu(MF_STRING | (m_HideMicrosoft ? MF_CHECKED : 0), ID_AUTORUNS_HIDEMICROSOFT, L"&Hide Microsoft Entries");
	menu.AppendMenu(MF_STRING | (m_Scanning ? MF_GRAYED : 0), ID_AUTORUNS_RESCAN, L"&Scan Again");
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CAutorunsView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	BOOL handled;
	OnProperties(0, 0, nullptr, handled);
	return true;
}

void CAutorunsView::ReportError(PCWSTR action, DWORD error) {
	auto text = std::format(L"{} failed: {}", action, Helpers::GetErrorText(error));
	if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
		text += L"\nRun WinSysLive as administrator to change it.";
	AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

LRESULT CAutorunsView::OnEnable(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto selected = GetSelected();
	if (!selected || !selected->Entry.Enabled)
		return 0;
	auto key = selected->Key;
	bool enable = !*selected->Entry.Enabled;
	if (!Autoruns::SetEnabled(selected->Entry, enable)) {
		ReportError(enable ? L"Enabling the entry" : L"Disabling the entry", ::GetLastError());
		return 0;
	}
	if (auto row = FindRow(key)) {
		row->Entry.Enabled = enable;
		if (row->Entry.Store == AutorunStore::Task)
			row->Entry.TaskState = enable ? AutorunTaskState::Ready : AutorunTaskState::Disabled;
	}
	m_List.Invalidate();
	return 0;
}

LRESULT CAutorunsView::OnDelete(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto selected = GetSelected();
	if (!selected || !selected->Entry.Deletable)
		return 0;
	// a copy: a scan may replace the rows while the message box is up
	auto entry = selected->Entry;
	auto key = selected->Key;
	auto text = std::format(L"Delete the autorun entry \"{}\"?\n\nThis removes {}, and can't be undone.", entry.Name, DeleteWhat(entry));
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
		return 0;
	if (!Autoruns::Delete(entry)) {
		ReportError(L"Deleting the entry", ::GetLastError());
		return 0;
	}
	m_Rows.clear();
	std::erase_if(m_All, [&](auto& row) { return row.Key == key; });
	ShowRows(L"");
	UpdateStatus();
	return 0;
}

LRESULT CAutorunsView::OnJump(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto selected = GetSelected();
	if (!selected)
		return 0;
	auto& e = selected->Entry;
	switch (e.Store) {
		case AutorunStore::RegistryValue:
		case AutorunStore::Service:
		{
			// Regedit opens the key it was last on
			auto path = std::format(L"Computer\\{}\\{}", RootFullName(e.Root), e.SubKey);
			CRegKey key;
			if (key.Create(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit") == ERROR_SUCCESS)
				key.SetStringValue(L"LastKey", path.c_str());
			// -m: another instance, as a running one ignores LastKey
			::ShellExecute(m_hWnd, nullptr, L"regedit.exe", L"-m", nullptr, SW_SHOWNORMAL);
			break;
		}
		case AutorunStore::StartupFile:
			::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\\{}\"", e.Location, e.Name).c_str(), nullptr, SW_SHOWNORMAL);
			break;
		case AutorunStore::Task:
			// nothing opens the Task Scheduler on a task
			::ShellExecute(m_hWnd, nullptr, L"taskschd.msc", nullptr, nullptr, SW_SHOWNORMAL);
			Frame()->SetStatusText(0, std::format(L"The task is {}", e.TaskPath).c_str());
			break;
	}
	return 0;
}

LRESULT CAutorunsView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && row->Entry.ImageFound)
		::SHObjectProperties(m_hWnd, SHOP_FILEPATH, row->Entry.ImagePath.c_str(), nullptr);
	return 0;
}

LRESULT CAutorunsView::OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelected(); row && row->Entry.ImageFound)
		::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", row->Entry.ImagePath).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CAutorunsView::OnHideMicrosoft(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_HideMicrosoft = !m_HideMicrosoft;
	auto& settings = AppSettings::Get();
	settings.AutorunsHideMicrosoft(m_HideMicrosoft);
	settings.Save();
	ShowRows(GetSelectedKey());
	UpdateStatus();
	return 0;
}

LRESULT CAutorunsView::OnRescan(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	StartScan();
	return 0;
}

LRESULT CAutorunsView::OnVirusTotalCommand(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row || !row->Entry.ImageFound)
		return 0;
	auto path = row->Entry.ImagePath;
	switch (wID) {
		case ID_VT_REPORT: VirusTotalCommands::OpenReport(m_hWnd, path); break;
		case ID_VT_CHECK: VirusTotalCommands::CheckAgain(m_hWnd, path); break;
		case ID_VT_SUBMIT: VirusTotalCommands::Submit(m_hWnd, path); break;
	}
	return 0;
}

LRESULT CAutorunsView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelected();
	if (!row)
		return 0;
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetText(*row, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CAutorunsView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CAutorunsView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CAutorunsView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CAutorunsView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CAutorunsView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.AutorunsColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CAutorunsView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows(GetSelectedKey());
	}
	return 0;
}

LRESULT CAutorunsView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CAutorunsView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
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
