#include "pch.h"
#include "UpdateIntervalDlg.h"
#include "IntervalText.h"

namespace {
	std::wstring const Hint = std::format(L"From {} to {} seconds.", IntervalText::Seconds(IntervalText::MinMilliseconds),
		IntervalText::Seconds(IntervalText::MaxMilliseconds));
}

LRESULT CUpdateIntervalDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetDlgItemText(IDC_UI_SECONDS, IntervalText::Seconds(m_Interval).c_str());
	SetDlgItemText(IDC_UI_STATUS, Hint.c_str());
	CEdit edit(GetDlgItem(IDC_UI_SECONDS));
	edit.SetSel(0, -1);
	edit.SetFocus();
	CenterWindow(GetParent());
	return FALSE;	// the focus is set
}

LRESULT CUpdateIntervalDlg::OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SetDlgItemText(IDC_UI_STATUS, Hint.c_str());
	return 0;
}

LRESULT CUpdateIntervalDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		CString text;
		GetDlgItemText(IDC_UI_SECONDS, text);
		auto interval = IntervalText::ParseSeconds((PCWSTR)text);
		if (!interval) {
			SetDlgItemText(IDC_UI_STATUS, (L"Not an interval. " + Hint).c_str());
			CEdit edit(GetDlgItem(IDC_UI_SECONDS));
			edit.SetFocus();
			edit.SetSel(0, -1);
			::MessageBeep(MB_ICONWARNING);
			return 0;
		}
		m_Interval = *interval;
	}
	EndDialog(wID);
	return 0;
}
