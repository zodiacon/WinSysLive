#pragma once

#include "ProcessPages.h"
#include <VirtualListView.h>

//
// The Statistics page: every counter of the process in one list, in sections (CPU, Memory, I/O, Handles and Objects):
// times, cycles, context switches, threads; private bytes, virtual size, working set, page faults and pool usage, each
// with its peak; I/O operations and bytes by type; handles, GDI and USER objects with their peaks, and the priorities.
// Most come from the sheet's snapshot; the peaks of handles and GUI objects and the priorities are queried through a
// limited-access handle opened once. Fed every sample while active (and when activated, with the sheet's last one);
// the Change column is each counter's change since the previous sample shown. Once the process is gone, its last values
// stay. Copy copies the selected row, Copy All every row with the sections as [titles].
//
class CProcStatsPage :
	public CPropertyPageImpl<CProcStatsPage>,
	public CDpiDynamicDialogLayout<CProcStatsPage>,
	public CVirtualListView<CProcStatsPage> {
public:
	enum { IDD = IDD_PROC_STATS };

	CProcStatsPage(ProcessProperties const& props, ProcessSnapshot const& snapshot);

	void OnSample(ProcessSnapshot const& snapshot);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const*) {}

	BEGIN_MSG_MAP(CProcStatsPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_STATS_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_STATS_COPYALL, OnCopy)
		NOTIFY_HANDLER(IDC_STATS_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CProcStatsPage>)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcStatsPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcStatsPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	enum class Kind { Header, Number, Bytes, Time, Text };
	struct Row {
		PCWSTR Name;
		Kind Kind;
		int64_t Value{ 0 };
		std::wstring Text;	// Kind::Text's value
		std::optional<int64_t> Change;	// since the previous sample shown; none on the first, and for text
		bool Known{ true };	// a queried value that couldn't be (the process can't be opened)
	};

	// the counters, in the list's order, from the process's entry and the handle
	std::vector<Row> Collect(SYSTEM_PROCESS_INFORMATION const& info) const;
	static std::wstring FormatValue(Kind kind, int64_t value);

	ProcessProperties const& m_Props;
	ProcessSnapshot const& m_Snapshot;
	std::unique_ptr<WinSys::Process> m_Process;	// limited query access; null if it can't be opened
	CListViewCtrl m_List;
	CFont m_BoldFont;	// the section rows', made from the list's font (again when that changes: a new DPI)
	HFONT m_BoldFrom{ nullptr };
	std::vector<Row> m_Rows;
	bool m_Fresh{ true };	// the next sample has no previous one to change from (the page was just shown)
};
