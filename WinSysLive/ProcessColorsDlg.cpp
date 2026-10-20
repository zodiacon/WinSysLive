#include "pch.h"
#include "ProcessColorsDlg.h"

namespace {
	const int Rows = IDC_PCOLOR_ENABLE8 - IDC_PCOLOR_ENABLE1 + 1;
}

CProcessColorsDlg::CProcessColorsDlg(std::vector<ProcessColors::Entry> entries) : m_Entries(std::move(entries)), m_Defaults(ProcessColors::Defaults()) {
	ATLASSERT(m_Entries.size() == Rows);
}

std::vector<ProcessColors::Entry> const& CProcessColorsDlg::GetEntries() const {
	return m_Entries;
}

LRESULT CProcessColorsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	ShowEntries();
	CenterWindow(GetParent());
	return TRUE;
}

void CProcessColorsDlg::ShowEntries() {
	for (int i = 0; i < Rows; i++) {
		CheckDlgButton(IDC_PCOLOR_ENABLE1 + i, m_Entries[i].Enabled ? BST_CHECKED : BST_UNCHECKED);
		GetDlgItem(IDC_PCOLOR_LIGHT1 + i).Invalidate();
		GetDlgItem(IDC_PCOLOR_DARK1 + i).Invalidate();
		UpdateDefaultButton(i);
	}
}

void CProcessColorsDlg::UpdateDefaultButton(int row) {
	auto& e = m_Entries[row];
	auto& def = m_Defaults[row];
	GetDlgItem(IDC_PCOLOR_DEFAULT1 + row).EnableWindow(e.Light != def.Light || e.Dark != def.Dark);
}

bool CProcessColorsDlg::IsDarkSwatch(UINT id) {
	return id >= IDC_PCOLOR_DARK1 && id <= IDC_PCOLOR_DARK8;
}

COLORREF* CProcessColorsDlg::ColorOf(UINT id) {
	if (id >= IDC_PCOLOR_LIGHT1 && id <= IDC_PCOLOR_LIGHT8)
		return &m_Entries[id - IDC_PCOLOR_LIGHT1].Light;
	if (IsDarkSwatch(id))
		return &m_Entries[id - IDC_PCOLOR_DARK1].Dark;
	return nullptr;
}

LRESULT CProcessColorsDlg::OnDrawItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	auto dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
	auto color = ColorOf(dis->CtlID);
	if (!color) {
		bHandled = FALSE;
		return 0;
	}

	CDCHandle dc(dis->hDC);
	CRect rc(dis->rcItem);
	// the border in the dialog's colors (GetSysColor follows the dark mode)
	dc.FillSolidRect(&rc, ::GetSysColor(COLOR_BTNSHADOW));
	rc.DeflateRect(1, 1);
	if (dis->itemState & ODS_SELECTED)
		rc.DeflateRect(1, 1);
	dc.FillSolidRect(&rc, *color);

	// the text as the Processes view draws it on that color
	auto oldFont = dc.SelectFont(CWindow(dis->hwndItem).GetFont());
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(IsDarkSwatch(dis->CtlID) ? RGB(255, 255, 255) : RGB(0, 0, 0));
	dc.DrawText(L"Sample", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc.SelectFont(oldFont);

	if (dis->itemState & ODS_FOCUS) {
		rc.DeflateRect(2, 2);
		dc.DrawFocusRect(&rc);
	}
	return TRUE;
}

LRESULT CProcessColorsDlg::OnChooseColor(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto color = ColorOf(wID);
	// WTL keeps the dialog's custom colors for the session
	CColorDialog dlg(*color, CC_FULLOPEN, m_hWnd);
	if (dlg.DoModal(m_hWnd) == IDOK) {
		*color = dlg.GetColor();
		GetDlgItem(wID).Invalidate();
		UpdateDefaultButton(IsDarkSwatch(wID) ? wID - IDC_PCOLOR_DARK1 : wID - IDC_PCOLOR_LIGHT1);
	}
	return 0;
}

LRESULT CProcessColorsDlg::OnDefaultColors(WORD /*wNotifyCode*/, WORD wID, HWND hWndCtl, BOOL& /*bHandled*/) {
	// the colors only: the check box stays as the user set it
	int row = wID - IDC_PCOLOR_DEFAULT1;
	m_Entries[row].Light = m_Defaults[row].Light;
	m_Entries[row].Dark = m_Defaults[row].Dark;
	GetDlgItem(IDC_PCOLOR_LIGHT1 + row).Invalidate();
	GetDlgItem(IDC_PCOLOR_DARK1 + row).Invalidate();
	// the button is about to be disabled, so the focus moves on rather than getting lost
	if (::GetFocus() == hWndCtl)
		NextDlgCtrl();
	UpdateDefaultButton(row);
	return 0;
}

LRESULT CProcessColorsDlg::OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Entries = m_Defaults;
	ShowEntries();
	return 0;
}

LRESULT CProcessColorsDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK)
		for (int i = 0; i < Rows; i++)
			m_Entries[i].Enabled = IsDlgButtonChecked(IDC_PCOLOR_ENABLE1 + i) == BST_CHECKED;
	EndDialog(wID);
	return 0;
}
