#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/PoolTags.h>
#include <VirtualListView.h>

//
// The Pool Tags tab, as PoolMonX: the kernel pools' usage by tag (WinSys SystemInfo::GetPoolTagInformation, one query)
// with paged and non-paged allocations, frees, their difference and the bytes in use, and each tag's binary and
// description from pooltag.txt (WinSys PoolTagDescriptions: the Debugging Tools' copy if installed, else the one built
// into the app, IDR_POOLTAGS). Read on every tick while the tab is active (and when it's activated, without change marks);
// a count that changed since the previous refresh has its cell colored for that refresh, green up and red down. The
// filter box matches the tag, the binary and the description; Copy copies the selected row's shown columns.
//
class CPoolTagsView :
	public CViewBase<CPoolTagsView>,
	public CVirtualListView<CPoolTagsView> {
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

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CPoolTagsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CPoolTagsView>)
		CHAIN_MSG_MAP(CViewBase<CPoolTagsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		CHAIN_MSG_MAP_ALT(CViewBase<CPoolTagsView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Tag, Value, PagedAllocs, PagedFrees, PagedDiff, PagedUsage, NonPagedAllocs, NonPagedFrees, NonPagedDiff,
		NonPagedUsage, TotalUsage, Binary, Description, _Count };
	// the columns with a change mark, PagedAllocs to TotalUsage
	static constexpr int FirstCounter = (int)Column::PagedAllocs, CounterCount = (int)Column::TotalUsage - FirstCounter + 1;

	struct Row {
		SYSTEM_POOLTAG Tag;
		WinSys::PoolTagDescription const* Description{ nullptr };
		// since the previous refresh, per counter column: -1, 0 or 1
		int8_t Change[CounterCount]{};
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

	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the PoolTagColumns setting
	void SaveColumns() const;

	// pooltag.txt: the Debugging Tools' or the built-in one
	void LoadDescriptions();
	// the tags read again, each compared with its previous counts (marks: whether the changes are shown)
	void Update(bool marks);
	static int64_t GetCounter(SYSTEM_POOLTAG const& tag, Column column);
	std::wstring GetText(Row const& row, Column column) const;
	bool Matches(Row const& row) const;
	// the list's rows: the tags that match the filter, sorted as the list is; selected: the tag to select
	void ShowRows(std::optional<ULONG> selected);
	std::optional<ULONG> GetSelectedTag() const;
	void SelectTag(std::optional<ULONG> tag);
	void UpdateStatus();

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };
	WinSys::PoolTagDescriptions m_Descriptions;
	std::wstring m_DescriptionsSource;	// the file's path, or "built-in"
	std::vector<Row> m_All;
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	COLORREF m_RowText{ CLR_DEFAULT }, m_RowTextBk{ CLR_DEFAULT };	// the row being drawn, as the list chose them
	bool m_Active{ false };
	bool m_Withheld{ false };	// the tags weren't given (not elevated)
};
