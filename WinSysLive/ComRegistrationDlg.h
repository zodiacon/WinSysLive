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
// the package. A missing server file or a signature that doesn't verify is red, a user's class over the machine's
// orange, another of the user's yellow: per-user classes are a known way to hijack a class. 32-bit Classes (on at
// first) shows the 32-bit view's; Suspicious Only the red, orange and yellow ones and the unsigned. The filter matches
// the name, CLSID, ProgID, server, app ID and package; any column sorts by its text. The context menu: Open File
// Location, Open in Registry Editor, Launch Permissions and Access Permissions (the app ID's or the machine's
// defaults, read-only in the ACL editor), Copy (the selected rows, or all shown) and Copy CLSID.
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
		DLGRESIZE_CONTROL(IDC_CR_32BIT, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CR_SUSPICIOUS, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CR_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_CR_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CR_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CR_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CComRegistrationDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_COMREG_READ, OnRead)
		MESSAGE_HANDLER(WM_COMREG_SIGNED, OnSigned)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		COMMAND_HANDLER(IDC_CR_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CR_32BIT, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CR_SUSPICIOUS, OnFilterChanged)
		NOTIFY_HANDLER(IDC_CR_LIST, LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_HANDLER(IDC_CR_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_CR_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_CR_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_ID_HANDLER(IDC_CR_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_CR_COPY, OnCopy)
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
	enum class Column { Name, Clsid, ProgId, Type, Server, Threading, AppId, AppName, Service, RunAs, Registered, View,
		Signature, Package, _Count };

	struct LoadState {
		std::mutex Lock;
		std::vector<WinSys::ComClassRegistration> Classes;
		bool Done{ false };
	};

	// what makes a row stand out (its color, and Suspicious Only)
	enum class Concern { None, PerUser, Override, Broken };

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
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
	// verify: queue the server's signature check if it isn't known (as a row is shown; not while sorting or copying)
	std::wstring GetText(WinSys::ComClassRegistration const& c, Column column, bool verify = false);
	Concern GetConcern(WinSys::ComClassRegistration const& c) const;
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
	std::wstring m_Filter;	// lowercase
	int m_SortColumn{ (int)Column::Name };
	bool m_SortAscending{ true };
};
