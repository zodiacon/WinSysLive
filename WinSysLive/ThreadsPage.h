#pragma once

#include <VirtualListView.h>
#include <WinSys/Keys.h>
#include <WinSys/Thread.h>

//
// The Threads tab of the Processes view's lower pane: the threads of the selected process, read from the
// frame's snapshot (which has every process's threads after its entry) on every refresh. CPU, cycles and the
// context switch delta come from the previous refresh's values, kept per thread. The values the snapshot
// doesn't have (ideal CPU, I/O and memory priority, cycles, service) are queried through a handle to each
// thread, opened once while its process is shown.
//
class CThreadsPage :
	public CWindowImpl<CThreadsPage>,
	public CVirtualListView<CThreadsPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_ThreadsPage", 0, COLOR_WINDOW)

	// nullptr: no process
	void SetProcess(std::shared_ptr<ProcessInfoEx> process);
	// takes the threads from the current snapshot
	void Refresh();

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;

	BEGIN_MSG_MAP(CThreadsPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
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
	};
	// what's kept per thread between refreshes
	struct ThreadState {
		int64_t CpuTime;
		uint32_t ContextSwitches;
		uint64_t Cycles{ 0 };
		bool Known{ false };	// the values above are from a previous refresh
		std::unique_ptr<WinSys::Thread> Handle;
		std::wstring Service;	// queried once: a thread's service doesn't change
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	std::wstring GetText(ThreadRow const& row, Column column) const;
	void SelectThread(uint32_t tid);

	CListViewCtrl m_List;
	std::shared_ptr<ProcessInfoEx> m_Process;
	std::vector<ThreadRow> m_Rows;	// in display order
	std::unordered_map<WinSys::ProcessOrThreadKey, ThreadState> m_Threads;	// node-based: ThreadRow::Service stays valid
	LARGE_INTEGER m_PrevTime{};	// of the previous refresh (QPC), 0 if none
};
