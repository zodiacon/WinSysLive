#include "pch.h"
#include "ProcessPages.h"
#include "FormatHelper.h"
#include <WTLHelper.h>

namespace {
	// the pages run on the sheet's thread, where WTLHelper's hook doesn't theme new dialogs
	void ApplyTheme(HWND hDlg) {
		DarkMode::setDarkWndNotifySafe(hDlg);
	}

	void SetIconId(PROPSHEETPAGE& psp, UINT icon) {
		psp.dwFlags |= PSP_USEICONID;
		psp.pszIcon = MAKEINTRESOURCE(icon);
	}
}

CProcGeneralPage::CProcGeneralPage(ProcessProperties const& props, HICON hIcon) : m_Props(props), m_hIcon(hIcon) {
	SetIconId(m_psp, IDI_PROCESS);
}

LRESULT CProcGeneralPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto& p = m_Props;
	if (m_hIcon)
		CStatic(GetDlgItem(IDC_PROC_ICON)).SetIcon(m_hIcon);
	SetDlgItemText(IDC_PROC_NAME, p.ImageName.c_str());
	SetDlgItemText(IDC_PROC_DESCRIPTION, p.Description.c_str());
	SetDlgItemText(IDC_PROC_PATH, p.ImagePath.c_str());
	SetDlgItemText(IDC_PROC_COMMANDLINE, p.CommandLine.c_str());
	SetDlgItemText(IDC_PROC_PID, std::format(L"{} (0x{:X})", p.Id, p.Id).c_str());
	SetDlgItemText(IDC_PROC_PARENT, (p.ParentName.empty()
		? std::format(L"{} (not running)", p.ParentId) : std::format(L"{} ({})", p.ParentName, p.ParentId)).c_str());
	SetDlgItemText(IDC_PROC_USER, p.User.c_str());
	SetDlgItemText(IDC_PROC_SESSION, std::to_wstring(p.SessionId).c_str());
	SetDlgItemText(IDC_PROC_STARTED, p.CreateTime ? FormatHelper::FormatDateTime(p.CreateTime).c_str() : L"");

	DlgResize_Init(false, false, 0);
	ApplyTheme(m_hWnd);
	return TRUE;
}

CProcPlaceholderPage::CProcPlaceholderPage(PCWSTR title, UINT icon, std::wstring text) :
	CPropertyPageImpl(title), m_Text(std::move(text)) {
	SetIconId(m_psp, icon);
}

LRESULT CProcPlaceholderPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetDlgItemText(IDC_PROC_PLACEHOLDER, m_Text.c_str());
	ApplyTheme(m_hWnd);
	return TRUE;
}

LRESULT CProcPlaceholderPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	// the text stays in the middle
	CWindow text(GetDlgItem(IDC_PROC_PLACEHOLDER));
	if (text) {
		CRect rc;
		text.GetWindowRect(&rc);
		int cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
		int margin = ::MulDiv(8, ::GetDpiForWindow(m_hWnd), 96);
		text.SetWindowPos(nullptr, margin, (cy - rc.Height()) / 2, (std::max)(0, cx - 2 * margin), rc.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	return 0;
}
