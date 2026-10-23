#include "pch.h"
#include "HighlightDlg.h"
#include "Highlight.h"

LRESULT CHighlightDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	for (auto [spin, value] : { std::pair{ IDC_HL_NEWSPIN, m_New }, std::pair{ IDC_HL_EXITEDSPIN, m_Exited } }) {
		CUpDownCtrl ctl(GetDlgItem(spin));
		ctl.SetRange(0, MaxSeconds);
		ctl.SetPos(std::clamp(value, 0, MaxSeconds));
	}
	CenterWindow(GetParent());
	UpdateTicks();
	return TRUE;
}

int CHighlightDlg::GetSeconds(int id) const {
	return std::clamp((int)GetDlgItemInt(id, nullptr, FALSE), 0, MaxSeconds);
}

void CHighlightDlg::UpdateTicks() {
	auto updates = [&](int seconds) {
		auto ticks = Highlight::ToTicks(seconds, m_Interval);
		return ticks == 0 ? std::wstring(L"none") : std::format(L"{} update{}", ticks, ticks == 1 ? L"" : L"s");
	};
	auto interval = m_Interval % 1000 ? std::format(L"{} ms", m_Interval) : std::format(L"{} s", m_Interval / 1000);
	SetDlgItemText(IDC_HL_UPDATES, std::format(L"At the update interval of {}: new, {}; exited, {}.", interval,
		updates(GetSeconds(IDC_HL_NEW)), updates(GetSeconds(IDC_HL_EXITED))).c_str());
}

LRESULT CHighlightDlg::OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// before WM_INITDIALOG's end, the spin buttons set the edits
	if (GetDlgItem(IDC_HL_UPDATES))
		UpdateTicks();
	return 0;
}

LRESULT CHighlightDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		m_New = GetSeconds(IDC_HL_NEW);
		m_Exited = GetSeconds(IDC_HL_EXITED);
	}
	EndDialog(wID);
	return 0;
}
