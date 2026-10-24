#pragma once

#include "TrayIcons.h"

//
// Options > Tray Icons...: a check box per icon (CTrayIcons' kinds, in the template's order: IDC_TRAY_SHOW + kind).
//
class CTrayIconsDlg : public CDialogImpl<CTrayIconsDlg> {
public:
	enum { IDD = IDD_TRAYICONS };

	explicit CTrayIconsDlg(CTrayIcons::Modes const& modes);

	CTrayIcons::Modes const& GetModes() const {
		return m_Modes;
	}

	BEGIN_MSG_MAP(CTrayIconsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	CTrayIcons::Modes m_Modes;
};
