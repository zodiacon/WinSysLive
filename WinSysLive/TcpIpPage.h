#pragma once

#include "ProcessPages.h"
#include "ConnectionRows.h"
#include "AddressResolver.h"
#include "NetworkTracking.h"

//
// The TCP/IP page: the process's TCP connections (listening ones too) and UDP endpoints, IPv4 and IPv6 (WinSys
// ConnectionInfo, which needs no elevation; merged as the Network view's, ConnectionRows). They're read again on
// every sample while the page is active; new ones are highlighted for two samples (not on the first read), and closed
// ones are shown once more in red. The selection (several rows, for Copy) is kept by connection. Copy and Copy All put tab-separated lines on
// the clipboard. Sent, Received and their rates come from the shared ETW tracker (NetworkTracking), so only when
// elevated, held while the page exists (the sheet's network graph holds it too).
//
class CProcTcpIpPage :
	public CPropertyPageImpl<CProcTcpIpPage>,
	public CDpiDynamicDialogLayout<CProcTcpIpPage>,
	public CVirtualListView<CProcTcpIpPage> {
public:
	enum { IDD = IDD_PROC_TCPIP };

	CProcTcpIpPage(ProcessProperties const& props, ProcessSnapshot const& snapshot);

	// from the sheet, while the page is active
	void OnSample(ProcessSnapshot const& snapshot);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	// the selected rows' menu: Close Connection, Resolve Again, Copy
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CProcTcpIpPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_ADDRESS_RESOLVED, OnAddressResolved)
		COMMAND_ID_HANDLER(IDC_TCPIP_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_TCPIP_COPYALL, OnCopy)
		COMMAND_ID_HANDLER(ID_NETWORK_CLOSE, OnCloseConnections)
		COMMAND_ID_HANDLER(ID_NETWORK_RESOLVEAGAIN, OnResolveAgain)
		NOTIFY_HANDLER(IDC_TCPIP_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_TCPIP_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CProcTcpIpPage>)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcTcpIpPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcTcpIpPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnAddressResolved(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		m_List.Invalidate();
		return 0;
	}
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseConnections(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResolveAgain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	enum class Column { Protocol, LocalAddress, LocalPort, RemoteAddress, RemotePort, State, Created,
		Sent, Received, SendRate, ReceiveRate, _Count };
	using Protocol = WinSys::ConnectionProtocol;
	using Key = ConnectionRows::Key;
	using Connection = ConnectionRow;

	// reads the process's connections and merges them into m_Rows; false (with the status saying why) on failure
	bool Load();
	std::wstring GetText(Connection const& c, Column column) const;
	// the selected rows' keys, the focused one first
	std::vector<Key> GetSelectedKeys() const;
	// the selected rows that can be closed (IPv4 TCP, not closed already), copied: samples go on while a message box
	// is up, and a merge moves the rows
	std::vector<Connection> GetClosableSelection() const;
	void SelectConnections(std::vector<Key> const& keys);
	void UpdateButtons();

	ProcessProperties const& m_Props;
	ProcessSnapshot const& m_Snapshot;
	CListViewCtrl m_List;
	std::vector<Connection> m_Rows;	// in display order
	bool m_Loaded{ false };	// read at least once: later connections are new
	bool m_Exited{ false };	// the process is gone: the rows stay as they were
	std::shared_ptr<WinSys::ProcessNetworkTracker> m_Network;	// null when not elevated
	ConnectionBytes m_Bytes;
};
