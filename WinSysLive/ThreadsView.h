#pragma once

#include "BarFont.h"

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include "ThreadCommands.h"
#include "Symbols.h"
#include <WinSys/Keys.h>
#include <WinSys/Thread.h>
#include <VirtualListView.h>

//
// The Threads tab: every thread of every process (some 9,000 here), from the frame's snapshot (each process entry's
// SYSTEM_EXTENDED_THREAD_INFORMATION array), with the Threads page's columns (CThreadsPage) and the process's name and
// PID. Taken only while the tab is active (a refresh of all the threads costs a few milliseconds; an inactive tab isn't
// seen), so CPU and the deltas are unknown for the first refresh after it's shown again.
//
// What a thread handle gives (cycles, ideal CPU, I/O and memory priority, the service, the start address when the snapshot
// has none, the COM apartment) is read only for the rows painted, the others' when sorting by such a column (or
// exporting), once a refresh: a handle to each of 9,000 threads, read every second, would cost more than the snapshot. The
// handles are opened when first needed and closed after HandleIdle unused (an open handle keeps an exited thread's object
// alive). Cycles Delta is known for a row read on consecutive refreshes. The COM apartment reads the threads' TEBs, through
// a handle to the process opened once (Implicit MTA needs all of a process's threads' flags: read for the processes
// whose rows are painted). The filter matches the process, the PIDs and TIDs, the state and the wait reason, and a
// service or start address already known, never querying for it (a lookup of every thread's symbol would load every
// process's). The start addresses show their symbols (SymbolEngine) for the rows painted.
//
// A row's context menu is the Threads page's (ThreadCommands: Stack, the double-click, Wait Chain, Suspend or Resume,
// Terminate, Set Priority, Affinity) with Go to Process; Del terminates the selected thread, after asking.
//
class CThreadsView :
	public CViewBase<CThreadsView>,
	public CVirtualListView<CThreadsView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override {
		return L"Threads";
	}
	bool CanCopy() const override {
		return m_List.GetSelectedCount() > 0;
	}
	bool CanExport() const override {
		return true;
	}
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void OnFrameIdle() override;
	void SaveSettings() override {
		SaveColumns();
	}
	void OnDpiChanged(UINT /*dpi*/) override;

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);
	void UpdateLayout(BOOL resizeBars = TRUE);

	BEGIN_MSG_MAP(CThreadsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_SYMBOLS_RESOLVED, OnSymbolsResolved)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		// posted to the view itself by a double-click (the frame's commands come through ALT_MSG_MAP(1))
		COMMAND_ID_HANDLER(ID_THREAD_STACK, OnThreadCommand)
		// the buttons beside the filter (their toolbar is the view's)
		COMMAND_ID_HANDLER(ID_THREADS_HIDEIDLE, OnHide)
		COMMAND_ID_HANDLER(ID_THREADS_HIDEIDLEPROCESS, OnHide)
		CHAIN_MSG_MAP(CVirtualListView<CThreadsView>)
		CHAIN_MSG_MAP(CViewBase<CThreadsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_COLUMN_RESET, OnResetColumns)
		COMMAND_ID_HANDLER(ID_KILL, OnKill)
		COMMAND_ID_HANDLER(ID_THREADS_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_THREADS_HIDEIDLE, OnHide)
		COMMAND_ID_HANDLER(ID_THREADS_HIDEIDLEPROCESS, OnHide)
		COMMAND_ID_HANDLER(ID_THREAD_STACK, OnThreadCommand)
		COMMAND_ID_HANDLER(ID_THREAD_WAITCHAIN, OnThreadCommand)
		COMMAND_ID_HANDLER(ID_THREAD_SUSPEND, OnThreadCommand)
		COMMAND_ID_HANDLER(ID_THREAD_RESUME, OnThreadCommand)
		COMMAND_ID_HANDLER(ID_THREAD_TERMINATE, OnThreadCommand)
		COMMAND_ID_HANDLER(ID_THREAD_AFFINITY, OnThreadCommand)
		COMMAND_RANGE_HANDLER(ID_THREAD_PRIORITY_TIMECRITICAL, ID_THREAD_PRIORITY_IDLE, OnThreadCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CThreadsView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Process, Pid, Id, CPU, CpuTime, KernelTime, UserTime, Cycles, CyclesDelta, State, WaitReason, Priority, BasePriority,
		IdealCpu, IoPriority, MemoryPriority, ContextSwitches, ContextSwitchDelta, Service, StartAddress, Created, ComApartment,
		_Count
	};
	// a handle unused this long is closed (ms)
	static constexpr ULONGLONG HandleIdle = 60000;

	struct ThreadState;
	// copied from the snapshot (whose buffer the next update overwrites)
	struct Row {
		WinSys::ProcessOrThreadKey Key;
		ThreadState* State{ nullptr };	// owned by m_States or m_Exited (unique_ptrs: the address stays)
		int32_t CPU{ -1 };	// ProcessInfo::CPU units; -1: not known (the first refresh)
		int64_t KernelTime{ 0 }, UserTime{ 0 };
		uint32_t RunState{ 0 }, WaitReason{ 0 };	// KTHREAD_STATE, KWAIT_REASON
		int Priority{ 0 }, BasePriority{ 0 };
		uint32_t ContextSwitches{ 0 };
		int64_t ContextSwitchDelta{ -1 };
		int64_t CpuTimeDelta{ -1 };	// -1: not known
		void* StartAddress{ nullptr };	// the snapshot's (zero when not elevated)
		bool New{ false }, Exited{ false };
	};
	// per thread, kept between refreshes
	struct ThreadState {
		uint32_t Pid{ 0 };
		int64_t ProcessCreated{ 0 };
		std::wstring ProcessName, ImagePath;
		int64_t CpuTime{ 0 };
		uint32_t ContextSwitches{ 0 };
		bool Known{ false };	// the values above are from a previous refresh
		int NewTicks{ 0 }, ExitedTicks{ 0 };
		Row Last;	// shown as it was for a while once the thread exits
		// read for the rows needing them (Read), at most once a refresh
		std::unique_ptr<WinSys::Thread> Handle;
		bool HandleTried{ false };
		ULONGLONG LastUsed{ 0 };
		uint32_t ReadRefresh{ 0 };	// m_Refresh when the values below were read; 0: never
		WinSys::CpuNumber IdealCpu{ 0, 0xff };
		WinSys::IoPriority IoPriority{ WinSys::IoPriority::Unknown };
		int MemoryPriority{ -1 };
		uint64_t Cycles{ 0 };
		int64_t CyclesDelta{ -1 };
		uint64_t Teb{ 0 };
		WinSys::ComFlags Com{ WinSys::ComFlags::Error };
		std::optional<std::wstring> Service;	// once: a thread's service doesn't change
		std::optional<void*> StartAddress;		// once, when the snapshot has none
	};
	// per process, for the COM apartments
	struct ProcessState {
		wil::unique_handle Handle;	// PROCESS_VM_READ; null if it can't be opened
		bool Wow64{ false };
		uint32_t MtaRefresh{ 0 };	// m_Refresh when HasMta was found
		bool HasMta{ false };
		uint32_t SeenRefresh{ 0 };
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolsResolved(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnThreadCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// Hide Idle Threads, Hide Idle Process: the buttons (which check themselves) or the View menu
	LRESULT OnHide(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the buttons and the View menu's items checked as the options are
	void UpdateHideChecks();
	// the buttons' icons and font in the toolbar's DPI, and their band's size
	void LayoutHideBar();
	// it used no CPU and made no context switch since the last refresh (not known on the first: not idle)
	static bool IsIdle(Row const& row);

	// the threads from the frame's snapshot
	void Update();
	// the handle's values of the row's thread, once a refresh
	void Read(Row const& row) const;
	// the process's handle for its threads' TEBs (opened once); null if it can't be read
	ProcessState* GetProcess(ThreadState const& state) const;
	// a thread of the process is in the MTA (its threads' COM flags read for it)
	bool HasMta(ThreadState const& state) const;
	static bool NeedsHandle(Column column);
	std::wstring GetText(Row const& row, Column column) const;
	bool Matches(Row const& row) const;
	// the rows the filter keeps, sorted, the selection kept (by thread key)
	void ShowRows(WinSys::ProcessOrThreadKey const& selected);
	Row const* GetSelected() const;
	WinSys::ProcessOrThreadKey GetSelectedKey() const;
	void SelectRow(WinSys::ProcessOrThreadKey const& key);
	ThreadCommands::Target GetTarget(Row const& row) const;
	bool IsShown(Column column) const;
	void UpdateStatus();

	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	CListViewCtrl m_List;
	CFilterBox m_Filter;
	CToolBarCtrl m_HideBar;	// in the rebar after the filter: Hide Idle Threads, Hide Idle Process
	CBarFont m_HideBarFont;
	bool m_HideIdle{ false };			// the ThreadsHideIdle setting
	bool m_HideIdleProcess{ true };		// the ThreadsHideIdleProcess setting
	size_t m_Hidden{ 0 };				// rows the options hide, for the status line
	int m_MenuColumn{ -1 };
	std::vector<Row> m_All;	// every row of the last refresh
	std::vector<Row const*> m_Rows;	// into m_All: the list's, in display order
	std::unordered_map<WinSys::ProcessOrThreadKey, std::unique_ptr<ThreadState>> m_States;
	// the threads that exited, still shown for a while (Highlight::ExitedTicks)
	std::unordered_map<WinSys::ProcessOrThreadKey, std::unique_ptr<ThreadState>> m_Exited;
	mutable std::unordered_map<WinSys::ProcessOrThreadKey, ProcessState> m_Processes;	// by process key
	std::unordered_map<WinSys::ProcessOrThreadKey, std::vector<Row const*>> m_ByProcess;	// every refresh, for HasMta
	uint32_t m_Refresh{ 0 };	// counts the refreshes taken
	size_t m_ProcessCount{ 0 };
	LARGE_INTEGER m_PrevTime{};	// of the previous refresh (QPC); 0: none (the first, or the first since shown again)
	bool m_Active{ false };
	bool m_ShowCom{ false };	// COM Apartment is shown: Read reads the TEBs
};
