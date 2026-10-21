#pragma once

#include "resource.h"
#include "ViewBase.h"
#include <TreeListView/include/TreeListView.h>
#include <WinSys/Sessions.h>
#include "ColumnLayout.h"

//
// The sessions (the console, remote desktop sessions, session 0 for services) with totals of their processes,
// and the processes as each session's children, like Task Manager's Users tab.
//
class CUsersView :
	public CViewBase<CUsersView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
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
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override {
		m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}

	BEGIN_MSG_MAP(CUsersView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		CHAIN_MSG_MAP(CViewBase<CUsersView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_USER_DISCONNECT, OnDisconnect)
		COMMAND_ID_HANDLER(ID_USER_LOGOFF, OnLogoff)
		COMMAND_ID_HANDLER(ID_USER_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		CHAIN_MSG_MAP_ALT(CViewBase<CUsersView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Name, ProcessId, SessionId, Session, State, Client, LogonTime, Processes, CPU, PrivateBytes, WorkingSet,
		_Count
	};

	// a session, or a process under its session
	struct Row {
		bool IsSession{ false };
		HTLITEM Item{ nullptr };
		uint32_t SessionId{ 0 };

		// session rows: totals over the session's processes, recomputed on every refresh
		WinSys::SessionInfo Session{};
		int Processes{ 0 };
		int32_t CPU{ 0 };	// ProcessInfo::CPU units
		int64_t PrivateBytes{ 0 }, WorkingSet{ 0 };

		// process rows
		std::shared_ptr<ProcessInfoEx> Process;
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSort(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDisconnect(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnLogoff(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

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
	// node-based maps: Row* (the items' data) stays valid
	std::unordered_map<uint32_t, Row> m_Sessions;
	std::unordered_map<WinSys::ProcessOrThreadKey, Row> m_Processes;
	std::vector<HTLITEM> m_AddedSessions;	// by SyncSessions; collapsed once their processes are in
	std::vector<Column> m_Columns;	// by list column: the Column it shows
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	int m_SortColumn{ -1 };	// a Column, or -1
	bool m_SortAscending{ true };
};
