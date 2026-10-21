#include "pch.h"
#include "ColumnLayout.h"
#include "ColumnsDlg.h"
#include "resource.h"
#include <ranges>

namespace ColumnLayout {
	namespace {
		bool Contains(std::vector<ShownColumn> const& columns, int id) {
			return std::ranges::contains(columns, id, &ShownColumn::Id);
		}
	}

	std::vector<ShownColumn> Parse(std::wstring_view text, std::span<ColumnDef const> defs) {
		std::vector<ShownColumn> columns;
		for (auto part : std::views::split(text, L',')) {
			std::wstring_view entry(part.begin(), part.end());
			auto colon = entry.find(L':');
			if (colon == entry.npos)
				continue;
			auto key = entry.substr(0, colon);
			auto def = std::ranges::find_if(defs, [&](auto& d) { return key == d.Key; });
			if (def == defs.end())
				continue;
			int id = (int)(def - defs.begin());
			int width = _wtoi(std::wstring(entry.substr(colon + 1)).c_str());
			if (width > 0 && !Contains(columns, id))
				columns.push_back({ id, width });
		}

		if (columns.empty()) {
			for (int i = 0; i < (int)defs.size(); i++)
				if (defs[i].Default)
					columns.push_back({ i, defs[i].Width });
		}
		// a missing mandatory column goes after the mandatory column before it in the table, or first
		int previous = -1;
		for (int i = 0; i < (int)defs.size(); i++) {
			if (!defs[i].Mandatory)
				continue;
			if (!Contains(columns, i)) {
				auto pos = previous < 0 ? columns.begin() : std::ranges::find(columns, previous, &ShownColumn::Id) + 1;
				columns.insert(pos, { i, defs[i].Width });
			}
			previous = i;
		}
		return columns;
	}

	std::wstring Format(std::vector<ShownColumn> const& columns, std::span<ColumnDef const> defs) {
		std::wstring text;
		for (auto& c : columns) {
			if (!text.empty())
				text += L',';
			text += std::format(L"{}:{}", defs[c.Id].Key, c.Width);
		}
		return text;
	}

	std::optional<std::vector<ShownColumn>> Select(HWND hParent, std::vector<ShownColumn> const& shown, std::span<ColumnDef const> defs) {
		// the shown columns first, in display order, then the others in table order
		std::vector<CColumnsDlg::Item> items, defaults;
		for (auto& c : shown)
			items.push_back({ c.Id, defs[c.Id].Name, true, defs[c.Id].Mandatory });
		for (int i = 0; i < (int)defs.size(); i++) {
			if (!Contains(shown, i))
				items.push_back({ i, defs[i].Name, false, defs[i].Mandatory });
			defaults.push_back({ i, defs[i].Name, defs[i].Default || defs[i].Mandatory, defs[i].Mandatory });
		}
		std::ranges::stable_partition(defaults, &CColumnsDlg::Item::Visible);

		CColumnsDlg dlg(std::move(items), std::move(defaults));
		if (dlg.DoModal(hParent) != IDOK)
			return std::nullopt;

		std::vector<ShownColumn> columns;
		for (auto& item : dlg.GetItems()) {
			if (!item.Visible)
				break;
			auto current = std::ranges::find(shown, item.Id, &ShownColumn::Id);
			columns.push_back({ item.Id, current != shown.end() ? current->Width : defs[item.Id].Width });
		}
		return columns;
	}

	std::vector<ShownColumn> GetShownColumns(CListViewCtrl list, ColumnManager const* cm) {
		auto header = list.GetHeader();
		int count = header.GetItemCount();
		std::vector<int> order(count);
		header.GetOrderArray(count, order.data());
		std::vector<ShownColumn> columns;
		// the widths at 96 DPI, as the setting keeps them and ColumnManager::AddColumn takes them
		auto dpi = DpiHelper::GetDpi(list);
		for (int index : order) {
			// the header item's lParam is the column manager's index
			HDITEM hdi{ HDI_LPARAM };
			header.GetItem(index, &hdi);
			columns.push_back({ cm->GetColumnTag<int>((int)hdi.lParam), DpiHelper::Scale(list.GetColumnWidth(index), USER_DEFAULT_SCREEN_DPI, dpi) });
		}
		return columns;
	}

	void SetColumns(ColumnManager* cm, std::vector<ShownColumn> const& columns, std::span<ColumnDef const> defs) {
		cm->Clear();
		for (auto& c : columns) {
			auto& def = defs[c.Id];
			cm->AddColumn(def.Name, def.Format, c.Width, c.Id);
		}
		CListViewCtrl lv(cm->GetListView());
		// the list view ignores the alignment of column 0 when it's inserted, but not when it's set
		if (!columns.empty() && defs[columns[0].Id].Format != LVCFMT_LEFT) {
			LVCOLUMN lvc{ LVCF_FMT };
			lvc.fmt = defs[columns[0].Id].Format;
			lv.SetColumn(0, &lvc);
		}
		lv.Invalidate();
	}

	std::optional<int> HeaderColumnAt(CListViewCtrl list, ColumnManager const* cm, POINT pt) {
		auto header = list.GetHeader();
		CRect rc;
		header.GetWindowRect(&rc);
		if (!rc.PtInRect(pt))
			return std::nullopt;
		HDHITTESTINFO hit{};
		hit.pt = pt;
		header.ScreenToClient(&hit.pt);
		if (header.HitTest(&hit) < 0)
			return -1;
		HDITEM hdi{ HDI_LPARAM };
		header.GetItem(hit.iItem, &hdi);
		return cm->GetColumnTag<int>((int)hdi.lParam);
	}

	bool IsLeftmostColumn(CListViewCtrl list, int column) {
		auto header = list.GetHeader();
		HDITEM hdi{ HDI_LPARAM };
		return header.GetItem(header.OrderToIndex(0), &hdi) && (int)hdi.lParam == column;
	}

	CMenuHandle PrepareHeaderMenu(CMenu& menu, int column, std::span<ColumnDef const> defs) {
		menu.LoadMenu(IDR_CONTEXT);
		CMenuHandle popup = menu.GetSubMenu(2);
		if (column >= 0)
			popup.ModifyMenu(ID_COLUMN_HIDE, MF_BYCOMMAND | MF_STRING, ID_COLUMN_HIDE, std::format(L"&Hide \"{}\"", defs[column].Name).c_str());
		popup.EnableMenuItem(ID_COLUMN_HIDE, MF_BYCOMMAND | (column >= 0 && !defs[column].Mandatory ? MF_ENABLED : MF_GRAYED));
		return popup;
	}
}
