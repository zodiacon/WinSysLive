#pragma once

#include <VirtualListView.h>
#include <WinSys/Keys.h>
#include <WinSys/Thread.h>
#include "ColumnLayout.h"
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
// ThreadColumns setting.
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
	// takes the threads from a snapshot's (SystemExtendedProcessInformation) entry of the process; nullptr: no process
	void Update(SYSTEM_PROCESS_INFORMATION const* info);

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	int GetRowImage(HWND, int row, int col) const;

	BEGIN_MSG_MAP(CThreadsPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CThreadsPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

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
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	std::wstring GetText(ThreadRow const& row, Column column) const;
	void SelectThread(uint32_t tid);

	// the shown columns in display order (the header's order, which the user can change by dragging)
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	CListViewCtrl m_List;
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
