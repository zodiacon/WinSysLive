#pragma once

#include "ProcessPages.h"
#include <array>

//
// The TCP/IP page: the process's TCP connections (listening ones too) and UDP endpoints, IPv4 and IPv6, from the
// IP helper's owner tables (GetExtendedTcpTable/GetExtendedUdpTable, which need no elevation). They're read again on
// every sample while the page is active; new ones are highlighted for two samples (not on the first read), and closed
// ones are shown once more in red. The selection (several rows, for Copy) is kept by connection. Copy and Copy All put tab-separated lines on
// the clipboard.
//
class CProcTcpIpPage :
	public CPropertyPageImpl<CProcTcpIpPage>,
	public CDynamicDialogLayout<CProcTcpIpPage>,
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

	BEGIN_MSG_MAP(CProcTcpIpPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_TCPIP_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_TCPIP_COPYALL, OnCopy)
		NOTIFY_HANDLER(IDC_TCPIP_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_TCPIP_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CProcTcpIpPage>)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcTcpIpPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcTcpIpPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	enum class Column { Protocol, LocalAddress, LocalPort, RemoteAddress, RemotePort, State, Created, _Count };
	enum class Protocol : uint8_t { Tcp, Tcp6, Udp, Udp6 };

	// what tells connections apart from one sample to the next
	struct Key {
		Protocol Proto;
		std::array<BYTE, 16> LocalAddress, RemoteAddress;	// IPv4 in the first 4 bytes
		uint32_t LocalScope, RemoteScope;
		uint16_t LocalPort, RemotePort;	// host order

		auto operator<=>(Key const&) const = default;
	};

	struct Connection {
		Key Id;
		DWORD State;		// MIB_TCP_STATE; 0 for UDP
		int64_t Created;	// FILETIME, 0 if unknown
		std::wstring LocalText, RemoteText;	// the addresses, formatted once
		int NewTicks{ 0 };	// > 0: still highlighted as new
		bool Closed{ false };	// shown once more, then removed
	};

	// reads the process's connections and merges them into m_Rows; false (with the status saying why) on failure
	bool Load();
	std::wstring GetText(Connection const& c, Column column) const;
	// the selected rows' keys, the focused one first
	std::vector<Key> GetSelectedKeys() const;
	void SelectConnections(std::vector<Key> const& keys);
	void UpdateButtons();

	ProcessProperties const& m_Props;
	ProcessSnapshot const& m_Snapshot;
	CListViewCtrl m_List;
	std::vector<Connection> m_Rows;	// in display order
	bool m_Loaded{ false };	// read at least once: later connections are new
	bool m_Exited{ false };	// the process is gone: the rows stay as they were
};
