#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/RegistryIo.h>
#include <VirtualListView.h>
#include <deque>

//
// The Registry tab: registry operations per process and key, from WinSys RegistryIoTracker (an ETW session on
// Microsoft-Windows-Kernel-Registry, elevated only), the Disk tab's twin: started when the tab is first shown and kept
// running (shared through Recorders), read again on every refresh tick (also while hidden, for the rates) and merged by
// process and key: new rows green for a while (not on the first read), rows the tracker forgot (no operation for a
// minute, or the process exited) red. The rate is the operations a second over the last RateUpdates refreshes. The
// filter box matches the process, PID, key and last value. Double-click goes to the process; the row menu also opens
// the key in the Registry Editor, and copies.
//
class CRegistryView :
	public CViewBase<CRegistryView>,
	public CVirtualListView<CRegistryView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	bool CanCopy() const override {
		return m_List.GetSelectedCount() > 0;
	}
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override {
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CRegistryView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CRegistryView>)
		CHAIN_MSG_MAP(CViewBase<CRegistryView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_COLUMN_RESET, OnResetColumns)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_REGISTRY_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_REGISTRY_OPENKEY, OnOpenKey)
		COMMAND_ID_HANDLER(ID_REGISTRY_COPYKEY, OnCopyKey)
		CHAIN_MSG_MAP_ALT(CViewBase<CRegistryView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Process, Pid, Key, Rate, Opens, Reads, Writes, Failures, LastOperation, LastValue, Result,
		FirstSeen, LastSeen, _Count };

	// the rate is over this many refreshes
	static constexpr size_t RateUpdates = 3;

	struct Key {
		uint32_t Pid;
		std::wstring Name;	// the tracker's (as first seen); empty for an unnamed key object
		uint64_t Object;
		auto operator<=>(Key const&) const = default;
	};
	struct Sample {
		int64_t Time;	// QPC
		uint64_t Operations;
	};
	struct Row {
		WinSys::RegistryIoCounters Counters;
		std::deque<Sample> History;	// the last RateUpdates + 1 counts, oldest first
		double Rate{ 0 };	// operations a second
		int NewTicks{ 0 };	// > 0: still highlighted as new
		bool Gone{ false };	// the tracker forgot it: shown for GoneTicks more refreshes
		int GoneTicks{ 0 };

		Key GetKey() const {
			return { Counters.Pid, Counters.Key, Counters.Object };
		}
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the RegistryColumns setting
	void SaveColumns() const;

	// the tracker, the first time the tab is shown
	void StartTracker();
	// the tracker's keys into m_All, by process and key
	void Merge(std::vector<WinSys::RegistryIoCounters> keys, int64_t now, int64_t frequency);
	std::wstring GetText(Row const& row, Column column) const;
	std::wstring GetProcessName(uint32_t pid) const;
	// the key's name, or what's known of an unnamed key object
	static std::wstring GetKeyText(WinSys::RegistryIoCounters const& counters);
	// "Computer\HKEY_LOCAL_MACHINE\..." for the Registry Editor; empty for a key it can't show (unnamed, or under a
	// root it doesn't have: an app hive's \REGISTRY\A, a silo's \REGISTRY\WC)
	static std::wstring GetEditorPath(std::wstring const& key);
	bool Matches(Row const& row) const;
	// the list's rows: the ones that match the filter, sorted as the list is; selected: the row to select
	void ShowRows(std::optional<Key> selected);
	std::optional<Key> GetSelectedKey() const;
	Row const* GetSelected() const;
	void SelectRow(std::optional<Key> const& key);
	void UpdateStatus();

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	// shared (Recorders): from the first time the tab is shown, or the frame's since the app started (Record from
	// Startup); the frame prunes the exited processes (Recorders::OnRefresh)
	std::shared_ptr<WinSys::RegistryIoTracker> m_Tracker;
	DWORD m_TrackerError{ 0 };	// why it didn't start
	std::vector<Row> m_All;	// the last merge
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	bool m_Loaded{ false };	// merged at least once: later rows are new
	bool m_Active{ false };
	// merged while hidden: the rows (filtered and sorted) are rebuilt when shown, with the selection from before
	bool m_RowsPending{ false };
	std::optional<Key> m_PendingSelection;
};
