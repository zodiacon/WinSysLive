#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include "ConnectionRows.h"
#include "AddressResolver.h"
#include "NetworkTracking.h"
#include <VirtualListView.h>

//
// The Network tab: every process's TCP connections and UDP endpoints (WinSys ConnectionInfo, merged by ConnectionRows), read again on every refresh tick
// (also while hidden, so the highlighting stays right) and merged: new ones green for two refreshes, closed ones red
// once. The process column has the process's icon (the frame's, from IMainFrame::GetProcessIcon). The filter box
// matches the process, PID, protocol, addresses, ports and state. Double-click or Go to Process selects the process;
// Copy copies the selected row. The selection is kept by connection. The Sent and Received columns and their rates come
// from the shared ETW tracker (NetworkTracking, elevated only), held while one of them is shown: the bytes since it
// started (the status line says when), TCP per connection, UDP per local port.
//
class CNetworkView :
	public CViewBase<CNetworkView>,
	public CVirtualListView<CNetworkView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
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

	BEGIN_MSG_MAP(CNetworkView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_ADDRESS_RESOLVED, OnAddressResolved)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CNetworkView>)
		CHAIN_MSG_MAP(CViewBase<CNetworkView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_NETWORK_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_NETWORK_CLOSE, OnCloseConnection)
		COMMAND_ID_HANDLER(ID_NETWORK_RESOLVEAGAIN, OnResolveAgain)
		CHAIN_MSG_MAP_ALT(CViewBase<CNetworkView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Process, Pid, Protocol, LocalAddress, LocalPort, RemoteAddress, RemotePort, State, Created,
		Sent, Received, SendRate, ReceiveRate, _Count };
	using Connection = ConnectionRow;

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnAddressResolved(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the NetworkColumns setting
	void SaveColumns() const;
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseConnection(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResolveAgain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::wstring GetText(Connection const& c, Column column) const;
	std::wstring GetProcessName(uint32_t pid) const;
	bool Matches(Connection const& c) const;
	// the list's rows: the connections that match the filter, sorted as the list is; selected: the connection to select
	void ShowRows(std::optional<ConnectionRows::Key> const& selected);
	std::optional<ConnectionRows::Key> GetSelectedKey() const;
	Connection const* GetSelected() const;
	void SelectConnection(ConnectionRows::Key const* key);
	void UpdateStatus();
	// the tracker is held while a byte column is shown
	bool ShowsBytes() const;
	void UpdateTracker();
	static ConnectionBytes::Value ByteValue(Column column);

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::vector<Connection> m_All;	// the last merge: every connection, the closed ones once more
	std::vector<Connection const*> m_Rows;	// into m_All: the list's rows, in display order
	bool m_Loaded{ false };	// read at least once: later connections are new
	bool m_Active{ false };
	// merged while hidden: the rows (filtered and sorted) are rebuilt when shown, with the selection from before
	bool m_RowsPending{ false };
	std::optional<ConnectionRows::Key> m_PendingSelection;
	std::shared_ptr<WinSys::ProcessNetworkTracker> m_Network;
	DWORD m_NetworkError{ 0 };	// why the tracker didn't start
	ConnectionBytes m_Bytes;	// the rows', from the tracker on every refresh
};
