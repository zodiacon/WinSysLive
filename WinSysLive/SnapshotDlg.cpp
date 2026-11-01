#include "pch.h"
#include "SnapshotDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	constexpr PCWSTR FileFilter = L"WinSysLive Snapshots (*.wslsnap)\0*.wslsnap\0All Files (*.*)\0*.*\0";
}

LRESULT CSnapshotDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_SNAPSHOT, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_SNAPSHOT, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_List.Attach(GetDlgItem(IDC_SN_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width;
	} columns[] = {
		{ L"Kind", 70 }, { L"Change", 70 }, { L"Name", 200 }, { L"Details", 420 }, { L"Path", 320 },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, LVCFMT_LEFT, ::MulDiv(columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	::SendDlgItemMessage(m_hWnd, IDC_SN_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter");
	::SendDlgItemMessage(m_hWnd, IDC_SN_BEFORE, EM_SETCUEBANNER, TRUE, (LPARAM)L"None: the list shows After's contents");

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	UpdateFields();
	EnableButtons();
	SetDlgItemText(IDC_SN_STATUS, L"Save a snapshot now to compare the system with it later, or open snapshots to compare.");
	return TRUE;
}

std::wstring CSnapshotDlg::Describe(Snapshot const& s) {
	return std::format(L"{}, {}", s.Computer, FormatHelper::FormatDateTime(s.Time));
}

void CSnapshotDlg::UpdateFields() {
	SetDlgItemText(IDC_SN_BEFORE, m_BeforePath.c_str());
	SetDlgItemText(IDC_SN_AFTER, m_AfterPath.empty() ? L"(the system now)" : m_AfterPath.c_str());
}

void CSnapshotDlg::EnableButtons() {
	for (auto id : { IDC_SN_COMPARE, IDC_SN_SAVE, IDC_SN_BEFORE_OPEN, IDC_SN_AFTER_OPEN, IDC_SN_BEFORE_CLEAR, IDC_SN_AFTER_NOW })
		GetDlgItem(id).EnableWindow(!m_Capturing);
	GetDlgItem(IDC_SN_BEFORE_CLEAR).EnableWindow(!m_Capturing && !m_BeforePath.empty());
	GetDlgItem(IDC_SN_AFTER_NOW).EnableWindow(!m_Capturing && !m_AfterPath.empty());
}

void CSnapshotDlg::StartCapture(Purpose purpose) {
	if (m_Capturing)
		return;
	m_Capturing = true;
	m_Purpose = purpose;
	EnableButtons();
	SetDlgItemText(IDC_SN_STATUS, L"Taking a snapshot of the system...");
	// the thread holds the state, not the dialog, so closing needn't wait for it
	m_Capture = std::make_shared<CaptureState>();
	std::thread([state = m_Capture, hWnd = m_hWnd] {
		// the Task Scheduler and the startup folders' shortcuts (Autoruns) are COM
		::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		auto snapshot = Snapshot::Capture();
		::CoUninitialize();
		{
			std::lock_guard lock(state->Lock);
			state->Result = std::move(snapshot);
		}
		if (::IsWindow(hWnd))
			::PostMessage(hWnd, WM_SNAPSHOT_CAPTURED, 0, 0);
		}).detach();
}

LRESULT CSnapshotDlg::OnCaptured(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Capturing = false;
	std::optional<Snapshot> snapshot;
	if (m_Capture) {
		std::lock_guard lock(m_Capture->Lock);
		snapshot = std::move(m_Capture->Result);
	}
	m_Capture.reset();
	EnableButtons();
	if (!snapshot)
		return 0;

	if (m_Purpose == Purpose::Save) {
		if (!snapshot->Save(m_SavePath)) {
			auto error = ::GetLastError();
			SetDlgItemText(IDC_SN_STATUS, L"");
			AtlMessageBox(m_hWnd, std::format(L"Failed to write {}: {}", m_SavePath, WinSys::Helpers::GetErrorText(error)).c_str(),
				IDR_MAINFRAME, MB_ICONERROR);
			return 0;
		}
		// ready for a later comparison against the system
		m_BeforePath = m_SavePath;
		m_Before = std::move(snapshot);
		m_AfterPath.clear();
		m_After.reset();
		UpdateFields();
		EnableButtons();
		SetDlgItemText(IDC_SN_STATUS, std::format(L"Saved: {} processes, {} services, {} drivers, {} autoruns. It's Before now: Compare shows what changed since.",
			m_Before->Processes.size(), m_Before->Services.size(), m_Before->Drivers.size(), m_Before->Autoruns.size()).c_str());
		return 0;
	}
	m_After = std::move(snapshot);
	ShowComparison();
	return 0;
}

LRESULT CSnapshotDlg::OnOpen(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SuspendResumeHook sr;
	CSimpleFileDialog dlg(TRUE, L"wslsnap", nullptr, OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER, FileFilter, m_hWnd);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	std::wstring path = dlg.m_szFileName;
	std::wstring error;
	auto snapshot = Snapshot::Load(path, error);
	if (!snapshot) {
		AtlMessageBox(m_hWnd, std::format(L"{}\n\n{}", path, error).c_str(), IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	if (wID == IDC_SN_BEFORE_OPEN) {
		m_BeforePath = path;
		m_Before = std::move(snapshot);
	}
	else {
		m_AfterPath = path;
		m_After = std::move(snapshot);
	}
	UpdateFields();
	EnableButtons();
	// two files compare at once; against the system, Compare captures it
	if (!m_AfterPath.empty())
		ShowComparison();
	else
		SetDlgItemText(IDC_SN_STATUS, std::format(L"Before: {}. Compare captures the system and shows what changed.", Describe(*m_Before)).c_str());
	return 0;
}

LRESULT CSnapshotDlg::OnClearBefore(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_BeforePath.clear();
	m_Before.reset();
	UpdateFields();
	EnableButtons();
	if (m_After)
		ShowComparison();
	return 0;
}

LRESULT CSnapshotDlg::OnAfterNow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_AfterPath.clear();
	m_After.reset();
	UpdateFields();
	EnableButtons();
	return 0;
}

LRESULT CSnapshotDlg::OnCompare(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the system as it is now, every time
	if (m_AfterPath.empty())
		StartCapture(Purpose::Compare);
	else
		ShowComparison();
	return 0;
}

LRESULT CSnapshotDlg::OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SYSTEMTIME st;
	::GetLocalTime(&st);
	WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD size = _countof(computer);
	if (!::GetComputerName(computer, &size))
		wcscpy_s(computer, L"Snapshot");
	auto name = std::format(L"{} {:04}-{:02}-{:02} {:02}{:02}.wslsnap", computer, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
	SuspendResumeHook sr;
	CSimpleFileDialog dlg(FALSE, L"wslsnap", name.c_str(), OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER, FileFilter, m_hWnd);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	m_SavePath = dlg.m_szFileName;
	StartCapture(Purpose::Save);
	return 0;
}

void CSnapshotDlg::ShowComparison() {
	if (!m_After)
		return;
	m_Comparison = m_Before.has_value();
	m_Rows = m_Comparison ? SnapshotCompare::Compare(*m_Before, *m_After) : SnapshotCompare::Contents(*m_After);
	// a comparison reads best grouped by kind and change
	m_SortColumn = (int)Column::Kind;
	m_SortAscending = true;
	ShowRows();
}

std::wstring CSnapshotDlg::GetText(SnapshotRow const& row, Column column) const {
	switch (column) {
		case Column::Kind: return SnapshotRow::KindName(row.Type);
		case Column::Change: return SnapshotRow::ChangeName(row.Diff);
		case Column::Name: return row.Name;
		case Column::Details: return row.Details;
		case Column::Path: return row.Path;
	}
	return L"";
}

void CSnapshotDlg::ShowRows() {
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int added = 0, removed = 0, changed = 0, shown = 0;
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		auto& r = m_Rows[i];
		switch (r.Diff) {
			case SnapshotRow::Change::Added: added++; break;
			case SnapshotRow::Change::Removed: removed++; break;
			case SnapshotRow::Change::Changed: changed++; break;
		}
		if (!m_Filter.empty()) {
			bool match = false;
			for (int c = 0; c < (int)Column::_Count && !match; c++) {
				auto text = GetText(r, (Column)c);
				::CharLowerBuff(text.data(), (DWORD)text.size());
				match = text.find(m_Filter) != std::wstring::npos;
			}
			if (!match)
				continue;
		}
		m_List.InsertItem(shown, GetText(r, Column::Kind).c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(shown, c, GetText(r, (Column)c).c_str());
		m_List.SetItemData(shown, i);
		shown++;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	m_List.SetRedraw(TRUE);

	std::wstring status;
	if (m_Comparison) {
		status = std::format(L"{} -> {}: ", Describe(*m_Before), m_AfterPath.empty() ? std::wstring(L"now") : Describe(*m_After));
		status += m_Rows.empty() ? std::wstring(L"no differences") : std::format(L"{} added, {} removed, {} changed", added, removed, changed);
	}
	else if (m_After)
		status = std::format(L"{} ({}): {} processes, {} services, {} drivers, {} autoruns", Describe(*m_After), m_After->Os,
			m_After->Processes.size(), m_After->Services.size(), m_After->Drivers.size(), m_After->Autoruns.size());
	if (!m_Filter.empty())
		status += std::format(L"; {} match the filter", shown);
	SetDlgItemText(IDC_SN_STATUS, status.c_str());
}

int CALLBACK CSnapshotDlg::Compare(LPARAM index1, LPARAM index2, LPARAM param) {
	auto self = reinterpret_cast<CSnapshotDlg*>(param);
	auto& a = self->m_Rows[index1];
	auto& b = self->m_Rows[index2];
	auto compare = [](auto const& x, auto const& y) { return x < y ? -1 : y < x ? 1 : 0; };
	int result = 0;
	switch ((Column)self->m_SortColumn) {
		case Column::Kind:
			result = compare(a.Type, b.Type);
			if (result == 0)
				result = compare(a.Diff, b.Diff);
			break;
		case Column::Change: result = compare(a.Diff, b.Diff); break;
		default: result = ::_wcsicmp(self->GetText(a, (Column)self->m_SortColumn).c_str(), self->GetText(b, (Column)self->m_SortColumn).c_str()); break;
	}
	if (result == 0)
		result = ::_wcsicmp(a.Name.c_str(), b.Name.c_str());
	return self->m_SortAscending ? result : -result;
}

LRESULT CSnapshotDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		m_SortAscending = true;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	return 0;
}

LRESULT CSnapshotDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (size_t)m_List.GetItemData((int)cd->nmcd.dwItemSpec);
			if (index >= m_Rows.size())
				break;
			bool dark = WTLHelper::IsDarkMode();
			// the other tabs' new and gone colors, and amber for a change
			switch (m_Rows[index].Diff) {
				case SnapshotRow::Change::Added: cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160); break;
				case SnapshotRow::Change::Removed: cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160); break;
				case SnapshotRow::Change::Changed: cd->clrTextBk = dark ? RGB(110, 80, 0) : RGB(255, 225, 150); break;
				default: return CDRF_DODEFAULT;
			}
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CSnapshotDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnOpenLocation(0, 0, nullptr, handled);
}

LRESULT CSnapshotDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the selected row
		CRect rc;
		if (selected < 0 || !m_List.GetItemRect(selected, &rc, LVIR_LABEL))
			return 0;
		pt = rc.CenterPoint();
		m_List.ClientToScreen(&pt);
	}
	if (selected < 0)
		return 0;
	auto& row = m_Rows[m_List.GetItemData(selected)];
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (row.Path.empty() ? MF_GRAYED : 0), ID_SNAPSHOT_OPENLOCATION, L"Open File &Location");
	menu.AppendMenu(MF_STRING, IDC_SN_COPY, L"&Copy");
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CSnapshotDlg::OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return 0;
	auto& path = m_Rows[m_List.GetItemData(selected)].Path;
	if (path.empty())
		return 0;
	if (::GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		SetDlgItemText(IDC_SN_STATUS, std::format(L"{} isn't there anymore", path).c_str());
		return 0;
	}
	::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", path).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CSnapshotDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_SN_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CSnapshotDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	bool selected = m_List.GetSelectedCount() > 0;
	std::wstring text = L"Kind\tChange\tName\tDetails\tPath\r\n";
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& r = m_Rows[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(r, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CSnapshotDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
