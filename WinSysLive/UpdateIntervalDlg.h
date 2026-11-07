#pragma once

#include "resource.h"

//
// View > Update Interval > Other...: the interval in seconds (IntervalText: 0.1 to 60, up to three decimals). OK with
// text that isn't one says so in the dialog and keeps it open; the caller applies GetInterval (milliseconds).
//
class CUpdateIntervalDlg : public CDialogImpl<CUpdateIntervalDlg> {
public:
	enum { IDD = IDD_UPDATEINTERVAL };

	explicit CUpdateIntervalDlg(int interval) : m_Interval(interval) {}

	int GetInterval() const {
		return m_Interval;
	}

	BEGIN_MSG_MAP(CUpdateIntervalDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_UI_SECONDS, EN_CHANGE, OnChanged)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	int m_Interval;
};
