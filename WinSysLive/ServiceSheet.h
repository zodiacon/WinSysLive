#pragma once

#include "resource.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/Service.h>
#include "ObjectSecurity.h"

//
// What the service sheet's pages share: the service's configuration as it was read, and whether a page applied a change
//
struct ServiceSheetData {
	std::wstring Name;
	WinSys::ServiceConfiguration Config{};
	std::wstring Description;
	WinSys::ServiceStatusProcess Status{};
	std::optional<WinSys::ServiceFailureActions> Failure;	// nothing if it can't be read
	bool Driver{ false };	// a kernel or file system driver: only the General page applies, with a driver's start types
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

	// a value shown rounded (the reset period in days, the delays in minutes): written back as it was unless its text
	// was changed, so a reset period of hours or a delay of seconds isn't rounded down by a change elsewhere
	struct RoundedValue {
		UINT Id;
		uint32_t Value;
		std::wstring Text;	// as first shown
	};
	// unit: per one shown (24 * 3600 seconds, 60000 ms); nothing after telling the user, if it's no number or too large
	std::optional<uint32_t> GetValue(RoundedValue const& value, uint32_t unit, PCWSTR what);

	bool m_Initialized{ false };
	RoundedValue m_Reset{ IDC_SVC_RESETDAYS }, m_RestartDelay{ IDC_SVC_RESTARTMIN }, m_RebootDelay{ IDC_SVC_REBOOTMIN };
};

// what the service depends on and what depends on it (directly; a node expands to the next level when it's opened, a
// load order group to its services); double-clicking a service, or its context menu, opens its properties
class CServiceDependenciesPage : public CPropertyPageImpl<CServiceDependenciesPage> {
public:
	enum { IDD = IDD_SERVICE_DEPENDENCIES };
	explicit CServiceDependenciesPage(ServiceSheetData& data) : m_Data(data) {}

	BEGIN_MSG_MAP(CServiceDependenciesPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnItemExpanding)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnTreeDoubleClick)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnTreeRightClick)
		CHAIN_MSG_MAP(CPropertyPageImpl<CServiceDependenciesPage>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnItemExpanding(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnTreeDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnTreeRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// a tree item's (its lParam is its index)
	struct Node {
		std::wstring Name;
		bool Group;	// a load order group ("+Group" in the dependencies), not a service
		bool Dependents;	// in the dependents' tree
		bool Installed{ false };	// a service that exists (its properties can be shown)
		bool Filled{ false };	// its children were added
	};
	// the services and groups name depends on, the services depending on it directly, or a group's services; false if
	// there are none
	bool AddChildren(CTreeViewCtrl tree, HTREEITEM parent, std::wstring const& name, bool dependents, bool group = false);
	void AddNode(CTreeViewCtrl tree, HTREEITEM parent, std::wstring const& name, bool group, bool dependents);
	// name is the sheet's service, the item's or one of its ancestors' (a cycle, which the SCM shouldn't allow)
	bool InPath(CTreeViewCtrl tree, HTREEITEM item, std::wstring const& name) const;
	// the service node under the cursor, or nullptr
	Node const* ServiceAtCursor(CTreeViewCtrl tree, HTREEITEM* item = nullptr) const;
	// the services of each load order group (lowercase names), read on the first group expanded
	std::vector<std::wstring> const& GroupMembers(std::wstring const& group);

	ServiceSheetData& m_Data;
	std::vector<Node> m_Nodes;
	std::optional<std::unordered_map<std::wstring, std::vector<std::wstring>>> m_Groups;
};

//
// A service's properties, as services.msc has them: General (display name, description, path, start type, error
// control), Log On (the account), Recovery (the failure actions), Dependencies and Security (the system's ACL editor on
// the service's descriptor, CServiceSecurityInfo; it applies its own changes). Modal; the configuration is read when
// it's created.
//
class CServiceSheet : public CPropertySheetImpl<CServiceSheet> {
public:
	explicit CServiceSheet(std::wstring const& name);
	~CServiceSheet();

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
	CServiceDependenciesPage m_Dependencies{ m_Data };
	CServiceSecurityInfo* m_Security{ nullptr };
	CAclDialogTheming m_AclTheming;
};
