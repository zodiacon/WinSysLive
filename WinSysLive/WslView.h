#pragma once

#include "resource.h"
#include "ViewBase.h"
#include <TreeListView/include/TreeListView.h>
#include <WinSys/Wsl.h>
#include "ColumnLayout.h"
#include "FilterBox.h"
#include <thread>
#include <mutex>
#include <condition_variable>

//
// The WSL tab: the installed WSL distributions (WinSys WslDistros: docker-desktop's too) and, under each running WSL 2
// one, its Linux processes as a tree by parent, read by a helper the tab runs in the distribution (WinSys
// WslProcessMonitor: one wsl.exe each, asked for a frame on every refresh, the previous request's frame taken). Which
// distributions run is asked on a thread of the view's own every few seconds (wsl.exe --list --running, which never
// starts one); a helper starts for each running one and stops when the distribution does or the tab closes (it keeps
// the distribution running meanwhile, as any process in it does). WSL 1 distributions' processes are Windows pico
// processes, in the Processes tab. CPU is a share of all of the VM's processors; memory is the resident set. New
// processes are green and exited ones red for a while. The context menu: Terminate (SIGTERM) and Kill (SIGKILL), as
// root in the distribution, after a confirmation; Copy, Copy Command Line.
//
class CWslView :
	public CViewBase<CWslView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	bool CanCopy() const override {
		return GetSelectedRow() != nullptr;
	}
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
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	void OnDpiChanged(UINT /*dpi*/) override {
		m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}

	static constexpr UINT WM_WSL_RUNNING = WM_APP + 0x1E3;	// lParam: a new RunningState, the receiver's to delete

	BEGIN_MSG_MAP(CWslView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_WSL_RUNNING, OnRunning)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CViewBase<CWslView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_COLUMN_RESET, OnResetColumns)
		CHAIN_MSG_MAP_ALT(CViewBase<CWslView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	struct RunningState {
		std::vector<WinSys::WslDistro> Distros;
		std::optional<std::vector<std::wstring>> Running;	// nothing: wsl.exe didn't run
	};

private:
	enum class Column {
		Name, Pid, ParentPid, User, State, Cpu, CpuTime, Threads, Resident, Virtual, StartTime, CommandLine, Processes,
		_Count
	};

	// a process: its distribution, PID and start (an ID is reused)
	struct Key {
		std::wstring Distro;
		uint32_t Pid;
		uint64_t StartTicks;
		bool operator==(Key const&) const = default;
	};
	struct KeyHash {
		size_t operator()(Key const& k) const {
			return std::hash<std::wstring>()(k.Distro) ^ (std::hash<uint64_t>()(((uint64_t)k.Pid << 40) ^ k.StartTicks) << 1);
		}
	};

	// a distribution, or a process under it
	struct Row {
		bool IsDistro{ false };
		HTLITEM Item{ nullptr };
		HTLITEM ParentItem{ nullptr };	// the item it's under now (TLV_ROOT for a distribution)
		std::wstring Distro;
		// distribution rows
		WinSys::WslDistro Info;
		bool Running{ false };
		int Processes{ 0 };
		float Cpu{ 0 };
		uint64_t Resident{ 0 };
		// process rows
		WinSys::WslProcess Process;
		std::optional<Key> Parent;	// the row it's under; nothing: the distribution's
		int64_t StartTime{ 0 };		// FILETIME
		uint64_t PrevTicks{ 0 };	// user and system ticks at the previous frame, for the CPU rate
		bool HasPrev{ false };
		int NewTicks{ 0 }, ExitTicks{ 0 };	// highlighted while above 0; exited while ExitTicks counts down
		bool Exited{ false };
	};

	// a running distribution's helper and what the CPU rates need
	struct Monitor {
		std::unique_ptr<WinSys::WslProcessMonitor> Helper;
		uint64_t PrevTotalTicks{ 0 };
		bool Loaded{ false };	// a frame came: later processes are new
		WinSys::WslSystem System;
		WinSys::WslFrame Last;
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRunning(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSort(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the distribution rows from the installed ones (a stopped one stays, with no processes)
	void SyncDistros(RunningState const& state);
	// a running distribution's frame into its rows; its helper asked for the next
	void SyncProcesses(std::wstring const& distro, Monitor& monitor);
	// the rows to show and where, then the tree to match: rows whose place changed go (with their subtrees) and come
	// back under the right parent, parents first
	void ArrangeTree();
	void DeleteItem(Row& row);
	bool Matches(Row const& row) const;
	void UpdateStatus();

	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;
	void Sort();
	static int CALLBACK CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam);
	int CompareRows(Row& r1, Row& r2) const;
	std::wstring GetColumnText(Row const& row, Column column) const;
	Row* GetSelectedRow() const;
	void ShowContextMenu(Row const& row, POINT pt);
	void Signal(Row const& row, PCWSTR signal, PCWSTR verb);

	CTreeListViewCtrl m_Tree;
	CFilterBox m_Filter;
	// node-based maps: Row* (the items' data) stays valid
	std::unordered_map<std::wstring, Row> m_Distros;
	std::unordered_map<Key, Row, KeyHash> m_Processes;
	std::unordered_map<std::wstring, Monitor> m_Monitors;	// by distribution
	std::optional<std::vector<std::wstring>> m_Running;	// the last answer; nothing: wsl.exe didn't run
	std::vector<Column> m_Columns;
	int m_MenuColumn{ -1 };
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
	bool m_Active{ false };
	// asks which distributions run, every few seconds, until the view goes
	std::jthread m_Poller;
	std::mutex m_PollLock;
	std::condition_variable_any m_PollWake;
};
