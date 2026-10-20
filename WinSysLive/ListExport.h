#pragma once

#include <functional>

//
// A list view's contents as text, for File > Export List and Copy All Rows: the shown columns (not zero-width), in
// display order (the header's order array), and every row's texts through LVM_GETITEMTEXT, so owner-data and callback
// lists (CVirtualListView, the TreeListView's inner list) ask their owners as they would to paint.
//
namespace ListExport {
	// the first row is the column headers. treeColumn: column 0's text of a row, for a TreeListView's inner list (the
	// TreeListView draws that column itself, so the list has no text there)
	std::vector<std::vector<std::wstring>> GetRows(HWND hList, std::function<std::wstring(int row)> const& treeColumn = nullptr);
	// tab-separated lines, as the views' Copy commands make them
	std::wstring ToText(std::vector<std::vector<std::wstring>> const& rows);
	// RFC 4180: fields with commas, quotes or line breaks quoted, quotes doubled
	std::wstring ToCsv(std::vector<std::vector<std::wstring>> const& rows);
	// UTF-8 with a BOM, so Excel reads it as Unicode; false (with the last error) on failure
	bool Save(std::wstring const& path, std::wstring const& text);
}
