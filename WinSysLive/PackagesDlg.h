#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Packages.h>
#include <functional>
#include <mutex>

//
// System > Installed Packages: a modal, resizable dialog, read-only. The installed MSIX/AppX packages (WinSys
// Packages::Enum, about 1.3 s for 160 packages, so read on a detached thread that shares only a LoadState with the
// dialog: closing doesn't wait): the current user's, or with All Users (elevated only) every user's. A row per package:
// display name, package name, version, architecture, kind (main, framework, resource, optional, bundle), publisher,
// signature, status (problems in red), install date, running processes (from the frame's snapshot, by package full
// name), location, dependencies and full name. Frameworks and Resources (on at first) shows those kinds too. The filter
// matches the names, publisher, full name and location; any column sorts. Double-click or Open Folder opens the
// install folder; Launch starts the package's application (a submenu of its app IDs when it has several; none for
// frameworks, resources or another user's package); Go to Process goes to the package's first running process
// (closing the dialog); Copy copies the
// selected rows, or every row shown, with the column names; Copy Full Name the selected packages' full names.
//
class CPackagesDlg :
	public CDialogImpl<CPackagesDlg>,
	public CDialogResize<CPackagesDlg>,
	public CDpiDialogResize<CPackagesDlg> {
public:
	enum { IDD = IDD_PACKAGES };
	static constexpr UINT WM_PACKAGES_READ = WM_APP + 0x1E0;

	// the running processes of each package (by full name), and going to a process
	using RunningLookup = std::function<std::unordered_map<std::wstring, std::vector<uint32_t>>()>;
	CPackagesDlg(RunningLookup running, std::function<void(uint32_t)> goToProcess)
		: m_Running(std::move(running)), m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CPackagesDlg)
		DLGRESIZE_CONTROL(IDC_PK_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PK_FRAMEWORKS, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PK_ALLUSERS, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PK_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_PK_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PK_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PK_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CPackagesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_PACKAGES_READ, OnPackagesRead)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_PK_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_PK_FRAMEWORKS, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_PK_ALLUSERS, OnRefresh)
		NOTIFY_HANDLER(IDC_PK_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_PK_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_HANDLER(IDC_PK_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(IDC_PK_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_PK_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_PACKAGES_OPENFOLDER, OnOpenFolder)
		COMMAND_ID_HANDLER(ID_PACKAGES_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_PACKAGES_COPYFULLNAME, OnCopyFullName)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CPackagesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CPackagesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { DisplayName, Name, Version, Architecture, Kind, Publisher, Signature, Status, Installed,
		Processes, Location, Dependencies, FullName, _Count };

	struct LoadState {
		std::mutex Lock;
		std::optional<std::vector<WinSys::PackageInfo>> Result;
		DWORD Error{ 0 };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPackagesRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenFolder(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyFullName(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// starts reading the packages (if not reading already)
	void Load();
	// starts one of the package's applications; the status line says its process ID, a message box why it failed
	void Launch(std::wstring const& appUserModelId);
	// the list's rows: the packages the filter and the kinds check box allow, sorted
	void ShowRows();
	std::wstring GetText(WinSys::PackageInfo const& p, Column column) const;
	// the package of the focused (else first selected) row; null if none
	WinSys::PackageInfo const* GetSelected() const;
	// the package's running processes (from the last lookup)
	std::vector<uint32_t> const* GetProcesses(WinSys::PackageInfo const& p) const;
	static int CALLBACK Compare(LPARAM index1, LPARAM index2, LPARAM param);

	RunningLookup m_Running;
	std::function<void(uint32_t)> m_GoToProcess;
	std::unordered_map<std::wstring, std::vector<uint32_t>> m_Processes;	// by package full name
	CListViewCtrl m_List;
	std::vector<WinSys::PackageInfo> m_Packages;	// each row's item data is its index here
	std::shared_ptr<LoadState> m_Load;	// while reading
	DWORD m_Error{ 0 };	// why the packages couldn't be read
	bool m_AllUsersShown{ false };	// what m_Packages holds
	std::wstring m_Filter;	// lowercase
	int m_SortColumn{ (int)Column::DisplayName };
	bool m_SortAscending{ true };
};
