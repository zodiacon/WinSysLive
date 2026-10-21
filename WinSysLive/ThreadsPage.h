#pragma once

#include <VirtualListView.h>
#include <WinSys/Keys.h>
#include <WinSys/Thread.h>
#include "ColumnLayout.h"
#include "FilterBox.h"
#include "Symbols.h"
#include "resource.h"

struct ProcessInfoEx;

//
// The Threads tab of the Processes view's lower pane, and the Threads page of a process's property sheet: the threads
// of a process, read from a snapshot (which has every process's threads after its entry) on every refresh: the
// frame's for the lower pane, the sheet's own for the sheet, which runs on another thread. CPU, cycles and the
// context switch delta come from the previous refresh's values, kept per thread. The values the snapshot
// doesn't have (ideal CPU, I/O and memory priority, cycles, service) are queried through a handle to each
// thread, opened once while its process is shown. New threads are highlighted for a while, exited ones are shown
// (with their last values) for one refresh. The columns are chosen from the header's context menu and kept in the
// ThreadColumns setting. The start addresses show their symbols (SymbolEngine) once resolved, with the address in a
// tooltip; Stack (a row's context menu, or a double-click) shows a thread's stack (CStackDlg). The context menu also
// suspends, resumes and terminates the thread, after asking.
//
class CThreadsPage :
	public CWindowImpl<CThreadsPage>,
	public CVirtualListView<CThreadsPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_ThreadsPage", 0, COLOR_WINDOW)

	// the columns from this layout (as in the ThreadColumns setting) instead of the setting, and changes to them not
	// saved: for a page on another thread than the settings'. Call before Create
	void UseColumns(std::wstring layout);
	// nullptr: no process
	void SetProcess(std::shared_ptr<ProcessInfoEx> process);
	// takes the threads from the frame's current snapshot
	void Refresh();
	// Ctrl+F while the page has the focus
	void FocusFilter() {
		m_Filter.Focus();
	}
	void SetListFont(HFONT font) {
		m_List.SetFont(font);
		m_Filter.SetFont(font);
		// a hidden page's header isn't all repainted otherwise
		m_List.GetHeader().Invalidate();
	}
	// the Kill command (the toolbar, Del) while the page has the focus: terminates the selected thread, after asking
	bool CanTerminateSelected() const;
	void TerminateSelected();
	// takes the threads from a snapshot's (SystemExtendedProcessInformation) entry of the process; nullptr: no process.
	// time: the snapshot's (QPC), for the rates; 0 for now. The same snapshot twice is ignored
	void Update(SYSTEM_PROCESS_INFORMATION const* info, int64_t time = 0);

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	int GetRowImage(HWND, int row, int col) const;

	BEGIN_MSG_MAP(CThreadsPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_PAGE_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_PAGE_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SYMBOLS_RESOLVED, OnSymbolsResolved)
		COMMAND_ID_HANDLER(ID_THREAD_STACK, OnStack)
		COMMAND_ID_HANDLER(ID_THREAD_WAITCHAIN, OnWaitChain)
		COMMAND_ID_HANDLER(ID_THREAD_TERMINATE, OnThreadControl)
		COMMAND_ID_HANDLER(ID_THREAD_SUSPEND, OnThreadControl)
		COMMAND_ID_HANDLER(ID_THREAD_RESUME, OnThreadControl)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(TTN_GETDISPINFO, OnGetTipText)
		CHAIN_MSG_MAP(CVirtualListView<CThreadsPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	// puts the column layout in its setting and saves it (also done when the page is destroyed)
	void SaveColumns() const;

private:
	enum class Column {
		Id, CPU, CpuTime, KernelTime, UserTime, Cycles, CyclesDelta, State, WaitReason, Priority, BasePriority, IdealCpu,
		IoPriority, MemoryPriority, ContextSwitches, ContextSwitchDelta, Service, StartAddress, Created,
		_Count
	};

	// copied from the snapshot (whose buffer the next update overwrites) and the thread's handle
	struct ThreadRow {
		WinSys::ProcessOrThreadKey Key;
		int32_t CPU{ -1 };	// ProcessInfo::CPU units; -1: not known yet (the first refresh)
		int64_t KernelTime, UserTime;
		uint32_t State, WaitReason;
		int Priority, BasePriority;
		uint32_t ContextSwitches;
		int64_t ContextSwitchDelta{ -1 };	// -1: not known yet
		void* StartAddress;
		// from the handle; unknown if it couldn't be opened
		bool HasHandle{ false };
		WinSys::CpuNumber IdealCpu{};
		WinSys::IoPriority IoPriority{ WinSys::IoPriority::Unknown };
		int MemoryPriority{ -1 };
		uint64_t Cycles{ 0 };
		int64_t CyclesDelta{ -1 };	// -1: not known yet
		std::wstring const* Service{ nullptr };	// in the thread's ThreadState
		bool New{ false };		// highlighted as created
		bool Exited{ false };	// highlighted as exited, for one refresh
	};
	// what's kept per thread between refreshes
	struct ThreadState {
		int64_t CpuTime;
		uint32_t ContextSwitches;
		uint64_t Cycles{ 0 };
		bool Known{ false };	// the values above are from a previous refresh
		std::unique_ptr<WinSys::Thread> Handle;
		std::wstring Service;	// queried once: a thread's service doesn't change
		int NewTicks{ 0 };	// refreshes left to highlight it as created
		ThreadRow Last;		// shown once more when the thread exits
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the rows that match the filter (any shown column contains it), sorted as the list is, with the selection kept
	void ShowRows(uint32_t selected);
	// columns: the shown ones, taken once for all the rows (from the header)
	bool Matches(ThreadRow const& row, std::vector<ColumnLayout::ShownColumn> const& columns) const;
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolsResolved(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnStack(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWaitChain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnThreadControl(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnGetTipText(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// the list's mouse moves: the tooltip asks for its text again when the mouse moves to another cell
	static LRESULT CALLBACK ListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);
	// the row and Column under the point (list client coordinates), or -1s
	std::pair<int, int> CellAt(CPoint pt) const;

	std::wstring GetText(ThreadRow const& row, Column column) const;
	// the selected row, if it's a thread that's still there
	ThreadRow const* GetSelectedThread() const;
	// waiting because it's suspended (KWAIT_REASON Suspended or WrSuspended)
	static bool IsSuspended(ThreadRow const& row);
	// not the app's own threads (suspending one could hang the app), nor the Idle and System processes'
	bool CanControl() const;
	void SelectThread(uint32_t tid);

	// the shown columns in display order (the header's order, which the user can change by dragging)
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// above the list
	std::vector<ThreadRow> m_AllRows;	// every row of the last refresh; m_Rows has the matching ones
	CToolTipCtrl m_Tip;	// the start address of a row showing its symbol
	std::wstring m_TipText;
	std::pair<int, int> m_TipCell{ -1, -1 };	// row and Column the tooltip is for
	// the process the threads are of, for symbols and stacks
	uint32_t m_Pid{ 0 };
	int64_t m_Created{ 0 };
	std::wstring m_ProcessName;
	// the thread state icons, in m_Images order
	static constexpr UINT s_StateIcons[] = { IDI_THREADS, IDI_CONTINUE, IDI_RUN, IDI_KILL, IDI_PAUSE, IDI_MEMORY };
	void AddImages();
	CImageList m_Images;	// the thread state icons, owned by the list
	std::unordered_map<UINT, int> m_IconIndices;	// IDI_* to their index in m_Images
	std::optional<std::wstring> m_Columns;	// see UseColumns
	std::shared_ptr<ProcessInfoEx> m_Process;
	std::vector<ThreadRow> m_Rows;	// in display order
	std::unordered_map<WinSys::ProcessOrThreadKey, ThreadState> m_Threads;	// node-based: ThreadRow::Service stays valid
	// the threads that exited on the last refresh: their rows point to their ThreadState's Service
	std::unordered_map<WinSys::ProcessOrThreadKey, ThreadState> m_Exited;
	LARGE_INTEGER m_PrevTime{};	// of the previous refresh (QPC), 0 if none
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
};
