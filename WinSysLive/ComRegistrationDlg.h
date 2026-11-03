#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "SignatureVerifier.h"
#include "ObjectSecurity.h"
#include <WinSys/Com.h>
#include <mutex>

//
// System > COM Registration: a modal, resizable dialog. The registered COM classes (WinSys ComRegistry::EnumClasses:
// the 64-bit and the 32-bit views, the machine's and the user's, and the packaged ones; about 14,000 here in a second
// or so, read on a detached thread that shares only a LoadState with the dialog) in a virtual list: name, CLSID,
// ProgID, where it runs (in-process, local server, surrogate, service, handler: the row's icon is a DLL's, a server's,
// a service's or an object's for the others), the server as registered, threading
// model, app ID and its name, service, Run As, where it's registered (the machine, the user, the user's over the
// machine's), the view, the server's signature (verified as the rows are shown, on SignatureVerifier's thread) and
// the package; the Concern column says why a row stands out. Red: a machine-wide class's server a standard user can
// replace, or (missing) create; a scriptlet (scrobj.dll's ScriptletURL); a local server that runs an interpreter
// (rundll32 of a DLL outside Windows, powershell, cmd, mshta...); a signature that doesn't verify. Orange: a user's
// class over the machine's; yellow: another of the user's (per-user classes are a known way to hijack a class); gray
// text: a missing server nobody but an administrator could put back (a leftover). Local Machine and Current User (both
// on at first: HKCR, the user's over the machine's) choose the hives, one alone showing its own registrations (the
// machine's that a user's hides too); 32-bit Classes (on at first) the 32-bit view's; Suspicious Only the colored rows
// and the unsigned. The filter matches the name, CLSID, ProgID, server, app ID, package and scriptlet; any column sorts
// by its text. The context menu: Open File
// Location, Open in Registry Editor, Launch Permissions and Access Permissions (the app ID's or the machine's
// defaults, read-only in the ACL editor), Copy (the selected rows, or all shown) and Copy CLSID. Save... writes the
// rows shown as CSV or tab-separated text (ListExport); Ctrl+F from any control goes to the filter box.
//
class CComRegistrationDlg :
	public CDialogImpl<CComRegistrationDlg>,
	public CDialogResize<CComRegistrationDlg>,
	public CDpiDialogResize<CComRegistrationDlg> {
public:
	enum { IDD = IDD_COMREGISTRATION };
	static constexpr UINT WM_COMREG_READ = WM_APP + 0x1E4;
	static constexpr UINT WM_COMREG_SIGNED = WM_APP + 0x1E5;

	BEGIN_DLGRESIZE_MAP(CComRegistrationDlg)
		DLGRESIZE_CONTROL(IDC_CR_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_CR_MACHINE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CR_USER, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CR_32BIT, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CR_SUSPICIOUS, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CR_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_CR_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CR_SAVE, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CR_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CR_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CComRegistrationDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_COMREG_READ, OnRead)
		MESSAGE_HANDLER(WM_COMREG_SIGNED, OnSigned)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		COMMAND_HANDLER(IDC_CR_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CR_MACHINE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CR_USER, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CR_32BIT, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CR_SUSPICIOUS, OnFilterChanged)
		NOTIFY_HANDLER(IDC_CR_LIST, LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_HANDLER(IDC_CR_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_CR_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_CR_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_ID_HANDLER(IDC_CR_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_CR_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_CR_SAVE, OnSave)
		COMMAND_ID_HANDLER(ID_COMREG_OPENLOCATION, OnOpenLocation)
		COMMAND_ID_HANDLER(ID_COMREG_OPENKEY, OnOpenKey)
		COMMAND_ID_HANDLER(ID_COMREG_LAUNCHPERMISSIONS, OnPermissions)
		COMMAND_ID_HANDLER(ID_COMREG_ACCESSPERMISSIONS, OnPermissions)
		COMMAND_ID_HANDLER(ID_COMREG_COPYCLSID, OnCopyClsid)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CComRegistrationDlg>)
		CHAIN_MSG_MAP(CDialogResize<CComRegistrationDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Name, Clsid, ProgId, Type, Concern, Server, Threading, AppId, AppName, Service, RunAs, Registered, View,
		Signature, Package, _Count };

	struct LoadState {
		std::mutex Lock;
		std::vector<WinSys::ComClassRegistration> Classes;
		bool Done{ false };
	};

	// what makes a row stand out (its color, and Suspicious Only): a user's class (yellow), one over the machine's
	// (orange), a missing server no standard user can create (a leftover: gray text), and red for what someone
	// could exploit or that runs other code: a server a standard user can replace or plant, a signature that doesn't
	// verify, a scriptlet, an interpreter (but rundll32 of a Windows DLL)
	enum class Concern { None, PerUser, Override, Stale, Danger };

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	// the modal loop's messages (WH_MSGFILTER, MSGF_DIALOGBOX): Ctrl+F from any control goes to the filter box
	static LRESULT CALLBACK MessageFilter(int code, WPARAM wParam, LPARAM lParam);
	LRESULT OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnPermissions(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyClsid(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Load();
	// the rows' icons at the dialog's DPI: a DLL (in-process, surrogate, handler), a local server, a service, an object
	// (the others)
	void CreateImages();
	// the row's icon in the image list
	static int ImageOf(WinSys::ComServerType type);
	// the rows: the classes the filter and the check boxes allow, sorted
	void ShowRows();
	void Sort();
	// the column names, then the rows shown (selectedOnly: the selected ones), each in the header's order; the
	// signatures as known (none are queued for it)
	std::vector<std::vector<std::wstring>> GetExportRows(bool selectedOnly);
	// verify: queue the server's signature check if it isn't known (as a row is shown; not while sorting or copying)
	std::wstring GetText(WinSys::ComClassRegistration const& c, Column column, bool verify = false);
	Concern GetConcern(WinSys::ComClassRegistration const& c) const;
	// the Concern column: why the row stands out, the reasons joined with "; "
	std::wstring GetConcernText(WinSys::ComClassRegistration const& c) const;
	// the Local Machine and Current User check boxes allow it: both, the merged view (HKCR: the user's over the
	// machine's, which is left out); one, that hive's own (a machine's class the user's hides included)
	bool InScope(WinSys::ComClassRegistration const& c, bool machine, bool user) const;
	// the server file to verify and to open: a bare DLL name in the view's system folder; empty if there's none
	static std::wstring FileOf(WinSys::ComClassRegistration const& c);
	// the class's key as the Registry Editor shows it
	static std::wstring EditorPath(WinSys::ComClassRegistration const& c);
	WinSys::ComClassRegistration const* GetSelected() const;

	CListViewCtrl m_List;
	CImageListManaged m_Images;	// the list's, shared (destroyed with the dialog, or when replaced)
	std::vector<WinSys::ComClassRegistration> m_Classes;
	std::vector<int> m_Rows;	// indexes into m_Classes, as shown
	std::shared_ptr<LoadState> m_Load;	// while reading
	SignatureVerifier m_Signatures;
	CAclDialogTheming m_AclTheming;
	HHOOK m_FilterHook{ nullptr };	// MessageFilter, while the dialog is up
	inline static thread_local CComRegistrationDlg* s_Current{ nullptr };
	std::wstring m_Filter;	// lowercase
	int m_SortColumn{ (int)Column::Name };
	bool m_SortAscending{ true };
};
