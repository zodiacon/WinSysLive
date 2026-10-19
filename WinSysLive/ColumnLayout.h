#pragma once

#include <span>
#include <optional>
#include <ColumnManager.h>

//
// The columns a list shows, in display order with their widths, as the views keep them in a setting ("Key:width,...")
// and as the column selection dialog (CColumnsDlg) changes them. A column is identified by its index in the view's
// table of ColumnDef.
//
namespace ColumnLayout {
	struct ColumnDef {
		PCWSTR Name;
		PCWSTR Key;		// stored in the setting, so it must not change
		int Width;
		int Format;		// LVCFMT_*
		bool Default;	// shown until the user chooses otherwise
		bool Mandatory{ false };	// can't be hidden
	};

	struct ShownColumn {
		int Id;		// index in the ColumnDef table
		int Width;
	};

	// Unknown keys are skipped. Nothing usable gives the defaults; mandatory columns that are missing are added.
	std::vector<ShownColumn> Parse(std::wstring_view text, std::span<ColumnDef const> defs);
	std::wstring Format(std::vector<ShownColumn> const& columns, std::span<ColumnDef const> defs);

	// Runs the column selection dialog. Columns that stay keep their width; the result is empty if it was canceled.
	std::optional<std::vector<ShownColumn>> Select(HWND hParent, std::vector<ShownColumn> const& shown, std::span<ColumnDef const> defs);

	//
	// For a list view with a ColumnManager (CVirtualListView) that holds only the shown columns, added in display
	// order, with the IDs as the tags. Rebuilding the columns invalidates CVirtualListView's sort column (call ClearSort).
	//
	// in display order: the header's, which the user can change by dragging
	std::vector<ShownColumn> GetShownColumns(CListViewCtrl list, ColumnManager const* cm);
	void SetColumns(ColumnManager* cm, std::vector<ShownColumn> const& columns, std::span<ColumnDef const> defs);
	// The column (ID) at a point in screen coordinates: -1 on the header's empty part, nothing if the point isn't on
	// the header. A header's WM_CONTEXTMENU comes up through the list view with the list view (or a window above it)
	// as the window, so the position is what tells it from a row's.
	std::optional<int> HeaderColumnAt(CListViewCtrl list, ColumnManager const* cm, POINT pt);
	// The header's context menu (IDR_CONTEXT's Column popup) for a column: Hide (disabled for -1 or a mandatory
	// column) and Select Columns
	// Whether a column (an index into the column manager, as CVirtualListView passes it) is the leftmost one shown,
	// which the user can change by dragging: row icons go there (with LVS_EX_SUBITEMIMAGES)
	bool IsLeftmostColumn(CListViewCtrl list, int column);
	CMenuHandle PrepareHeaderMenu(CMenu& menu, int column, std::span<ColumnDef const> defs);
}
