#pragma once

#include "ProcessPages.h"
#include <WinSys/Heaps.h>

//
// The Heaps page: the process's heaps (WinSys ProcessHeaps, a non-invasive query, about a millisecond), read again on
// every sample while the page is active: address, kind (NT or segment heap), class (the default heap said), committed
// and reserved sizes, the bytes in use and their share of the committed (NT heaps only: the segment heap doesn't report
// them), segments and flags. New heaps are highlighted for a while (not on the first read), destroyed ones shown once
// more in red, as on the TCP/IP page; the selection is kept by address. Copy and Copy All put tab-separated lines on
// the clipboard. Other users' processes need elevation, protected ones can't be read, and neither can 32-bit ones (the
// status line says which).
//
class CProcHeapsPage :
	public CPropertyPageImpl<CProcHeapsPage>,
	public CDpiDynamicDialogLayout<CProcHeapsPage>,
	public CVirtualListView<CProcHeapsPage> {
public:
	enum { IDD = IDD_PROC_HEAPS };

	CProcHeapsPage(ProcessProperties const& props, ProcessSnapshot const& snapshot);

	// from the sheet, while the page is active
	void OnSample(ProcessSnapshot const& snapshot);

	// CPropertyPageImpl: 0 allows the activation
	int OnSetActive();

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CProcHeapsPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_HEAPS_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_HEAPS_COPYALL, OnCopy)
		NOTIFY_HANDLER(IDC_HEAPS_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_HEAPS_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CProcHeapsPage>)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcHeapsPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcHeapsPage>)
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

	enum class Column { Address, Kind, Class, Committed, Reserved, Used, UsedPercent, Segments, Flags, _Count };

	struct Row : WinSys::HeapInfo {
		int NewTicks{ 0 };		// highlighted while above 0
		int GoneTicks{ 0 };		// destroyed: shown in red while above 0
	};

	// reads the heaps and merges them into m_Rows; false (with the status line saying why) on failure
	bool Load();
	// why the heaps can't be read, from the query's error
	std::wstring ErrorText(DWORD error) const;
	std::wstring GetText(Row const& row, Column column) const;
	std::vector<uint64_t> GetSelectedAddresses() const;
	void SelectAddresses(std::vector<uint64_t> const& addresses);
	void UpdateStatus();
	void UpdateButtons();

	ProcessProperties const& m_Props;
	ProcessSnapshot const& m_Snapshot;
	CListViewCtrl m_List;
	std::vector<Row> m_Rows;	// in display order
	bool m_Loaded{ false };	// read at least once: later heaps are new
	bool m_Exited{ false };	// the process is gone: the rows stay as they were
};
