#include "pch.h"
#include "ListExport.h"

namespace {
	// the shown columns in display order (a hidden one may be kept at zero width), and their titles
	std::vector<int> GetShownColumns(CListViewCtrl list, std::vector<std::wstring>* titles = nullptr) {
		CHeaderCtrl header(list.GetHeader());
		int count = header.GetItemCount();
		std::vector<int> order(count);
		if (count)
			list.GetColumnOrderArray(count, order.data());
		std::vector<int> columns;
		for (auto col : order) {
			WCHAR title[128]{};
			HDITEM hdi{ HDI_WIDTH | HDI_TEXT };
			hdi.pszText = title;
			hdi.cchTextMax = _countof(title);
			header.GetItem(col, &hdi);
			if (hdi.cxy == 0)
				continue;
			columns.push_back(col);
			if (titles)
				titles->push_back(title);
		}
		return columns;
	}
}

namespace ListExport {
	std::wstring RowText(HWND hList, int row) {
		CListViewCtrl list(hList);
		std::wstring text, buffer(4096, L'\0');
		bool first = true;
		for (auto col : GetShownColumns(list)) {
			LVITEM item{};
			item.iSubItem = col;
			item.pszText = buffer.data();
			item.cchTextMax = (int)buffer.size();
			auto len = (int)list.SendMessage(LVM_GETITEMTEXT, row, (LPARAM)&item);
			if (!first)
				text += L'\t';
			first = false;
			text.append(buffer.data(), len);
		}
		return text;
	}

	std::vector<std::vector<std::wstring>> GetRows(HWND hList, std::function<std::wstring(int row)> const& treeColumn) {
		CListViewCtrl list(hList);
		std::vector<std::wstring> titles;
		auto columns = GetShownColumns(list, &titles);

		std::vector<std::vector<std::wstring>> rows;
		rows.reserve(list.GetItemCount() + 1);
		rows.push_back(std::move(titles));
		// a buffer as long as any text: LVM_GETITEMTEXT truncates
		std::wstring buffer(4096, L'\0');
		for (int i = 0; i < list.GetItemCount(); i++) {
			std::vector<std::wstring> row;
			row.reserve(columns.size());
			for (auto col : columns) {
				if (col == 0 && treeColumn) {
					row.push_back(treeColumn(i));
					continue;
				}
				LVITEM item{};
				item.iSubItem = col;
				item.pszText = buffer.data();
				item.cchTextMax = (int)buffer.size();
				auto len = (int)list.SendMessage(LVM_GETITEMTEXT, i, (LPARAM)&item);
				row.emplace_back(buffer.data(), len);
			}
			rows.push_back(std::move(row));
		}
		return rows;
	}

	std::wstring ToText(std::vector<std::vector<std::wstring>> const& rows) {
		std::wstring text;
		for (auto& row : rows) {
			for (size_t i = 0; i < row.size(); i++)
				text += (i ? L"\t" : L"") + row[i];
			text += L"\r\n";
		}
		return text;
	}

	std::wstring ToCsv(std::vector<std::vector<std::wstring>> const& rows) {
		std::wstring text;
		for (auto& row : rows) {
			for (size_t i = 0; i < row.size(); i++) {
				if (i)
					text += L',';
				auto& field = row[i];
				if (field.find_first_of(L",\"\r\n") == std::wstring::npos) {
					text += field;
					continue;
				}
				text += L'"';
				for (auto ch : field) {
					if (ch == L'"')
						text += L'"';
					text += ch;
				}
				text += L'"';
			}
			text += L"\r\n";
		}
		return text;
	}

	bool Save(std::wstring const& path, std::wstring const& text) {
		auto size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		std::string utf8("\xEF\xBB\xBF");
		auto bom = utf8.size();
		utf8.resize(bom + size);
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), utf8.data() + bom, size, nullptr, nullptr);
		wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!file)
			return false;
		DWORD written;
		return ::WriteFile(file.get(), utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size();
	}
}
