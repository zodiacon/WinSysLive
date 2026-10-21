#include "pch.h"
#include "AffinityDlg.h"

CAffinityDlg::CAffinityDlg(std::wstring title, DWORD_PTR mask, DWORD_PTR systemMask, bool thread) :
	m_Title(std::move(title)), m_Mask(mask), m_SystemMask(systemMask), m_Thread(thread) {
}

DWORD_PTR CAffinityDlg::GetMask() const {
	return m_Mask;
}

LRESULT CAffinityDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetWindowText(std::format(L"Processor Affinity: {}", m_Title).c_str());
	if (m_Thread)
		SetDlgItemText(IDC_AFF_LABEL, L"The processors the thread may run on:");

	// the check boxes fill the template's (hidden) area in rows of eight; the dialog grows to fit them
	CRect area;
	GetDlgItem(IDC_AFF_AREA).GetWindowRect(&area);
	ScreenToClient(&area);
	CRect cell(0, 0, 44, 13);
	MapDialogRect(&cell);
	const int perRow = 8;
	int count = 0;
	auto font = GetFont();
	for (int cpu = 0; cpu < (int)sizeof(DWORD_PTR) * 8; cpu++) {
		if ((m_SystemMask & ((DWORD_PTR)1 << cpu)) == 0)
			continue;
		int x = area.left + (count % perRow) * cell.Width(), y = area.top + (count / perRow) * cell.Height();
		CRect rc(x, y, x + cell.Width() - 4, y + cell.Height() - 2);
		CButton check;
		check.Create(m_hWnd, rc, std::format(L"CPU {}", cpu).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, FirstCheckId + cpu);
		check.SetFont(font);
		check.SetCheck((m_Mask & ((DWORD_PTR)1 << cpu)) ? BST_CHECKED : BST_UNCHECKED);
		count++;
	}
	int columns = (std::min)(count, perRow), rows = (count + perRow - 1) / perRow;
	int dx = (std::max)(0, (int)(area.left + columns * cell.Width() - area.right));
	int dy = area.top + rows * cell.Height() - area.bottom;

	// the buttons move below the grid (OK and Cancel to the right), and the dialog takes the difference
	for (UINT id : { IDC_AFF_ALL, IDC_AFF_NONE, IDOK, IDCANCEL }) {
		CWindow button(GetDlgItem(id));
		CRect rc;
		button.GetWindowRect(&rc);
		ScreenToClient(&rc);
		rc.OffsetRect(id == IDOK || id == IDCANCEL ? dx : 0, dy);
		button.SetWindowPos(nullptr, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	CRect rc;
	GetWindowRect(&rc);
	SetWindowPos(nullptr, 0, 0, rc.Width() + dx, rc.Height() + dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	UpdateOK();
	CenterWindow(GetParent());
	return TRUE;
}

DWORD_PTR CAffinityDlg::ReadMask() const {
	DWORD_PTR mask = 0;
	for (int cpu = 0; cpu < (int)sizeof(DWORD_PTR) * 8; cpu++)
		if ((m_SystemMask & ((DWORD_PTR)1 << cpu)) && IsDlgButtonChecked(FirstCheckId + cpu) == BST_CHECKED)
			mask |= (DWORD_PTR)1 << cpu;
	return mask;
}

void CAffinityDlg::UpdateOK() {
	GetDlgItem(IDOK).EnableWindow(ReadMask() != 0);
}

LRESULT CAffinityDlg::OnSelect(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	for (int cpu = 0; cpu < (int)sizeof(DWORD_PTR) * 8; cpu++)
		if (m_SystemMask & ((DWORD_PTR)1 << cpu))
			CheckDlgButton(FirstCheckId + cpu, wID == IDC_AFF_ALL ? BST_CHECKED : BST_UNCHECKED);
	UpdateOK();
	return 0;
}

LRESULT CAffinityDlg::OnCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	UpdateOK();
	return 0;
}

LRESULT CAffinityDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK)
		m_Mask = ReadMask();
	EndDialog(wID);
	return 0;
}
