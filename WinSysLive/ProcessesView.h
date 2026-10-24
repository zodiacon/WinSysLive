#pragma once

#include <WinSys/SystemInfo.h>

#include "ViewBase.h"
#include <TreeListView/include/TreeListView.h>
#include <WinSys/ProcessNetwork.h>
#include "ColumnLayout.h"
#include "ProcessDetails.h"
#include "ProcessColors.h"
#include "FilterBox.h"
#include "Symbols.h"
#include "SignatureVerifier.h"
#include "VirusTotalChecker.h"
#include <WinSys/Gpu.h>
#include <CustomSplitterWindow.h>
#include <unordered_set>

class CProcessesView :
	public CViewBase<CProcessesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	// the process, or the row of the lower pane's page with the focus
	bool CanCopy() const override;
	HWND GetExportList() override {
		return m_Tree.GetListCtrl();
	}
	std::optional<std::wstring> GetTreeColumnText(HWND hList, int row) override {
		if (hList != m_Tree.GetListCtrl())
			return std::nullopt;
		auto item = m_Tree.GetVisibleItem(row);
		return item ? m_Tree.GetCellText(item, 0) : std::wstring();
	}
	void OnActivate(bool active) override;
	void SaveSettings() override;
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;
	void OnFontChanged(HFONT font) override;
	void OnDpiChanged(UINT dpi) override;
	void OnProcessColorsChanged() override;
	void ShowProcess(uint32_t pid) override;
	void OnFrameIdle() override;
	std::wstring GetCommandTip(UINT id) const override;
	// CFrameWindowImpl's, which it calls on WM_SIZE; then the rebar's bands
	void UpdateLayout(BOOL resizeBars = TRUE);

	// posted by the signature worker (View > Hide Processes > Signed Processes) when a file's signature is verified
	static constexpr UINT WM_PROCESS_SIGNED = WM_APP + 0x160;

	BEGIN_MSG_MAP(CProcessesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_DUMP_WRITTEN, OnDumpWritten)
		MESSAGE_HANDLER(WM_PROCESS_SIGNED, OnSigned)
		MESSAGE_HANDLER(VirusTotalChecker::WM_VIRUSTOTAL, OnVirusTotal)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelectionChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnHideDropDown)
		CHAIN_MSG_MAP(CViewBase<CProcessesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_PROCESS_KILL, OnKill)
		COMMAND_ID_HANDLER(ID_KILL, OnKillFocused)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_PROCESS_KILLALL, OnKillAll)
		COMMAND_ID_HANDLER(ID_PROCESS_KILLTREE, OnKillTree)
		COMMAND_ID_HANDLER(ID_PROCESS_RESTART, OnRestart)
		COMMAND_ID_HANDLER(ID_PROCESS_EMPTYWORKINGSET, OnEmptyWorkingSet)
		COMMAND_ID_HANDLER(ID_PROCESS_DEBUG, OnDebug)
		COMMAND_ID_HANDLER(ID_PROCESS_CRITICAL, OnCritical)
		COMMAND_ID_HANDLER(ID_PROCESS_SUSPEND, OnSuspend)
		COMMAND_ID_HANDLER(ID_PROCESS_RESUME, OnResume)
		COMMAND_RANGE_HANDLER(ID_PRIORITY_REALTIME, ID_PRIORITY_IDLE, OnSetPriority)
		COMMAND_RANGE_HANDLER(ID_IOPRIORITY_VERYLOW, ID_IOPRIORITY_HIGH, OnSetIoPriority)
		COMMAND_RANGE_HANDLER(ID_MEMPRIORITY_VERYLOW, ID_MEMPRIORITY_NORMAL, OnSetMemoryPriority)
		COMMAND_ID_HANDLER(ID_PROCESS_AFFINITY, OnSetAffinity)
		COMMAND_ID_HANDLER(ID_PROCESS_EFFICIENCY, OnEfficiencyMode)
		COMMAND_ID_HANDLER(ID_PROCESS_GOTOFILELOCATION, OnGoToFileLocation)
		COMMAND_ID_HANDLER(ID_VT_REPORT, OnVirusTotalCommand)
		COMMAND_ID_HANDLER(ID_VT_CHECK, OnVirusTotalCommand)
		COMMAND_ID_HANDLER(ID_VT_SUBMIT, OnVirusTotalCommand)
		COMMAND_ID_HANDLER(ID_PROCESS_MINIDUMP, OnCreateDump)
		COMMAND_ID_HANDLER(ID_PROCESS_WAITCHAIN, OnWaitChain)
		COMMAND_ID_HANDLER(ID_PROCESS_FULLDUMP, OnCreateDump)
		COMMAND_ID_HANDLER(ID_VIEW_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSTREE, OnToggleTree)
		COMMAND_ID_HANDLER(ID_VIEW_LOWERPANE, OnToggleLowerPane)
		COMMAND_ID_HANDLER(ID_VIEW_KEEPSELECTIONVISIBLE, OnToggleKeepSelectionVisible)
		COMMAND_RANGE_HANDLER(ID_VIEW_HIDEOTHERUSERS, ID_VIEW_HIDESIGNED, OnToggleHide)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		CHAIN_MSG_MAP_ALT(CViewBase<CProcessesView>, 1)
	END_MSG_MAP()

private:
	enum class Column {
		Name, Id, ParentId, ParentName, Status, CPU, CpuTime, KernelTime, UserTime, CpuCycles, ContextSwitches, ContextSwitchDelta,
		PrivateBytes, PrivateBytesDelta, PeakPrivateBytes, WorkingSet, PeakWorkingSet, VirtualSize, PageFaults, PagedPool, NonPagedPool,
		Threads, PeakThreads, Handles, GdiObjects, PeakGdiObjects, UserObjects, PeakUserObjects, Session, User, Priority, MemoryPriority, IoPriority, StartTime,
		Architecture, IntegrityLevel, Elevation, Protection, Critical, Cfg, Mitigations, DpiAwareness, Virtualization, JobId,
		IoRead, IoWrite, IoOther, NetworkSend, NetworkReceive,
		GpuUsage, GpuDedicated, GpuShared, NpuUsage, NpuMemory,
		PackageName, WindowTitle, VirusTotal, Description, ImagePath, CommandLine,
		_Count
	};

	struct ProcessItem {
		std::shared_ptr<ProcessInfoEx> Process;
		HTLITEM Item{ nullptr };
		int Image{ -1 };
		int NewTicks{ 0 };			// > 0: still highlighted as new
		bool Terminated{ false };	// exited: highlighted for a while (ExitTicks), then removed
		int ExitTicks{ 0 };	// refreshes left to show it, once it exited
		ProcessKind Kinds{ ProcessKind::None };	// for the row color (see UpdateKinds)
		bool Match{ true };	// matches the filter (always, without one); in the tree only if it does, or a descendant does
		bool Hidden{ false };	// by View > Hide Processes (then it doesn't match)
		// queried on every refresh while a GPU column is shown
		WinSys::GpuProcessState GpuState;
		WinSys::GpuProcessUsage Gpu;
		// and while an NPU column is
		WinSys::GpuProcessState NpuState;
		WinSys::GpuProcessUsage Npu;
		// changes since the previous update (0 on the first)
		int64_t ContextSwitchDelta{ 0 }, PrivateBytesDelta{ 0 };
		uint64_t PrevContextSwitches{ 0 };
		int64_t PrevPrivateBytes{ -1 };	// -1: no previous update yet
		// a value that's queried (or summed) rather than read from the snapshot, taken once before a sort by its column
		int64_t SortValue{ 0 };
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
	// the selected process's menu, at pt (screen)
	void ShowRowMenu(CPoint pt);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKillFocused(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKillAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKillTree(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRestart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEmptyWorkingSet(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDebug(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCritical(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSuspend(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSetPriority(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSetIoPriority(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSetMemoryPriority(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSetAffinity(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// Set Affinity with more than one processor group (over 64 processors)
	void SetGroupsAffinity(WinSys::Process& process, std::vector<WinSys::SystemInfo::ProcessorGroup> const& groups);
	LRESULT OnEfficiencyMode(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnVirusTotalCommand(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCreateDump(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnWaitChain(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDumpWritten(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleTree(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleLowerPane(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleKeepSelectionVisible(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleHide(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVirusTotal(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	// the Hide button's menu: View > Hide Processes
	LRESULT OnHideDropDown(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	// the Hide button's font and size for its DPI, and its band
	void LayoutHideBar();
	// twice the width it would have, for its DPI
	void SetHideButtonWidth();
	// checked (pressed) while an option is on
	void UpdateHideButton();
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
	// the columns and the splitter
	void SaveLayout() const;
	bool ShowsNetwork() const;
	// Task Manager's Status: Running, Suspended (every thread, the Suspended kind) or Not responding (a hung window)
	enum class ProcessStatus { Running, NotResponding, Suspended };
	ProcessStatus GetStatus(ProcessItem const& item) const;
	// m_HungPids again, while the Status column is shown (a pass over the top-level windows)
	void UpdateHungProcesses();
	// m_WindowTitles again, while the Window Title column is shown (another pass)
	void UpdateWindowTitles();
	// the process that created this one, if it's still running (its ID may belong to a later process)
	ProcessInfoEx* GetParent(ProcessInfoEx const& process) const;
	static std::wstring FormatMitigations(WinSys::ProcessMitigations const& m);
	// a GPU column is shown: the processes' GPU usage is queried (a call per engine of each process using the GPU)
	bool ShowsGpu() const;
	// an NPU column is shown, and there are NPUs
	bool ShowsNpu() const;
	// zero without the tracker
	WinSys::ProcessNetworkTracker::Counters NetworkCounters(uint32_t pid) const;
	// the job's ID when the snapshot has it (elevated), else 1 for a process in a job and 0 for one not in a job
	static uint32_t GetJobId(ProcessInfoEx& process);
	// the process total when elevated; otherwise the sum over its current threads (exited threads' are lost)
	static uint64_t GetContextSwitches(ProcessInfoEx const& process);
	void UpdateDeltas(ProcessItem& item);
	// the kinds that don't change (secure, protected, immersive, job); Service and DotNet are set separately
	void SetFixedKinds(ProcessItem& item) const;
	static bool IsSuspended(ProcessInfoEx const& process);
	void UpdateSuspended(ProcessItem& item);
	// the PIDs hosting running services; false if they're unchanged
	bool UpdateServicePids();
	// sets DotNet on the processes the CLR has started in (see UpdateDotNet in the .cpp)
	void UpdateDotNet(bool all);

	void BuildItems();
	// the tree again from the items, which keep their state (deltas, highlights, kinds): after a filter change or
	// switching between tree and flat
	void RebuildTree();
	// the matching items into an empty tree, in snapshot order; the exited ones last
	void InsertItems();
	// the filter: a process matches if it isn't hidden (sets Hidden) and its image name, PID, user, description or
	// command line contains the text
	bool Matches(ProcessItem& item);
	// by View > Hide Processes; queues the image for verification while signed processes are hidden
	bool IsHidden(ProcessItem const& item);
	bool IsHiding() const;
	// the filter's text or a Hide Processes option
	bool IsFiltering() const;
	// in the Windows directory, or without an image (the system's own: Idle, System, Registry, Memory Compression...)
	bool IsWindowsProcess(ProcessInfoEx& process) const;
	// the items whose Hidden changed (the kinds change, a signature is verified) go into the tree or out of it
	void UpdateHidden();
	// the matches' count in the status line (the filter's text, from m_Filter, or the hidden processes)
	void UpdateFilterStatus();
	HTLITEM EnsureInserted(ProcessItem& item);
	void RemoveItem(ProcessItem& item);
	// hItem and its ancestors, while they don't match the filter and have no children (see EnsureInserted)
	void RemoveUnneededAncestors(HTLITEM hItem);
	void CollectDescendants(HTLITEM hItem, std::vector<ProcessItem*>& items) const;
	ProcessItem* FindParent(ProcessItem const& item);
	ProcessItem* GetSelectedProcess() const;
	// ensureVisible: scroll to it; otherwise the scroll position stays
	void RestoreSelection(WinSys::ProcessOrThreadKey const* key, bool ensureVisible);
	void Sort();
	// the sort column and direction, in the ProcessSort setting
	void LoadSort();
	void SaveSort() const;
	// columns whose values are queried (or summed) rather than read from the snapshot (see ProcessItem::SortValue)
	static bool IsQueriedColumn(Column column);
	int64_t GetQueriedValue(ProcessItem& item, Column column) const;
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
	// started again by CreateProcess: not a service's process or a packaged app's
	static bool CanRestart(ProcessItem const* item);
	// changing it needs SeDebugPrivilege: only elevated
	static bool CanSetCritical(ProcessItem const* item);
	// the selected process opened with the access (and limited query access), if it can be controlled; null (after
	// saying why) otherwise
	std::unique_ptr<WinSys::Process> OpenSelected(WinSys::ProcessAccessMask access) const;
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
	CToolBarCtrl m_HideBar;	// in the rebar after the filter: the Hide drop-down button
	CFont m_HideBarFont;	// for its DPI
	int m_HideButtonWidth{ 0 };	// at 96 DPI
	// runs while a network column is shown
	std::shared_ptr<WinSys::ProcessNetworkTracker> m_Network;	// while a network column is shown (NetworkTracking)
	DWORD m_NetworkError{ 0 };	// why it didn't start
	std::vector<ProcessColors::Entry> m_Colors;	// the ProcessColors setting
	std::unordered_set<uint32_t> m_ServicePids;
	std::vector<uint32_t> m_HungPids;	// sorted; empty while the Status column is hidden
	std::unordered_map<uint32_t, std::wstring> m_WindowTitles;	// by PID; empty while the Window Title column is hidden
	std::wstring m_CurrentUser;	// DOMAIN\user, for the Own kind
	// refreshes left to enumerate the services again; set when processes start, as a service host is a new process
	int m_ServiceRechecks{ 0 };
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::unordered_map<WinSys::ProcessOrThreadKey, ProcessItem> m_Items;	// node-based: ProcessItem* stays valid
	std::vector<Column> m_Columns;	// m_Columns[i] is shown in list column i; Name is always column 0 (the tree column)
	int m_SortColumn{ -1 };	// a Column, or -1
	bool m_SortAscending{ true };
	bool m_SortPending{ false };	// refreshed while hidden: sorted when shown
	bool m_TreeMode{ true };
	// scroll to the selected process on every update; otherwise an update keeps the scroll position
	bool m_KeepSelectionVisible{ false };
	// View > Hide Processes (the ProcessHide* settings)
	bool m_HideOtherUsers{ false }, m_HideServices{ false }, m_HideWindows{ false }, m_HideSigned{ false };
	std::wstring m_WindowsDirectory;	// with a backslash at the end
	SignatureVerifier m_Signatures;	// the images', while signed processes are hidden
	bool m_Active{ false };
};
