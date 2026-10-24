#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/Ipc.h>
#include <VirtualListView.h>

//
// The RPC/ALPC tab: which processes talk to which, from WinSys IpcTracker (an ETW session, elevated only), started
// when the tab is first shown and kept running: ALPC messages per client and server process, and RPC calls per client,
// interface and endpoint, with the server where it's known. Read again on every refresh tick (also while hidden) and
// merged by row key: new rows green for two refreshes (not on the first read); the rate is over the last refresh. A
// process that exits is forgotten (IpcTracker::RemoveProcess), its rows with it. The filter box matches the processes,
// PIDs, kind, interface, endpoint and protocol. Double-click goes to the server (on its columns) or the client; Copy
// copies the selected row's shown columns.
//
class CIpcView :
	public CViewBase<CIpcView>,
	public CVirtualListView<CIpcView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	bool CanCopy() const override {
		return m_List.GetSelectedCount() > 0;
	}
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override {
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CIpcView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CIpcView>)
		CHAIN_MSG_MAP(CViewBase<CIpcView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_IPC_GOTOCLIENT, OnGoToClient)
		COMMAND_ID_HANDLER(ID_IPC_GOTOSERVER, OnGoToServer)
		CHAIN_MSG_MAP_ALT(CViewBase<CIpcView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Client, ClientPid, Server, ServerPid, Kind, Interface, Endpoint, Protocol, Count, Rate,
		FirstSeen, LastSeen, _Count };

	struct Row {
		WinSys::IpcLink Link;
		std::wstring Key;	// the kind, the client and (ALPC) the server or (RPC) what was called: the server may come later
		double Rate{ 0 };	// per second, over the last refresh
		int NewTicks{ 0 };	// > 0: still highlighted as new
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToClient(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToServer(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the IpcColumns setting
	void SaveColumns() const;

	// the tracker, the first time the tab is shown
	void StartTracker();
	// the tracker's links into m_All, by row key; elapsed: seconds since the previous merge, for the rates
	void Merge(std::vector<WinSys::IpcLink> links, double elapsed);
	static std::wstring KeyOf(WinSys::IpcLink const& link);
	std::wstring GetText(Row const& row, Column column) const;
	std::wstring GetProcessName(uint32_t pid) const;
	std::wstring const& GetInterfaceName(GUID const& iface) const;
	bool Matches(Row const& row) const;
	// the list's rows: the ones that match the filter, sorted as the list is; selected: the key of the row to select
	void ShowRows(std::wstring const& selected);
	std::wstring GetSelectedKey() const;
	Row const* GetSelected() const;
	void SelectRow(std::wstring const& key);
	void UpdateStatus();

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::unique_ptr<WinSys::IpcTracker> m_Tracker;	// from the first time the tab is shown
	DWORD m_TrackerError{ 0 };	// why it didn't start
	std::vector<Row> m_All;	// the last merge
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	mutable std::unordered_map<std::wstring, std::wstring> m_InterfaceNames;	// by the GUID's text
	int64_t m_LastMerge{ 0 };	// QPC
	bool m_Loaded{ false };	// merged at least once: later rows are new
	bool m_Active{ false };
	// merged while hidden: the rows (filtered and sorted) are rebuilt when shown, with the selection from before
	bool m_RowsPending{ false };
	std::wstring m_PendingSelection;
};
