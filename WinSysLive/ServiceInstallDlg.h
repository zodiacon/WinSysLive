#pragma once

#include "resource.h"

//
// The Services and Drivers tabs' New Service and Tools > Install Driver/Service: a service or driver created with WinSys ServiceManager::Install (name,
// display name, binary path, type, start type, error control, account and password, dependencies), then its
// description set and, if asked, started. Needs elevation; a failure to create is reported and keeps the dialog open.
//
class CServiceInstallDlg : public CDialogImpl<CServiceInstallDlg> {
public:
	enum { IDD = IDD_SERVICE_INSTALL };

	// driver: a kernel driver by default (the Drivers tab); title: the caption, else New Service or New Driver
	explicit CServiceInstallDlg(bool driver, PCWSTR title = nullptr) : m_Driver(driver), m_Title(title) {}

	// the name of the service created (after IDOK)
	std::wstring const& GetName() const {
		return m_Name;
	}
	// what was created is a driver (after IDOK)
	bool IsDriver() const {
		return m_Created == Created::Driver;
	}

	BEGIN_MSG_MAP(CServiceInstallDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_SVCI_BROWSE, OnBrowse)
		COMMAND_HANDLER(IDC_SVCI_TYPE, CBN_SELCHANGE, OnTypeChanged)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTypeChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the selected type is a driver's (no account)
	bool IsDriverType() const;
	// tells the user and focuses the control; returns false
	bool Invalid(UINT id, PCWSTR text);

	enum class Created { None, Service, Driver };

	bool m_Driver;
	PCWSTR m_Title;
	std::wstring m_Name;
	Created m_Created{ Created::None };
};
