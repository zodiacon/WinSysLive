#pragma once

#include "resource.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/Service.h>

//
// What the service sheet's pages share: the service's configuration as it was read, and whether a page applied a change
//
struct ServiceSheetData {
	std::wstring Name;
	WinSys::ServiceConfiguration Config{};
	std::wstring Description;
	WinSys::ServiceStatusProcess Status{};
	std::optional<WinSys::ServiceFailureActions> Failure;	// nothing if it can't be read
	bool Applied{ false };
};

//
// Changes go through a WinSys Service opened with SERVICE_CHANGE_CONFIG (in practice, elevation): each page applies
// what it changed when OK or Apply is clicked; a failure is reported and keeps the sheet open.
//
template<typename T>
class CServicePageBase : public CPropertyPageImpl<T> {
public:
	explicit CServicePageBase(ServiceSheetData& data) : m_Data(data) {}

	BEGIN_MSG_MAP(CServicePageBase)
		CHAIN_MSG_MAP(CPropertyPageImpl<T>)
	END_MSG_MAP()

protected:
	// the service opened for changing it (with extra access, if needed); nullptr after the error was shown
	std::unique_ptr<WinSys::Service> OpenForChange(WinSys::ServiceAccessMask extra = WinSys::ServiceAccessMask::None);
	void ShowError(PCWSTR operation, DWORD error);
	// a change on the page: Apply is enabled
	void Changed() {
		m_Changed = true;
		this->SetModified(TRUE);
	}

	ServiceSheetData& m_Data;
	bool m_Changed{ false };
};

// display name, description, path, start type and error control
class CServiceGeneralPage : public CServicePageBase<CServiceGeneralPage> {
public:
	enum { IDD = IDD_SERVICE_GENERAL };
	using CServicePageBase::CServicePageBase;

	BEGIN_MSG_MAP(CServiceGeneralPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_CODE_HANDLER(EN_CHANGE, OnChange)
		COMMAND_CODE_HANDLER(CBN_SELCHANGE, OnChange)
		CHAIN_MSG_MAP(CServicePageBase<CServiceGeneralPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	int OnApply();

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	bool m_Initialized{ false };	// EN_CHANGE comes while the fields are filled
};

// the account the service runs as: a built-in one, or an account with its password
class CServiceLogOnPage : public CServicePageBase<CServiceLogOnPage> {
public:
	enum { IDD = IDD_SERVICE_LOGON };
	using CServicePageBase::CServicePageBase;

	BEGIN_MSG_MAP(CServiceLogOnPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_RANGE_CODE_HANDLER(IDC_SVC_LOCALSYSTEM, IDC_SVC_THISACCOUNT, BN_CLICKED, OnAccountKind)
		COMMAND_CODE_HANDLER(EN_CHANGE, OnChange)
		CHAIN_MSG_MAP(CServicePageBase<CServiceLogOnPage>)
	END_MSG_MAP()

	int OnApply();

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnAccountKind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void UpdateControls();

	bool m_Initialized{ false };
	bool m_ReadOnly{ false };	// a per-user service: its account is the user's
};

// the failure actions
class CServiceRecoveryPage : public CServicePageBase<CServiceRecoveryPage> {
public:
	enum { IDD = IDD_SERVICE_RECOVERY };
	using CServicePageBase::CServicePageBase;

	BEGIN_MSG_MAP(CServiceRecoveryPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_CODE_HANDLER(CBN_SELCHANGE, OnChange)
		COMMAND_CODE_HANDLER(EN_CHANGE, OnChange)
		COMMAND_ID_HANDLER(IDC_SVC_NONCRASH, OnChange)
		CHAIN_MSG_MAP(CServicePageBase<CServiceRecoveryPage>)
	END_MSG_MAP()

	int OnApply();

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChange(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the delays and the command line, enabled for the actions chosen
	void UpdateControls();
	WinSys::ServiceFailureActionType GetAction(int index) const;

	bool m_Initialized{ false };
};

//
// A service's properties, as services.msc has them: General (display name, description, path, start type, error
// control), Log On (the account) and Recovery (the failure actions). Modal; the configuration is read when it's created.
//
class CServiceSheet : public CPropertySheetImpl<CServiceSheet> {
public:
	explicit CServiceSheet(std::wstring const& name);

	// a page changed the configuration
	bool Applied() const {
		return m_Data.Applied;
	}

	void OnSheetInitialized();

private:
	ServiceSheetData m_Data;
	std::wstring m_Title;
	CServiceGeneralPage m_General{ m_Data };
	CServiceLogOnPage m_LogOn{ m_Data };
	CServiceRecoveryPage m_Recovery{ m_Data };
};
