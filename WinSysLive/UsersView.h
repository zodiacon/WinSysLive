#pragma once

#include "BarFont.h"

#include "resource.h"
#include "ViewBase.h"
#include <TreeListView/include/TreeListView.h>
#include <WinSys/Sessions.h>
#include <WinSys/LsaSecurity.h>
#include <map>
#include "ColumnLayout.h"
#include "FilterBox.h"

//
// The sessions (the console, remote desktop sessions, session 0 for services) with totals of their processes,
// and the processes as each session's children, like Task Manager's Users tab; or, with Group by Account (the button
// beside the filter), the accounts the processes run as (Run As starts another user's process in this session), with
// totals of their processes, and under each account its logon sessions (a Run As logon is one of its own). A User
// column has each process's account, and a process running as another user than its session's is highlighted. The filter box (in the view's rebar) keeps the processes whose image name, PID or account
// contains its text, or whose session's title, name or client does; the group rows stay (they have the totals).
//
class CUsersView :
	public CViewBase<CUsersView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	bool CanCopy() const override {
		return GetSelectedRow() != nullptr;
	}
	HWND GetExportList() override {
		return m_Tree.GetListCtrl();
	}
	std::optional<std::wstring> GetTreeColumnText(HWND hList, int row) override {
		if (hList != m_Tree.GetListCtrl())
			return std::nullopt;
		auto item = m_Tree.GetVisibleItem(row);
		return item ? m_Tree.GetCellText(item, 0) : std::wstring();
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	void OnDpiChanged(UINT dpi) override;
	void UpdateLayout(BOOL resizeBars = TRUE);

	BEGIN_MSG_MAP(CUsersView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		// the mode button, in the view's rebar (which forwards its command)
		COMMAND_ID_HANDLER(ID_USERS_GROUPBYACCOUNT, OnGroupByAccount)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CViewBase<CUsersView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_USER_DISCONNECT, OnDisconnect)
		COMMAND_ID_HANDLER(ID_USER_LOGOFF, OnLogoff)
		COMMAND_ID_HANDLER(ID_USER_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		// the View menu's (and so the command palette's)
		COMMAND_ID_HANDLER(ID_USERS_GROUPBYACCOUNT, OnGroupByAccount)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_COLUMN_RESET, OnResetColumns)
		CHAIN_MSG_MAP_ALT(CViewBase<CUsersView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Name, ProcessId, SessionId, Session, State, Client, LogonTime, Processes, CPU, PrivateBytes, WorkingSet,
		User, LogonId, LogonType,
		_Count
	};

	// a session or an account with its logon sessions (the groups, by the mode), or a process under its group
	struct Row {
		bool IsSession{ false };
		bool IsAccount{ false };
		bool IsLogon{ false };
		HTLITEM Item{ nullptr };
		uint32_t SessionId{ 0 };	// session rows, process rows
		std::wstring Account;		// account and logon rows (DOMAIN\user, empty: unknown), process rows
		int64_t LogonId{ 0 };		// logon rows, process rows: the token's logon session; 0 if unknown

		// group rows: totals over the group's processes, recomputed on every refresh
		WinSys::SessionInfo Session{};	// session rows
		int Processes{ 0 };
		int32_t CPU{ 0 };	// ProcessInfo::CPU units
		int64_t PrivateBytes{ 0 }, WorkingSet{ 0 };
		std::wstring Sessions;	// account and logon rows: the sessions their processes run in ("0, 1")

		// process rows
		std::shared_ptr<ProcessInfoEx> Process;

		bool IsGroup() const {
			return IsSession || IsAccount || IsLogon;
		}
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSort(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDisconnect(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnLogoff(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGroupByAccount(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the mode button's band after the filter's, sized for the DPI (as the Processes view's Hide button)
	void LayoutModeBar();
	void SetModeButtonWidth();
	// the tree again, its groups by the mode; the user's own group expanded
	void Rebuild();
	void ExpandOwnGroup();
	static std::wstring GetAccountTitle(std::wstring const& account);
	// a logon session's details, queried once (another user's need elevation: then nothing)
	WinSys::LogonSessionData const* GetLogon(int64_t logonId) const;
	std::wstring GetLogonTitle(Row const& logon) const;
	// a process row whose account is a user's other than its session's (Run As), not a system or service account
	bool IsOtherUser(Row const& process) const;
	// a process row: shown with the filter's text (always, without one)
	bool Matches(Row const& row) const;

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// Name stays list column 0, which draws the tree; the others follow it, in the order given (the header's order array)
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the UserColumns setting
	void SaveColumns() const;
	void SyncSessions();
	void SyncProcesses();
	void Sort();
	static int CALLBACK CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam);
	int CompareRows(Row& r1, Row& r2) const;
	std::wstring GetColumnText(Row& row, Column column) const;
	static std::wstring GetSessionTitle(WinSys::SessionInfo const& session);
	Row* GetSelectedRow() const;
	void ShowContextMenu(Row const& row, POINT pt);	// pt in screen coordinates
	void ShowError(PCWSTR operation) const;

	CTreeListViewCtrl m_Tree;
	CFilterBox m_Filter;	// in the view's rebar
	// node-based maps: Row* (the items' data) stays valid
	std::unordered_map<uint32_t, Row> m_Sessions;
	std::unordered_map<WinSys::ProcessOrThreadKey, Row> m_Processes;
	std::unordered_map<std::wstring, Row> m_Accounts;	// by account name, while grouping by account
	std::map<std::pair<std::wstring, int64_t>, Row> m_Logons;	// by account and logon ID, under the accounts
	// the logon sessions' details by logon ID (nothing: not readable), dropped once no process has the logon
	mutable std::unordered_map<int64_t, std::optional<WinSys::LogonSessionData>> m_LogonDetails;
	std::vector<HTLITEM> m_AddedGroups;	// collapsed once their processes are in
	CToolBarCtrl m_ModeBar;	// in the rebar after the filter: Group by Account
	CBarFont m_ModeBarFont;	// the user's, else for its DPI
	int m_ModeButtonWidth{ 0 };	// at 96 DPI
	bool m_ByAccount{ false };	// the groups are accounts, not sessions (the UsersGroupByAccount setting)
	std::vector<Column> m_Columns;	// by list column: the Column it shows
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	int m_SortColumn{ -1 };	// a Column, or -1
	bool m_SortAscending{ true };
};
