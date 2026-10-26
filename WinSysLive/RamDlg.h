#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/PhysicalMemory.h>
#include <thread>
#include <mutex>

//
// System > RAM: a modal, resizable dialog, read-only, as RAMMap's Use Counts: physical memory by use (process private,
// mapped file, paged pool...) and by list (active, standby, modified, free...), from every page's entry in the PFN
// database (WinSys PageUseCounts::Read, which needs elevation, so the menu item is enabled only then). Reading it takes
// seconds (about 6 s for 128 GB), so it runs on a worker thread when the dialog opens and on Scan Again; pages move
// between lists meanwhile, so the totals aren't a snapshot. A row per known use and any other use that has pages, then
// Unused (the zeroed, free and bad pages, whose use means nothing) and Total, which stays last when sorting. Copy
// copies the selected rows, or all of them.
//
class CRamDlg :
	public CDialogImpl<CRamDlg>,
	public CDialogResize<CRamDlg>,
	public CDpiDialogResize<CRamDlg> {
public:
	enum { IDD = IDD_RAM };

	static constexpr UINT WM_RAM_SCANNED = WM_APP + 0x1A0;

	BEGIN_DLGRESIZE_MAP(CRamDlg)
		DLGRESIZE_CONTROL(IDC_RAM_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_RAM_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_RAM_SCAN, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_RAM_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CRamDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_RAM_SCANNED, OnScanned)
		NOTIFY_HANDLER(IDC_RAM_LIST, LVN_COLUMNCLICK, OnColumnClick)
		COMMAND_ID_HANDLER(IDC_RAM_SCAN, OnScanAgain)
		COMMAND_ID_HANDLER(IDC_RAM_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CRamDlg>)
		CHAIN_MSG_MAP(CDialogResize<CRamDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Usage, Total, Share, Active, Standby, Modified, ModifiedNoWrite, Transition, Zeroed, Free, Bad, _Count };

	// row keys: a PageUse value, else these
	static constexpr int UnusedKey = (int)WinSys::PageUse::_Count, TotalKey = UnusedKey + 1;

	struct Row {
		int Key;
		std::array<uint64_t, (size_t)WinSys::PageList::_Count> Pages{};
		uint64_t Total{ 0 };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnScanAgain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void StartScan();
	// the rows from the last scan's counts, keeping the selection by key, sorted
	void ShowRows();
	static std::wstring GetUseName(int key);
	// in bytes, or the Share column's percentage times 100
	uint64_t GetValue(Row const& row, Column column) const;
	std::wstring GetText(Row const& row, Column column) const;
	static int CALLBACK Compare(LPARAM index1, LPARAM index2, LPARAM param);
	void UpdateStatus();

	CListViewCtrl m_List;
	std::optional<WinSys::PageUseCounts> m_Counts;	// the last scan's
	std::vector<Row> m_Rows;	// each list item's data is its index here
	int64_t m_ScanTime{ 0 };	// FILETIME of the last scan; 0: none yet
	uint64_t m_ScanDuration{ 0 };	// ms
	DWORD m_ScanError{ 0 };	// the last scan's, if it failed
	int m_SortColumn{ -1 };	// -1: the uses' order
	bool m_SortAscending{ true };

	std::jthread m_Worker;
	std::atomic<bool> m_Cancel{ false };	// set when the dialog closes: a scan takes seconds
	bool m_Scanning{ false };
	std::mutex m_ResultLock;
	std::optional<WinSys::PageUseCounts> m_Result;
	DWORD m_ResultError{ 0 };
	uint64_t m_ResultDuration{ 0 };
};
