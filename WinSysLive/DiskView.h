#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/FileIo.h>
#include <VirtualListView.h>
#include <deque>

//
// The Disk tab: file I/O per process and file, from WinSys FileIoTracker (an ETW session on Microsoft-Windows-Kernel-File,
// elevated only), started when the tab is first shown and kept running, as the RPC/ALPC tab's. Read again on every
// refresh tick (also while hidden, for the rates) and merged by process and file key: new rows green for two refreshes
// (not on the first read), rows the tracker forgot (no I/O for a minute, or the process exited) red once. The rates are
// over the last RateUpdates refreshes, as the Network tab's: the events come in batches about once a second. The filter
// box matches the process, PID and file. Double-click goes to the process; the row menu also has the file's
// properties and location, and Copy.
//
class CDiskView :
	public CViewBase<CDiskView>,
	public CVirtualListView<CDiskView> {
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

	BEGIN_MSG_MAP(CDiskView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CDiskView>)
		CHAIN_MSG_MAP(CViewBase<CDiskView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_DISK_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_DISK_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_DISK_OPENLOCATION, OnOpenLocation)
		CHAIN_MSG_MAP_ALT(CViewBase<CDiskView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Process, Pid, File, ReadRate, WriteRate, TotalRate, BytesRead, BytesWritten, Reads, Writes,
		FirstSeen, LastSeen, _Count };

	// the rates are over this many refreshes
	static constexpr size_t RateUpdates = 3;

	struct Key {
		uint32_t Pid;
		uint64_t FileKey;
		auto operator<=>(Key const&) const = default;
	};
	struct Sample {
		int64_t Time;	// QPC
		uint64_t Read, Written;
	};
	struct Row {
		WinSys::FileIoCounters File;
		std::deque<Sample> History;	// the last RateUpdates + 1 counts, oldest first
		double ReadRate{ 0 }, WriteRate{ 0 };	// bytes a second
		int NewTicks{ 0 };	// > 0: still highlighted as new
		bool Gone{ false };	// the tracker forgot it: shown for GoneTicks more refreshes
		int GoneTicks{ 0 };

		Key GetKey() const {
			return { File.Pid, File.FileKey };
		}
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the DiskColumns setting
	void SaveColumns() const;

	// the tracker, the first time the tab is shown
	void StartTracker();
	// the tracker's files into m_All, by process and file key
	void Merge(std::vector<WinSys::FileIoCounters> files, int64_t now, int64_t frequency);
	std::wstring GetText(Row const& row, Column column) const;
	std::wstring GetProcessName(uint32_t pid) const;
	bool Matches(Row const& row) const;
	// the list's rows: the ones that match the filter, sorted as the list is; selected: the row to select
	void ShowRows(std::optional<Key> selected);
	std::optional<Key> GetSelectedKey() const;
	Row const* GetSelected() const;
	void SelectRow(std::optional<Key> key);
	void UpdateStatus();

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::unique_ptr<WinSys::FileIoTracker> m_Tracker;	// from the first time the tab is shown
	DWORD m_TrackerError{ 0 };	// why it didn't start
	std::vector<Row> m_All;	// the last merge
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	bool m_Loaded{ false };	// merged at least once: later rows are new
	bool m_Active{ false };
	// merged while hidden: the rows (filtered and sorted) are rebuilt when shown, with the selection from before
	bool m_RowsPending{ false };
	std::optional<Key> m_PendingSelection;
};
