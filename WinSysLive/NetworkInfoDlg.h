#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <functional>

//
// System > Network Information: a modal, resizable dialog, read-only (but for Flush DNS Cache). A tab per table over
// one list: Listening Ports (WinSys ConnectionInfo::Read's listening TCP sockets and UDP endpoints, with their
// processes), DNS Cache (WinSys NetworkTables::ReadDnsCache, WMI's MSFT_DNSClientCache), Neighbors (the ARP and IPv6
// neighbor caches) and Routes (with the interfaces' metrics), all read when the dialog opens and on Refresh. The list
// shows the selected tab's rows (its columns set again on a tab change), sorts by any column (numbers as numbers),
// and is filtered by any column; Copy copies the selected rows, or all shown, with the column names. Flush DNS Cache
// (on the DNS tab, after a confirmation) empties the cache, as ipconfig /flushdns, and reads it again. Double-clicking a
// listening port goes to its process. The list's context menu has Go to Process (listening ports), Flush DNS Cache
// (the DNS cache) and Copy.
//
class CNetworkInfoDlg :
	public CDialogImpl<CNetworkInfoDlg>,
	public CDialogResize<CNetworkInfoDlg>,
	public CDpiDialogResize<CNetworkInfoDlg> {
public:
	enum { IDD = IDD_NETINFO };

	// processName: a running process's image name by ID; goToProcess: selects it in the Processes tab
	CNetworkInfoDlg(std::function<std::wstring(uint32_t)> processName, std::function<void(uint32_t)> goToProcess)
		: m_ProcessName(std::move(processName)), m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CNetworkInfoDlg)
		DLGRESIZE_CONTROL(IDC_NI_TABS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_NI_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_NI_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_NI_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_NI_FLUSH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_NI_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_NI_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CNetworkInfoDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_HANDLER(IDC_NI_TABS, TCN_SELCHANGE, OnTabChanged)
		NOTIFY_HANDLER(IDC_NI_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_NI_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_HANDLER(IDC_NI_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_NI_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_NI_FLUSH, OnFlush)
		COMMAND_ID_HANDLER(IDC_NI_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_NETINFO_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CNetworkInfoDlg>)
		CHAIN_MSG_MAP(CDialogResize<CNetworkInfoDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Tab { Listening, Dns, Neighbors, Routes, _Count };

	struct ColumnDef {
		PCWSTR Name;
		int Width;	// at 96 DPI
		bool Numeric{ false };	// sorts as a number, right-aligned
	};
	struct Row {
		std::vector<std::wstring> Cells;
		uint32_t Pid{ 0 };	// a listening port's process
	};
	struct Table {
		std::vector<ColumnDef> Columns;
		std::vector<Row> Rows;
		std::wstring Error;	// why it couldn't be read
		int SortColumn{ 0 };
		bool SortAscending{ true };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFlush(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void LoadListening();
	void LoadDns();
	void LoadNeighbors();
	void LoadRoutes();
	Table& Current();
	// the selected tab's columns and rows (those the filter matches), sorted
	void ShowTab();
	void ShowRows();
	void UpdateStatus();

	std::function<std::wstring(uint32_t)> m_ProcessName;
	std::function<void(uint32_t)> m_GoToProcess;
	CTabCtrl m_Tabs;
	CListViewCtrl m_List;
	Table m_Tables[(int)Tab::_Count];
	std::vector<int> m_Shown;	// row indices of the current table, in display order; the list's item data
	std::wstring m_Filter;	// lowercase
};
