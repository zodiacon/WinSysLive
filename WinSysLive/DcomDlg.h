#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Com.h>
#include <WinSys/EventLog.h>
#include <mutex>

//
// System > DCOM and Running Objects: a modal, resizable dialog, read-only, in five tabs, each a list above the selected
// row's details (property and value):
// - Machine: the machine-wide DCOM settings (WinSys ComSecurity::GetMachineSettings: Enable DCOM, the legacy
//   authentication and impersonation levels, the default launch and access permissions and the machine's limits, the
//   hardening, the logging levels), a permission's entries in the details;
// - App IDs: every app ID (ComRegistry::EnumAppIds; HKLM's AppID key is shared by both views): its name, Run As,
//   service, surrogate, authentication level, launch and access permissions (its own, or "Default": the machine's),
//   remote server, the classes using it (of both views, each once), the executables mapped to it; colored by what
//   weakens it: red for no authentication (level None), orange for a launch permission granting remote launch or
//   activation to anyone who can log on (Everyone, Users, Anonymous...) or an access permission granting remote access
//   to Anonymous (Windows' own default gives Everyone remote access), yellow for a stored account (Run As a user: its
//   password is in the LSA secrets) or a per-user app ID;
// - Elevation: the classes the elevation moniker may create elevated (Elevation\Enabled), those UAC elevates without a
//   prompt (COMAutoApprovalList), red when a standard user can replace the server;
// - DCOM Errors: the System log's DistributedCOM events of the last 30 days (EventLog::Query with the provider): the
//   activations that failed (10016's permission errors, servers that didn't start or register), the class and app ID
//   named from the event's data. Successful activations aren't logged anywhere ETW can see (Microsoft-Windows-COMRuntime,
//   -COM and -DistributedCOM have failure events only; combase's TraceLogging providers sent nothing for activations);
// - Running Objects: the Running Object Table as this process sees it (ComSecurity::EnumRunningObjects).
// Everything is read on a detached thread (COM, MTA: a custom moniker's display name is asked of its object, the
// classes take a second or two) that shares only a LoadState with the dialog and posts WM_DCOM_READ; closing doesn't
// wait. The filter matches any cell of the upper list; both lists sort by any column (times and numbers as such). The
// context menu: Launch Permissions and Access Permissions (the system's ACL editor, read-only, ComPermissionInfo.h),
// Open in Registry Editor (also the double-click), Open File Location (a class's server), Copy (also the button: the
// list that last had the focus, the selected rows or all of them).
//
class CDcomDlg :
	public CDialogImpl<CDcomDlg>,
	public CDialogResize<CDcomDlg>,
	public CDpiDialogResize<CDcomDlg> {
public:
	enum { IDD = IDD_DCOM };
	static constexpr UINT WM_DCOM_READ = WM_APP + 0x1E6;

	BEGIN_DLGRESIZE_MAP(CDcomDlg)
		DLGRESIZE_CONTROL(IDC_DC_TABS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_DC_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_DC_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_DC_DETAILS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DC_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DC_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_DC_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CDcomDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DCOM_READ, OnRead)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_HANDLER(IDC_DC_TABS, TCN_SELCHANGE, OnTabChanged)
		NOTIFY_HANDLER(IDC_DC_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_DC_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_HANDLER(IDC_DC_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_HANDLER(IDC_DC_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_DC_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_DC_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CDcomDlg>)
		CHAIN_MSG_MAP(CDialogResize<CDcomDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Tab { Machine, AppIds, Elevation, Errors, RunningObjects, _Count };
	enum class Concern { None, Note, Warning, Danger };

	// a list's columns, rows and sorting
	struct Table {
		enum class Kind { Text, Number, Time };
		struct Column {
			PCWSTR Name;
			int Width;	// at 96 DPI
			Kind Sort{ Kind::Text };
		};
		std::vector<Column> Columns;
		std::vector<std::vector<std::wstring>> Rows;	// cells
		std::vector<int64_t> Times;		// per row: the Time column's value (FILETIME), for sorting
		std::vector<Concern> Concerns;	// per row: its color
		std::vector<int> Shown;	// row indices in display order; the list's item data
		int SortColumn{ 0 };
		bool SortAscending{ true };
	};

	// what the thread reads
	struct LoadState {
		std::mutex Lock;
		std::vector<WinSys::DcomMachineSetting> Settings;
		std::vector<WinSys::ComAppIdRegistration> AppIds;
		std::vector<WinSys::ComClassRegistration> Classes;
		std::optional<std::vector<WinSys::EventLogEntry>> Errors;
		DWORD ErrorsError{ 0 };
		std::optional<std::vector<WinSys::RunningObject>> Objects;
		DWORD ObjectsError{ 0 };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// starts the reading thread (unless one is running)
	void Load();
	// the tables' rows from what was read
	void BuildRows();
	void BuildMachine();
	void BuildAppIds();
	void BuildElevation();
	void BuildErrors();
	void BuildObjects();
	Tab CurrentTab() const;
	// the tab's columns in the upper list, then its rows
	void ShowTab();
	// the rows the filter keeps, sorted, keeping the selection
	void ShowRows();
	// the selected row's properties in the lower list
	void ShowDetails();
	static void InitColumns(CListViewCtrl& list, Table const& table);
	static void Fill(CListViewCtrl& list, Table& table);
	static void Sort(Table& table);
	// the selected row's index in its tab's source vector, or -1
	int GetSelectedRow() const;
	void UpdateStatus();

	// the selected row's permission (the security descriptor, the editor's title), launch or access; false if it has
	// none to show (a message box said why)
	void ShowPermission(bool launch);
	// the registry key of the selected row, as regedit's address bar takes it; empty if none
	std::wstring GetSelectedKeyPath() const;
	// a class's server file (the Elevation tab), empty if none
	std::wstring GetSelectedFile() const;
	// a class's name by CLSID (upper case, braces), from the classes read; empty if not known
	std::wstring ClassName(std::wstring const& clsid) const;
	std::wstring AppIdName(std::wstring const& appId) const;
	// a permission's entries as detail rows ("Launch: Allow Everyone" - "Local Launch, Local Activation")
	static void AddPermissionRows(std::vector<std::pair<std::wstring, std::wstring>>& rows, PCWSTR title,
		std::vector<uint8_t> const& sd, bool launch);

	CTabCtrl m_Tabs;
	CListViewCtrl m_List, m_Details;
	Table m_Tables[(int)Tab::_Count];
	std::vector<std::pair<std::wstring, std::wstring>> m_DetailRows;	// the lower list's
	std::shared_ptr<LoadState> m_Load;	// while reading
	bool m_Loaded{ false };
	// what was read; the tables' rows index these
	std::vector<WinSys::DcomMachineSetting> m_Settings;
	std::vector<WinSys::ComAppIdRegistration> m_AppIds;
	std::vector<WinSys::ComClassRegistration> m_Classes;
	std::vector<int> m_Elevated;	// indices into m_Classes: the Elevation tab's rows
	std::vector<WinSys::EventLogEntry> m_Errors;
	DWORD m_ErrorsError{ 0 };
	std::vector<WinSys::RunningObject> m_Objects;
	DWORD m_ObjectsError{ 0 };
	std::unordered_map<std::wstring, int> m_ClassByClsid;	// the 64-bit view's (else the 32-bit's) class, by CLSID
	std::unordered_map<std::wstring, int> m_AppIdById;		// HKCR's app ID by ID (the user's over the machine's)
	std::unordered_map<std::wstring, std::vector<int>> m_ClassesByAppId;	// an app ID to its classes (a CLSID once)
	std::wstring m_Filter;		// lowercase
	HWND m_CopyList{ nullptr };
	bool m_Filling{ false };	// selection changes while the upper list is filled aren't the user's
};
