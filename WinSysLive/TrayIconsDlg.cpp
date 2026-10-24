#include "pch.h"
#include "resource.h"
#include "TrayIconsDlg.h"

CTrayIconsDlg::CTrayIconsDlg(CTrayIcons::Modes const& modes) : m_Modes(modes) {
}

LRESULT CTrayIconsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CenterWindow(GetParent());
	for (int i = 0; i < CTrayIcons::Count; i++) {
		SetDlgItemText(IDC_TRAY_SHOW + i, CTrayIcons::Info((TrayIconKind)i).Name);
		CheckDlgButton(IDC_TRAY_SHOW + i, m_Modes[i] != TrayIconMode::Off ? BST_CHECKED : BST_UNCHECKED);
	}
	return TRUE;
}

LRESULT CTrayIconsDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		for (int i = 0; i < CTrayIcons::Count; i++)
			m_Modes[i] = IsDlgButtonChecked(IDC_TRAY_SHOW + i) == BST_CHECKED ? TrayIconMode::Graph : TrayIconMode::Off;
	}
	EndDialog(wID);
	return 0;
}
