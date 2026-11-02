#include "pch.h"
#include "ImagePage.h"
#include "FormatHelper.h"
#include "TotalPE.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include <WinSys/Helpers.h>

using namespace WinSys;

namespace {
	std::wstring Hex(uint64_t value, int digits = 8) {
		return std::format(L"0x{:0{}X}", value, digits);
	}

	std::wstring Wide(std::string const& text) {
		return std::wstring(text.begin(), text.end());	// names in PE files are ASCII
	}

	// a time stamp as a date, unless it's a reproducible build's hash (not a plausible time)
	std::wstring TimeStampText(uint32_t stamp) {
		auto now = (uint64_t)::_time64(nullptr);
		if (stamp < 631152000 || stamp > now + 86400)	// before 1990, or in the future
			return std::format(L"{} (a hash: a reproducible build)", Hex(stamp));
		auto filetime = ((int64_t)stamp + 11644473600LL) * 10000000;
		return FormatHelper::FormatDateTime(filetime) + L" (UTC " + Hex(stamp) + L")";
	}
}

CProcImagePage::CProcImagePage(ProcessProperties const& props) : m_Props(props) {
}

LRESULT CProcImagePage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_PE_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	ListViewHelper::EnableColumnDpiScaling(m_List);
	CComboBox view(GetDlgItem(IDC_PE_VIEW));
	for (auto name : { L"Headers", L"Sections", L"Imports", L"Exports" })
		view.AddString(name);
	view.SetCurSel(0);

	if (m_Props.ImagePath.empty())
		m_Error = ERROR_FILE_NOT_FOUND;
	else {
		CWaitCursor wait;
		m_File = PEFile::Load(m_Props.ImagePath);
		if (!m_File)
			m_Error = ::GetLastError();
	}
	GetDlgItem(IDC_PE_TOTALPE).EnableWindow(!m_Props.ImagePath.empty());
	if (m_File) {
		BuildTables();
		auto& h = m_File->GetHeaders();
		auto imports = m_File->GetImports();
		size_t functions = 0;
		for (auto& l : imports)
			functions += l.Functions.size();
		auto exports = m_File->GetExports();
		SetDlgItemText(IDC_PE_SUMMARY, std::format(L"{} {}, {}; {} sections, {} imports from {} libraries, {} exports",
			PEFile::MachineName(h.Machine), h.Is64 ? L"PE32+" : L"PE32", PEFile::SubsystemName(h.Subsystem),
			m_File->GetSections().size(), functions, imports.size(), exports ? exports->Functions.size() : 0).c_str());
	}
	ShowView();
	InitDynamicLayout(false, false);
	return TRUE;
}

void CProcImagePage::BuildTables() {
	auto& h = m_File->GetHeaders();

	auto& headers = m_Tables[(int)View::Headers];
	headers.Columns = { { L"Field", 170 }, { L"Value", 470 } };
	auto field = [&](PCWSTR name, std::wstring value) { headers.Rows.push_back({ name, std::move(value) }); };
	field(L"File", m_Props.ImagePath);
	field(L"Machine", std::format(L"{} ({})", PEFile::MachineName(h.Machine), Hex(h.Machine, 4)));
	field(L"Format", h.Is64 ? L"PE32+ (64-bit)" : L"PE32 (32-bit)");
	field(L"Time stamp", TimeStampText(h.TimeDateStamp));
	field(L"Characteristics", std::format(L"{} ({})", PEFile::CharacteristicsText(h.Characteristics), Hex(h.Characteristics, 4)));
	field(L"Linker version", std::format(L"{}.{}", h.LinkerMajor, h.LinkerMinor));
	field(L"Entry point", h.EntryPoint ? L"RVA " + Hex(h.EntryPoint) : L"(none)");
	field(L"Image base", Hex(h.ImageBase, h.Is64 ? 16 : 8));
	field(L"Size of image", std::format(L"{} ({})", FormatHelper::FormatBytes(h.SizeOfImage), Hex(h.SizeOfImage)));
	field(L"Size of headers", Hex(h.SizeOfHeaders));
	field(L"Alignment", std::format(L"sections {}, file {}", Hex(h.SectionAlignment, 0), Hex(h.FileAlignment, 0)));
	field(L"Subsystem", std::format(L"{} {}.{}", PEFile::SubsystemName(h.Subsystem), h.SubsystemMajor, h.SubsystemMinor));
	field(L"OS version", std::format(L"{}.{}", h.OsMajor, h.OsMinor));
	field(L"DLL characteristics", std::format(L"{} ({})", PEFile::DllCharacteristicsText(h.DllCharacteristics), Hex(h.DllCharacteristics, 4)));
	field(L"Checksum", h.CheckSum ? Hex(h.CheckSum) : L"0 (none)");
	field(L"Stack", std::format(L"{} reserved, {} committed", FormatHelper::FormatBytes(h.StackReserve), FormatHelper::FormatBytes(h.StackCommit)));
	field(L"Heap", std::format(L"{} reserved, {} committed", FormatHelper::FormatBytes(h.HeapReserve), FormatHelper::FormatBytes(h.HeapCommit)));
	for (int i = 0; i < (int)h.Directories.size(); i++) {
		auto& d = h.Directories[i];
		if (d.Rva || d.Size)
			headers.Rows.push_back({ std::format(L"{} directory", PEFile::DirectoryName(i)),
				std::format(L"RVA {}, {} bytes", Hex(d.Rva), d.Size) });
	}

	auto& sections = m_Tables[(int)View::Sections];
	sections.Columns = { { L"Name", 80 }, { L"Virtual Address", 110 }, { L"Virtual Size", 100 }, { L"Raw Offset", 100 },
		{ L"Raw Size", 100 }, { L"Characteristics", 260 } };
	sections.Numeric = { false, true, true, true, true, false };
	for (auto& s : m_File->GetSections()) {
		sections.Rows.push_back({ Wide(s.Name), Hex(s.VirtualAddress), Hex(s.VirtualSize), Hex(s.RawOffset), Hex(s.RawSize),
			std::format(L"{} ({})", PEFile::SectionCharacteristicsText(s.Characteristics), Hex(s.Characteristics)) });
		sections.Keys.push_back({ 0, s.VirtualAddress, s.VirtualSize, s.RawOffset, s.RawSize, 0 });
	}

	auto& imports = m_Tables[(int)View::Imports];
	imports.Columns = { { L"Library", 180 }, { L"Function", 260 }, { L"Hint / Ordinal", 100 }, { L"Delay-loaded", 90 } };
	imports.Numeric = { false, false, true, false };
	for (auto& l : m_File->GetImports())
		for (auto& f : l.Functions) {
			imports.Rows.push_back({ Wide(l.Name), f.ByOrdinal ? std::format(L"(ordinal {})", f.Ordinal) : Wide(f.Name),
				f.ByOrdinal ? std::format(L"#{}", f.Ordinal) : std::to_wstring(f.Hint), l.Delayed ? L"Yes" : L"" });
			imports.Keys.push_back({ 0, 0, f.ByOrdinal ? f.Ordinal : f.Hint, 0 });
		}

	auto& exports = m_Tables[(int)View::Exports];
	exports.Columns = { { L"Ordinal", 70 }, { L"Name", 300 }, { L"RVA", 100 }, { L"Forwarded To", 240 } };
	exports.Numeric = { true, false, true, false };
	if (auto e = m_File->GetExports())
		for (auto& f : e->Functions) {
			exports.Rows.push_back({ std::to_wstring(f.Ordinal), f.Name.empty() ? L"(by ordinal only)" : Wide(f.Name),
				f.Forwarder.empty() ? Hex(f.Rva) : L"", Wide(f.Forwarder) });
			exports.Keys.push_back({ f.Ordinal, 0, f.Forwarder.empty() ? f.Rva : 0, 0 });
		}
}

void CProcImagePage::ShowView() {
	auto view = CComboBox(GetDlgItem(IDC_PE_VIEW)).GetCurSel();
	auto& table = m_Tables[(std::max)(view, 0)];
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	while (m_List.GetHeader().GetItemCount() > 0)
		m_List.DeleteColumn(0);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int c = 0; c < (int)table.Columns.size(); c++)
		m_List.InsertColumn(c, table.Columns[c].first, c < (int)table.Numeric.size() && table.Numeric[c] ? LVCFMT_RIGHT : LVCFMT_LEFT,
			::MulDiv(table.Columns[c].second, dpi, 96));

	// the rows in the sort's order (the file's without one)
	std::vector<int> order(table.Rows.size());
	for (int i = 0; i < (int)order.size(); i++)
		order[i] = i;
	if (m_SortColumn >= 0 && m_SortColumn < (int)table.Columns.size()) {
		auto column = m_SortColumn;
		bool numeric = column < (int)table.Numeric.size() && table.Numeric[column];
		std::ranges::stable_sort(order, [&](int a, int b) {
			int result = numeric ? (table.Keys[a][column] < table.Keys[b][column] ? -1 : table.Keys[a][column] > table.Keys[b][column] ? 1 : 0)
				: ::_wcsicmp(table.Rows[a][column].c_str(), table.Rows[b][column].c_str());
			return m_SortAscending ? result < 0 : result > 0;
			});
	}
	for (int row = 0; row < (int)order.size(); row++) {
		auto& cells = table.Rows[order[row]];
		m_List.InsertItem(row, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			m_List.SetItemText(row, c, cells[c].c_str());
	}
	m_List.SetRedraw(TRUE);

	std::wstring status;
	if (!m_File)
		status = m_Props.ImagePath.empty() ? L"The process has no image file" : L"Can't read the image: " + Helpers::GetErrorText(m_Error);
	else if (view == (int)View::Exports && table.Rows.empty())
		status = L"No exports";
	else if (view == (int)View::Imports && table.Rows.empty())
		status = L"No imports";
	else
		status = std::format(L"{} rows", table.Rows.size());
	SetDlgItemText(IDC_PE_STATUS, status.c_str());
}

LRESULT CProcImagePage::OnViewChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_SortColumn = -1;
	ShowView();
	return 0;
}

LRESULT CProcImagePage::OnColumnClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	auto column = reinterpret_cast<NMLISTVIEW*>(pnmh)->iSubItem;
	m_SortAscending = column == m_SortColumn ? !m_SortAscending : true;
	m_SortColumn = column;
	ShowView();
	return 0;
}

LRESULT CProcImagePage::OnTotalPE(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_Props.ImagePath.empty())
		TotalPE::Open(m_hWnd, m_Props.ImagePath);
	return 0;
}

LRESULT CProcImagePage::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	auto header = m_List.GetHeader();
	int columns = header.GetItemCount();
	for (int c = 0; c < columns; c++) {
		WCHAR name[64]{};
		HDITEM hdi{ HDI_TEXT };
		hdi.pszText = name;
		hdi.cchTextMax = _countof(name);
		header.GetItem(c, &hdi);
		text += (c ? L"\t" : L"") + std::wstring(name);
	}
	text += L"\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		for (int c = 0; c < columns; c++) {
			CString cell;
			m_List.GetItemText(i, c, cell);
			text += (c ? L"\t" : L"") + std::wstring(cell);
		}
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
