#include "pch.h"
#include "PEView.h"
#include "resource.h"
#include "FormatHelper.h"
#include "TotalPE.h"
#include "XmlText.h"
#include "Signature.h"
#include <ClipboardHelper.h>
#include <ListViewhelper.h>
#include <WinSys/Helpers.h>
#include <WinSys/FileVersion.h>

using namespace WinSys;

namespace {
	std::wstring Hex(uint64_t value, int digits = 8) {
		return std::format(L"0x{:0{}X}", value, digits);
	}

	std::wstring Wide(std::string const& text) {
		return std::wstring(text.begin(), text.end());	// names in PE files are ASCII
	}

	// the PDB's path as the linker wrote it: UTF-8
	std::wstring FromUtf8(std::string const& text) {
		if (text.empty())
			return L"";
		auto size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
		std::wstring result(size, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), result.data(), size);
		return result;
	}

	// a time stamp as a date, unless it's a reproducible build's hash (not a plausible time)
	std::wstring TimeStampText(uint32_t stamp) {
		auto now = (uint64_t)::_time64(nullptr);
		if (stamp < 631152000 || stamp > now + 86400)	// before 1990, or in the future
			return std::format(L"{} (a hash: a reproducible build)", Hex(stamp));
		auto filetime = ((int64_t)stamp + 11644473600LL) * 10000000;
		return FormatHelper::FormatDateTime(filetime) + L" (UTC " + Hex(stamp) + L")";
	}

	std::wstring GuidText(GUID const& guid) {
		WCHAR text[64];
		return ::StringFromGUID2(guid, text, _countof(text)) ? text : L"";
	}
}

void CPEView::Init(HWND hDlg, std::wstring path) {
	m_Dlg = hDlg;
	m_Path = std::move(path);
	m_List.Attach(m_Dlg.GetDlgItem(IDC_PE_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	// the Icons view's image list is ours
	m_List.ModifyStyle(0, LVS_SHAREIMAGELISTS);
	ListViewHelper::EnableColumnDpiScaling(m_List);
	CComboBox view(m_Dlg.GetDlgItem(IDC_PE_VIEW));
	for (auto name : { L"Headers", L"Sections", L"Imports", L"Exports", L"Debug", L"Resources", L"Icons", L"Version", L"Manifest", L".NET" })
		view.AddString(name);
	view.SetCurSel(0);

	if (m_Path.empty())
		m_Error = ERROR_FILE_NOT_FOUND;
	else {
		CWaitCursor wait;
		m_File = PEFile::Load(m_Path);
		if (!m_File)
			m_Error = ::GetLastError();
	}
	m_Dlg.GetDlgItem(IDC_PE_TOTALPE).EnableWindow(!m_Path.empty());
	if (m_File) {
		BuildTables();
		BuildMoreTables();
		auto& h = m_File->GetHeaders();
		auto imports = m_File->GetImports();
		size_t functions = 0;
		for (auto& l : imports)
			functions += l.Functions.size();
		auto exports = m_File->GetExports();
		m_Dlg.SetDlgItemText(IDC_PE_SUMMARY, std::format(L"{} {}, {}; {} sections, {} imports from {} libraries, {} exports",
			PEFile::MachineName(h.Machine), h.Is64 ? L"PE32+" : L"PE32", PEFile::SubsystemName(h.Subsystem),
			m_File->GetSections().size(), functions, imports.size(), exports ? exports->Functions.size() : 0).c_str());
	}
	ShowView();
}

void CPEView::BuildTables() {
	auto& h = m_File->GetHeaders();

	auto& headers = m_Tables[(int)View::Headers];
	headers.Columns = { { L"Field", 170 }, { L"Value", 470 } };
	auto field = [&](PCWSTR name, std::wstring value) { headers.Rows.push_back({ name, std::move(value) }); };
	field(L"File", m_Path);
	// Authenticode: the embedded signature, else a catalog's (as the General page's check)
	field(L"Signature", FormatSignature(FileSignature::Verify(m_Path.c_str())));
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
	// the header's checksum against the computed one (drivers must match; executables mostly leave it 0)
	auto computed = m_File->ComputeChecksum();
	field(L"Checksum", h.CheckSum == 0 ? std::format(L"0 (none; computed {})", Hex(computed))
		: h.CheckSum == computed ? std::format(L"{} (correct)", Hex(h.CheckSum))
		: std::format(L"{} (wrong: computed {})", Hex(h.CheckSum), Hex(computed)));
	if (auto net = m_File->GetDotNet())
		field(L".NET", std::format(L"{}, metadata {}", net->IsILOnly() ? L"Managed (IL only)" : L"Mixed (managed and native)",
			Wide(net->MetadataVersion)));
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

	auto& debug = m_Tables[(int)View::Debug];
	debug.Columns = { { L"Type", 170 }, { L"Size", 70 }, { L"RVA", 100 }, { L"Details", 480 } };
	debug.Numeric = { false, true, true, false };
	auto codeView = m_File->GetCodeView();
	for (auto& d : m_File->GetDebugEntries()) {
		std::wstring details;
		if (d.Type == IMAGE_DEBUG_TYPE_CODEVIEW && codeView)
			details = std::format(L"{}, GUID {}, age {}", FromUtf8(codeView->PdbPath), GuidText(codeView->Signature), codeView->Age);
		else if (d.Type == 20)	// IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS
			details = std::format(L"{} ({})", PEFile::ExDllCharacteristicsText(d.Value), Hex(d.Value, 2));
		else if (d.Type == 16)	// IMAGE_DEBUG_TYPE_REPRO
			details = L"A reproducible build: the time stamps are hashes of its inputs";
		debug.Rows.push_back({ std::format(L"{} ({})", PEFile::DebugTypeName(d.Type), d.Type), std::to_wstring(d.Size), Hex(d.Rva), details });
		debug.Keys.push_back({ 0, d.Size, d.Rva, 0 });
	}
}

void CPEView::BuildMoreTables() {
	auto resources = m_File->GetResources();
	auto& res = m_Tables[(int)View::Resources];
	res.Columns = { { L"Type", 150 }, { L"Name", 160 }, { L"Language", 80 }, { L"Size", 90 }, { L"RVA", 100 }, { L"Code Page", 80 } };
	res.Numeric = { false, false, true, true, true, true };
	for (auto& r : resources) {
		auto type = r.TypeId ? std::wstring(PEFile::ResourceTypeName(r.TypeId)) : r.Type;
		if (r.TypeId && type.empty())
			type = std::format(L"#{}", r.TypeId);
		res.Rows.push_back({ type, r.NameId ? std::format(L"#{}", r.NameId) : r.Name, std::format(L"0x{:04X}", r.Language),
			std::to_wstring(r.Size), Hex(r.Rva), std::to_wstring(r.CodePage) });
		res.Keys.push_back({ 0, 0, r.Language, r.Size, r.Rva, r.CodePage });
	}

	// each icon group's icons, drawn at the preview size (a small one stretched: its own size is in its row)
	auto& icons = m_Tables[(int)View::Icons];
	icons.Columns = { { L"Group", 120 }, { L"Size", 90 }, { L"Colors", 90 }, { L"Bytes", 80 }, { L"Icon ID", 70 }, { L"Language", 80 } };
	icons.Numeric = { false, true, true, true, true, true };
	auto preview = ::MulDiv(48, ::GetDpiForWindow(m_Dlg), 96);
	m_IconImages.Create(preview, preview, ILC_COLOR32 | ILC_MASK, 16, 16);
	for (auto& group : resources) {
		if (group.TypeId != 14)
			continue;
		auto groupName = group.NameId ? std::format(L"#{}", group.NameId) : group.Name;
		for (auto& e : m_File->GetIconGroup(group)) {
			int image = -1;
			if (auto icon = m_File->FindIcon(resources, e.IconId, group.Language)) {
				auto data = m_File->GetResourceData(*icon);
				if (!data.empty())
					if (auto hIcon = ::CreateIconFromResourceEx(data.data(), (DWORD)data.size(), TRUE, 0x00030000, e.Width, e.Height, LR_DEFAULTCOLOR)) {
						image = m_IconImages.AddIcon(hIcon);
						::DestroyIcon(hIcon);
					}
			}
			auto colors = e.BitCount ? std::format(L"{}-bit", e.BitCount) : e.ColorCount ? std::format(L"{} colors", e.ColorCount) : std::wstring(L"");
			icons.Rows.push_back({ groupName, std::format(L"{} \x00D7 {}", e.Width, e.Height), colors, std::to_wstring(e.Size),
				std::to_wstring(e.IconId), std::format(L"0x{:04X}", group.Language) });
			icons.Keys.push_back({ 0, (uint64_t)e.Width * 1000 + e.Height, e.BitCount, e.Size, e.IconId, group.Language });
			icons.Images.push_back(image);
		}
	}

	auto& version = m_Tables[(int)View::Version];
	version.Columns = { { L"Field", 170 }, { L"Value", 470 } };
	if (auto v = FileVersion::ReadDetails(m_Path)) {
		version.Rows.push_back({ L"File version", FileVersion::VersionText(v->FileVersion) });
		version.Rows.push_back({ L"Product version", FileVersion::VersionText(v->ProductVersion) });
		std::wstring flags;
		for (auto [flag, name] : { std::pair{ (DWORD)VS_FF_DEBUG, L"Debug" }, { (DWORD)VS_FF_PRERELEASE, L"Prerelease" },
			{ (DWORD)VS_FF_PATCHED, L"Patched" }, { (DWORD)VS_FF_PRIVATEBUILD, L"Private build" }, { (DWORD)VS_FF_SPECIALBUILD, L"Special build" } })
			if (v->FileFlags & flag)
				flags += (flags.empty() ? L"" : L", ") + std::wstring(name);
		if (!flags.empty())
			version.Rows.push_back({ L"Flags", flags });
		WCHAR language[128]{};
		::VerLanguageName(v->Language, language, _countof(language));
		version.Rows.push_back({ L"Language", std::format(L"{} (0x{:04X}), code page {}", language, v->Language, v->CodePage) });
		for (auto& [name, value] : v->Strings)
			version.Rows.push_back({ name, value });
	}

	// the manifest's lines (one line of XML broken at its tags)
	auto& manifest = m_Tables[(int)View::Manifest];
	manifest.Columns = { { L"Manifest", 900 } };
	if (auto it = std::ranges::find_if(resources, [](auto& r) { return r.TypeId == 24; }); it != resources.end()) {
		auto data = m_File->GetResourceData(*it);
		std::string text(data.begin(), data.end());
		if (text.starts_with("\xEF\xBB\xBF"))
			text.erase(0, 3);
		// a line per element, indented (the linker writes the merged manifest on one line)
		auto wide = XmlText::Indent(FromUtf8(text));
		size_t start = 0;
		while (start < wide.size()) {
			auto end = wide.find(L'\n', start);
			auto line = wide.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
			if (!line.empty() && line.back() == L'\r')
				line.pop_back();
			// tabs as spaces: the list shows no tabs
			for (size_t t; (t = line.find(L'\t')) != std::wstring::npos; )
				line.replace(t, 1, L"    ");
			if (!line.empty())
				manifest.Rows.push_back({ line });
			if (end == std::wstring::npos)
				break;
			start = end + 1;
		}
	}

	auto& net = m_Tables[(int)View::DotNet];
	net.Columns = { { L"Field", 170 }, { L"Value", 470 } };
	if (auto n = m_File->GetDotNet()) {
		net.Rows.push_back({ L"Kind", n->IsILOnly() ? L"Managed (IL only)" : L"Mixed (managed and native: C++/CLI)" });
		net.Rows.push_back({ L"Metadata version", Wide(n->MetadataVersion) + L" (.NET Framework 4 and .NET Core/5+ alike)" });
		net.Rows.push_back({ L"CLR header version", std::format(L"{}.{}", n->RuntimeMajor, n->RuntimeMinor) });
		net.Rows.push_back({ L"Flags", std::format(L"{} ({})", PEFile::DotNetFlagsText(n->Flags), Hex(n->Flags)) });
		net.Rows.push_back({ L"Entry point", n->EntryPointToken == 0 ? std::wstring(L"(none: a library)")
			: (n->Flags & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT) ? L"RVA " + Hex(n->EntryPointToken) : L"Token " + Hex(n->EntryPointToken) });
		net.Rows.push_back({ L"Metadata", std::format(L"RVA {}, {} bytes", Hex(n->MetadataRva), n->MetadataSize) });
	}
}

void CPEView::ShowView() {
	auto view = CComboBox(m_Dlg.GetDlgItem(IDC_PE_VIEW)).GetCurSel();
	auto& table = m_Tables[(std::max)(view, 0)];
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	while (m_List.GetHeader().GetItemCount() > 0)
		m_List.DeleteColumn(0);
	auto dpi = ::GetDpiForWindow(m_Dlg);
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
	// the Icons view's previews, as the rows' images; none for the other views
	bool images = !table.Images.empty();
	m_List.SetImageList(images ? m_IconImages.m_hImageList : nullptr, LVSIL_SMALL);
	for (int row = 0; row < (int)order.size(); row++) {
		auto& cells = table.Rows[order[row]];
		if (images)
			m_List.InsertItem(LVIF_TEXT | LVIF_IMAGE, row, cells[0].c_str(), 0, 0, table.Images[order[row]], 0);
		else
			m_List.InsertItem(row, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			m_List.SetItemText(row, c, cells[c].c_str());
	}
	m_List.SetRedraw(TRUE);

	std::wstring status;
	if (!m_File)
		status = m_Path.empty() ? L"No image file" : L"Can't read the image: " + Helpers::GetErrorText(m_Error);
	else if (table.Rows.empty())
		status = view == (int)View::Exports ? L"No exports" : view == (int)View::Imports ? L"No imports" : view == (int)View::Debug
			? L"No debug directory" : view == (int)View::Resources ? L"No resources" : view == (int)View::Icons ? L"No icons" : view == (int)View::Version ? L"No version resource"
			: view == (int)View::Manifest ? L"No manifest" : view == (int)View::DotNet ? L"Not a .NET image (a .NET Core app's exe is a native host: its DLL is the managed image)" : L"None";
	else
		status = std::format(L"{} rows", table.Rows.size());
	m_Dlg.SetDlgItemText(IDC_PE_STATUS, status.c_str());
}

void CPEView::OnViewChanged() {
	m_SortColumn = -1;
	ShowView();
}

void CPEView::OnColumnClick(int column) {
	m_SortAscending = column == m_SortColumn ? !m_SortAscending : true;
	m_SortColumn = column;
	ShowView();
}

void CPEView::OnTotalPE() {
	if (!m_Path.empty())
		TotalPE::Open(m_Dlg, m_Path);
}

void CPEView::OnCopy() {
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
	ClipboardHelper::CopyText(m_Dlg, text.c_str());
}
