#pragma once

#include "ViewBase.h"
#include <TreeListView/include/TreeListView.h>
#include <WinSys/ProcessNetwork.h>
#include "ColumnLayout.h"
#include "ProcessDetails.h"
#include "ProcessColors.h"
#include "FilterBox.h"
#include <CustomSplitterWindow.h>
#include <unordered_set>

class CProcessesView :
	public CViewBase<CProcessesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;
	void OnFontChanged(HFONT font) override;
	void OnProcessColorsChanged() override;
	void ShowProcess(uint32_t pid) override;
	void OnFrameIdle() override;
	std::wstring GetCommandTip(UINT id) const override;

	BEGIN_MSG_MAP(CProcessesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelectionChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		CHAIN_MSG_MAP(CViewBase<CProcessesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_PROCESS_KILL, OnKill)
		COMMAND_ID_HANDLER(ID_KILL, OnKillFocused)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_PROCESS_KILLALL, OnKillAll)
		COMMAND_ID_HANDLER(ID_PROCESS_SUSPEND, OnSuspend)
		COMMAND_ID_HANDLER(ID_PROCESS_RESUME, OnResume)
		COMMAND_RANGE_HANDLER(ID_PRIORITY_REALTIME, ID_PRIORITY_IDLE, OnSetPriority)
		COMMAND_ID_HANDLER(ID_PROCESS_GOTOFILELOCATION, OnGoToFileLocation)
		COMMAND_ID_HANDLER(ID_VIEW_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSTREE, OnToggleTree)
		COMMAND_ID_HANDLER(ID_VIEW_LOWERPANE, OnToggleLowerPane)
		COMMAND_ID_HANDLER(ID_VIEW_KEEPSELECTIONVISIBLE, OnToggleKeepSelectionVisible)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		CHAIN_MSG_MAP_ALT(CViewBase<CProcessesView>, 1)
	END_MSG_MAP()

private:
	enum class Column {
		Name, Id, CPU, CpuTime, KernelTime, UserTime, CpuCycles, ContextSwitches, ContextSwitchDelta,
		PrivateBytes, PrivateBytesDelta, PeakPrivateBytes, WorkingSet, PeakWorkingSet, VirtualSize, PageFaults,
		Threads, PeakThreads, Handles, GdiObjects, UserObjects, Session, User, Priority, MemoryPriority, IoPriority, StartTime,
		Architecture, IntegrityLevel, Protection, Virtualization, JobId, IoRead, IoWrite, IoOther, NetworkSend, NetworkReceive,
		Description, ImagePath, CommandLine,
		_Count
	};

	struct ProcessItem {
		std::shared_ptr<ProcessInfoEx> Process;
		HTLITEM Item{ nullptr };
		int Image{ -1 };
		int NewTicks{ 0 };			// > 0: still highlighted as new
		bool Terminated{ false };	// highlighted for one update, then removed
		ProcessKind Kinds{ ProcessKind::None };	// for the row color (see UpdateKinds)
		bool Match{ true };	// matches the filter (always, without one); in the tree only if it does, or a descendant does
		// changes since the previous update (0 on the first)
		int64_t ContextSwitchDelta{ 0 }, PrivateBytesDelta{ 0 };
		uint64_t PrevContextSwitches{ 0 };
		int64_t PrevPrivateBytes{ -1 };	// -1: no previous update yet
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSort(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelectionChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKillFocused(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKillAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSuspend(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSetPriority(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleTree(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleLowerPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleKeepSelectionVisible(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	struct ShownColumn {
		Column Id;
		int Width;
	};
	// the shown columns in display order (the header's order, which the user can change by dragging)
	std::vector<ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ShownColumn> const& columns);
	static std::vector<ShownColumn> LoadColumns();
	static std::vector<ColumnLayout::ShownColumn> ToLayout(std::vector<ShownColumn> const& columns);
	void SaveColumns() const;
	bool ShowsNetwork() const;
	// zero without the tracker
	WinSys::ProcessNetworkTracker::Counters NetworkCounters(uint32_t pid) const;
	// the job's ID when the snapshot has it (elevated), else 1 for a process in a job and 0 for one not in a job
	static uint32_t GetJobId(ProcessInfoEx& process);
	// the process total when elevated; otherwise the sum over its current threads (exited threads' are lost)
	static uint64_t GetContextSwitches(ProcessInfoEx const& process);
	void UpdateDeltas(ProcessItem& item);
	// the kinds that don't change (secure, protected, immersive, job); Service and DotNet are set separately
	void SetFixedKinds(ProcessItem& item) const;
	// the PIDs hosting running services; false if they're unchanged
	bool UpdateServicePids();
	// sets DotNet on the processes the CLR has started in (see UpdateDotNet in the .cpp)
	void UpdateDotNet(bool all);

	void BuildItems();
	// the filter: a process matches if its image name, PID, user, description or command line contains the text
	bool Matches(ProcessItem& item) const;
	// the matches' count in the status line (the filter's text, from m_Filter)
	void UpdateFilterStatus();
	HTLITEM EnsureInserted(ProcessItem& item);
	void RemoveItem(ProcessItem& item);
	void CollectDescendants(HTLITEM hItem, std::vector<ProcessItem*>& items) const;
	ProcessItem* FindParent(ProcessItem const& item);
	ProcessItem* GetSelectedProcess() const;
	// ensureVisible: scroll to it; otherwise the scroll position stays
	void RestoreSelection(WinSys::ProcessOrThreadKey const* key, bool ensureVisible);
	void Sort();
	static int CALLBACK CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam);
	int CompareProcesses(ProcessItem& item1, ProcessItem& item2) const;
	std::wstring GetColumnText(ProcessItem& item, Column column) const;
	void UpdateUI();
	// what the Kill command (the toolbar, Del) acts on: the list with the focus
	enum class KillTarget { Process, Thread, Handle, None };
	KillTarget GetKillTarget() const;
	bool CanKill() const;
	// the lower pane shows the selected process (none for one that exited)
	void UpdateDetails();
	static bool CanControl(ProcessItem const* item);
	void ShowError(PCWSTR operation) const;

	// the process list on top; the lower pane (handles, modules, threads of the selected process) can be hidden
	CCustomHorSplitterWindow m_Splitter;
	CTreeListViewCtrl m_Tree;
	CProcessDetails m_Details;
	bool m_LowerPane{ false };
	// the lower pane follows a selection change after a pause, so moving through the list with the keyboard
	// (arrows held down, type-ahead) doesn't enumerate the handles or modules of every process on the way
	enum { DetailsTimerId = 1, DetailsDelay = 150 };
	CFilterBox m_Filter;	// in the view's rebar
	// runs while a network column is shown
	std::shared_ptr<WinSys::ProcessNetworkTracker> m_Network;	// while a network column is shown (NetworkTracking)
	DWORD m_NetworkError{ 0 };	// why it didn't start
	std::vector<ProcessColors::Entry> m_Colors;	// the ProcessColors setting
	std::unordered_set<uint32_t> m_ServicePids;
	// refreshes left to enumerate the services again; set when processes start, as a service host is a new process
	int m_ServiceRechecks{ 0 };
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::unordered_map<WinSys::ProcessOrThreadKey, ProcessItem> m_Items;	// node-based: ProcessItem* stays valid
	std::vector<Column> m_Columns;	// m_Columns[i] is shown in list column i; Name is always column 0 (the tree column)
	int m_SortColumn{ -1 };	// a Column, or -1
	bool m_SortAscending{ true };
	bool m_TreeMode{ true };
	// scroll to the selected process on every update; otherwise an update keeps the scroll position
	bool m_KeepSelectionVisible{ false };
	bool m_Active{ false };
};
