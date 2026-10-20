#pragma once

#include <VirtualListView.h>
#include "resource.h"

namespace WinSys {
	class Token;
}

//
// What a process's property sheet shows of it: copied from the snapshot on the UI thread, because the sheet runs on
// a thread of its own. The pages query anything else from the process themselves.
//
struct ProcessProperties {
	uint32_t Id{ 0 };
	int64_t CreateTime{ 0 };
	uint32_t ParentId{ 0 };
	uint32_t SessionId{ 0 };
	std::wstring ImageName, ImagePath, CommandLine, User, Description;
	std::wstring ParentName;	// empty if the parent isn't running (or its ID was reused)
};

//
// The General page: identity and origin of the process, its security mitigations and signature. Its controls follow
// the page's size through the dialog's AFX_DIALOG_LAYOUT resource (CDynamicDialogLayout)
//
class CProcGeneralPage :
	public CPropertyPageImpl<CProcGeneralPage>,
	public CDynamicDialogLayout<CProcGeneralPage> {
public:
	enum { IDD = IDD_PROC_GENERAL };

	CProcGeneralPage(ProcessProperties const& props, HICON hIcon);

	BEGIN_MSG_MAP(CProcGeneralPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_PROC_VERIFY, OnVerify)
		COMMAND_ID_HANDLER(IDC_PROC_KILL, OnKill)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcGeneralPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcGeneralPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVerify(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// ASLR, CFG, CET and DEP, from the process's mitigation policies (and the image's header, for ASLR)
	void ShowMitigations();

	ProcessProperties const& m_Props;
	HICON m_hIcon;	// the sheet's
};

//
// The Environment page: the process's environment variables, read from its process parameters (WinSys
// Process::GetEnvironment) when the page is first shown. Copy puts the selected variables on the clipboard,
// Copy All all of them, as NAME=value lines.
//
class CProcEnvironmentPage :
	public CPropertyPageImpl<CProcEnvironmentPage>,
	public CDynamicDialogLayout<CProcEnvironmentPage>,
	public CVirtualListView<CProcEnvironmentPage> {
public:
	enum { IDD = IDD_PROC_ENVIRONMENT };

	explicit CProcEnvironmentPage(ProcessProperties const& props);

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);

	BEGIN_MSG_MAP(CProcEnvironmentPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_ENV_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_ENV_COPYALL, OnCopy)
		NOTIFY_HANDLER(IDC_ENV_LIST, LVN_ITEMCHANGED, OnItemChanged)
		CHAIN_MSG_MAP(CVirtualListView<CProcEnvironmentPage>)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcEnvironmentPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcEnvironmentPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// reads the variables; false (with the status line saying why) if they can't be read
	bool Load();
	void UpdateButtons();

	ProcessProperties const& m_Props;
	CListViewCtrl m_List;
	std::vector<std::pair<std::wstring, std::wstring>> m_Vars;	// in display order
};

//
// The Token page: the process token's properties (read-only edits, grouped), privileges and groups (with an
// AppContainer's capabilities), read from the token (WinSys Token) when the page is first shown
//
class CProcTokenPage :
	public CPropertyPageImpl<CProcTokenPage>,
	public CDynamicDialogLayout<CProcTokenPage> {
public:
	enum { IDD = IDD_PROC_TOKEN };

	explicit CProcTokenPage(ProcessProperties const& props);

	BEGIN_MSG_MAP(CProcTokenPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcTokenPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcTokenPage>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	void ShowProperties(WinSys::Token const& token);
	void ShowPrivileges(WinSys::Token const& token);
	void ShowGroups(WinSys::Token const& token);

	ProcessProperties const& m_Props;
	CListViewCtrl m_Privileges, m_Groups;
};

//
// A page still to be implemented: its title, icon and a line saying what it will show
//
class CProcPlaceholderPage : public CPropertyPageImpl<CProcPlaceholderPage> {
public:
	enum { IDD = IDD_PROC_PAGE };

	// title: kept (not copied) until the page is created
	CProcPlaceholderPage(PCWSTR title, UINT icon, std::wstring text);

	BEGIN_MSG_MAP(CProcPlaceholderPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcPlaceholderPage>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	std::wstring m_Text;
};
