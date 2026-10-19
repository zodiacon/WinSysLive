#pragma once

#include <span>
#include <optional>

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
}
