#include "pch.h"
#include "ZombieHandlesDlg.h"
#include "FormatHelper.h"
#include <WinSys/AccessMask.h>
#include <WinSys/Handles.h>
#include <WinSys/Helpers.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>

CZombieHandlesDlg::CZombieHandlesDlg(IMainFrame* frame, WinSys::ZombieProcess zombie, uint32_t selectHolder) :
	m_Frame(frame), m_Zombie(std::move(zombie)), m_SelectHolder(selectHolder) {
}

DWORD CZombieHandlesDlg::CloseHandle(WinSys::ZombieHolder const& holder) {
	// only if the value still refers to a process (it may have been closed and reused since the scan)
	wil::unique_handle hProcess(::OpenProcess(PROCESS_DUP_HANDLE, FALSE, holder.Pid));
	if (!hProcess || !WinSys::HandleNames::CloseInProcess(hProcess.get(), holder.Handle, L"Process"))
		return ::GetLastError();
	return ERROR_SUCCESS;
}

std::wstring CZombieHandlesDlg::ZombieName() const {
	auto slash = m_Zombie.ImagePath.rfind(L'\\');
	return slash == std::wstring::npos ? m_Zombie.ImagePath : m_Zombie.ImagePath.substr(slash + 1);
}

LRESULT CZombieHandlesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto name = ZombieName();
	SetWindowText(std::format(L"Handles to {} ({})", name, m_Zombie.Pid).c_str());

	// the zombie: who it was, when it exited, how many keep it
	std::vector<uint32_t> holders;
	for (auto& h : m_Zombie.Holders)
		if (!std::ranges::contains(holders, h.Pid))
			holders.push_back(h.Pid);
	int64_t now;
	::GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&now));
	auto info = std::format(L"{} (PID {}) exited {} ({} ago), exit code 0x{:X}.\n{}\n{} handle{} in {} process{} keep it alive.", name,
		m_Zombie.Pid, m_Zombie.ExitTime ? FormatHelper::FormatDateTime(m_Zombie.ExitTime) : L"at an unknown time",
		m_Zombie.ExitTime ? FormatHelper::FormatTimeSpan(now - m_Zombie.ExitTime) : L"?", m_Zombie.ExitCode, m_Zombie.ImagePath,
		m_Zombie.Holders.size(), m_Zombie.Holders.size() == 1 ? L"" : L"s", holders.size(), holders.size() == 1 ? L"" : L"es");
	SetDlgItemText(IDC_ZH_INFO, info.c_str());

	m_List.Attach(GetDlgItem(IDC_ZH_LIST));
	// the frame's image list, for the holders' icons; the list mustn't destroy it
	m_List.ModifyStyle(0, LVS_SHAREIMAGELISTS | LVS_NOSORTHEADER);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_List.SetImageList(m_Frame->GetImageList(), LVSIL_SMALL);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Holder", 160, LVCFMT_LEFT }, { L"Holder PID", 75, LVCFMT_RIGHT }, { L"Handle", 75, LVCFMT_RIGHT },
		{ L"Access", 85, LVCFMT_RIGHT }, { L"Rights", 260, LVCFMT_LEFT }, { L"Attributes", 120, LVCFMT_LEFT },
		{ L"Status", 160, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	// by holder (the row's first), then by value
	auto& pm = m_Frame->GetProcessManager();
	for (auto& h : m_Zombie.Holders)
		m_Rows.push_back({ h, pm.GetProcessNameById(h.Pid) });
	std::ranges::stable_sort(m_Rows, [&](auto& a, auto& b) {
		auto key = [&](Row const& r) { return std::tuple(r.Handle.Pid != m_SelectHolder, r.Handle.Pid, r.Handle.Handle); };
		return key(a) < key(b);
		});
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		auto process = pm.GetProcessById(m_Rows[i].Handle.Pid);
		m_List.InsertItem(i, m_Rows[i].HolderName.c_str(), process ? m_Frame->GetProcessIcon(*process) : -1);
		if (m_Rows[i].Handle.Pid == m_SelectHolder)
			m_List.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
	}
	m_List.SetItemState(0, LVIS_FOCUSED, LVIS_FOCUSED);
	UpdateRows();

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	m_List.SetFocus();
	return FALSE;
}

std::wstring CZombieHandlesDlg::GetText(Row const& row, Column column) const {
	auto& h = row.Handle;
	switch (column) {
		case Column::Holder: return row.HolderName;
		case Column::HolderPid: return std::to_wstring(h.Pid);
		case Column::Handle: return std::format(L"0x{:X}", h.Handle);
		case Column::Access: return std::format(L"0x{:08X}", h.Access);
		case Column::Rights: return WinSys::AccessMaskDecoder::Decode(L"Process", h.Access);
		case Column::Attributes: {
			std::wstring text;
			if (h.Attributes & 2)	// OBJ_INHERIT
				text = L"Inherit";
			if (h.Attributes & 1)	// OBJ_PROTECT_CLOSE
				text += (text.empty() ? L"" : L", ") + std::wstring(L"Protect from close");
			return text;
		}
		case Column::Status:
			return row.Closed ? L"Closed" : row.Error != ERROR_SUCCESS ? L"Failed: " + WinSys::Helpers::GetErrorText(row.Error) : L"";
	}
	return L"";
}

void CZombieHandlesDlg::UpdateRows() {
	for (int i = 0; i < (int)m_Rows.size(); i++)
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, GetText(m_Rows[i], (Column)c).c_str());
	UpdateButtons();
	UpdateStatus();
}

void CZombieHandlesDlg::UpdateStatus() {
	auto open = std::ranges::count_if(m_Rows, [](auto& r) { return !r.Closed; });
	auto closed = (int64_t)m_Rows.size() - open;
	auto failed = std::ranges::count_if(m_Rows, [](auto& r) { return !r.Closed && r.Error != ERROR_SUCCESS; });
	std::wstring text = std::format(L"{} open", open);
	if (closed)
		text += std::format(L", {} closed", closed);
	if (failed)
		text += std::format(L", {} failed to close", failed);
	if (!open)
		text += L": the zombie is released once the last reference to it goes";
	SetDlgItemText(IDC_ZH_STATUS, text.c_str());
}

std::vector<int> CZombieHandlesDlg::GetSelectedRows() const {
	// a multi-select list: WTL's GetSelectedIndex asserts on one
	std::vector<int> rows;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED))
		rows.push_back(i);
	return rows;
}

void CZombieHandlesDlg::UpdateButtons() {
	auto selected = GetSelectedRows();
	GetDlgItem(IDC_ZH_CLOSE).EnableWindow(std::ranges::any_of(selected, [&](int i) { return !m_Rows[i].Closed; }));
	GetDlgItem(IDC_ZH_CLOSEALL).EnableWindow(std::ranges::any_of(m_Rows, [](auto& r) { return !r.Closed; }));
}

LRESULT CZombieHandlesDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if ((lv->uChanged & LVIF_STATE) && ((lv->uOldState ^ lv->uNewState) & LVIS_SELECTED))
		UpdateButtons();
	return 0;
}

LRESULT CZombieHandlesDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// closed handles grayed, failures red
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT: {
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (row >= m_Rows.size())
				break;
			bool dark = WTLHelper::IsDarkMode();
			if (m_Rows[row].Closed)
				cd->clrText = dark ? RGB(128, 128, 128) : RGB(150, 150, 150);
			else if (m_Rows[row].Error != ERROR_SUCCESS)
				cd->clrText = dark ? RGB(255, 110, 110) : RGB(200, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

void CZombieHandlesDlg::CloseRows(std::vector<int> const& rows) {
	std::vector<int> open;
	for (int i : rows)
		if (!m_Rows[i].Closed)
			open.push_back(i);
	if (open.empty())
		return;

	std::wstring list;
	const size_t listed = 12;
	for (size_t i = 0; i < open.size() && i < listed; i++) {
		auto& r = m_Rows[open[i]];
		list += std::format(L"\n  0x{:X} in {} ({})", r.Handle.Handle, r.HolderName, r.Handle.Pid);
	}
	if (open.size() > listed)
		list += std::format(L"\n  ...and {} more", open.size() - listed);
	auto text = std::format(L"Close {} handle{} to {} ({})?\n{}\n\nThe holders may fail if they still use them.", open.size(),
		open.size() == 1 ? L"" : L"s", ZombieName(), m_Zombie.Pid, list);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return;

	CWaitCursor wait;
	for (int i : open) {
		auto& r = m_Rows[i];
		r.Error = CloseHandle(r.Handle);
		r.Closed = r.Error == ERROR_SUCCESS;
		m_Closed |= r.Closed;
	}
	UpdateRows();
	m_List.RedrawItems(0, (int)m_Rows.size() - 1);
}

LRESULT CZombieHandlesDlg::OnCloseSelected(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CloseRows(GetSelectedRows());
	return 0;
}

LRESULT CZombieHandlesDlg::OnCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::vector<int> all((int)m_Rows.size());
	for (int i = 0; i < (int)all.size(); i++)
		all[i] = i;
	CloseRows(all);
	return 0;
}

LRESULT CZombieHandlesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto rows = GetSelectedRows();
	if (rows.empty())
		for (int i = 0; i < (int)m_Rows.size(); i++)
			rows.push_back(i);
	std::wstring text;
	for (int i : rows) {
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(m_Rows[i], (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CZombieHandlesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
