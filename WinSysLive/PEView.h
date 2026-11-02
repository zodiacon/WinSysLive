#pragma once

#include <WinSys/PEFile.h>

//
// A PE file shown in a dialog's controls (IDC_PE_VIEW, a combo box of the views; IDC_PE_SUMMARY; IDC_PE_LIST, a report
// list view; IDC_PE_STATUS; IDC_PE_TOTALPE and IDC_PE_COPY buttons): the process sheet's Image page and the module
// dialog (CPEFileDlg) host it and route their messages to it. The file (WinSys PEFile) is read by Init; one list shows
// the view chosen: Headers (the file and optional headers' fields in words, then the data directories in use),
// Sections (name, virtual address and size, raw offset and size, protection and contents), Imports (library, function,
// hint or ordinal, delay-loaded), Exports (ordinal, name, RVA, forwarder), Debug (the debug directory's entries: the
// CodeView's PDB path, GUID and age, which symbols match; the extended DLL characteristics, CET among them), Resources
// (type, name, language, size, code page), Version (the version resource's fixed versions and strings: WinSys
// FileVersion::ReadDetails), Manifest (the manifest's lines) and .NET (the CLR header: managed or mixed, the metadata's
// runtime version, the flags). The Headers view says whether the checksum matches the computed one. A header
// sorts by its column (the numbers by value). Open in TotalPE opens the file there (TotalPE.h); Copy copies the
// selected rows (or all) with the header.
//
class CPEView {
public:
	// reads the file and shows the first view; path empty: "no image file"
	void Init(HWND hDlg, std::wstring path);
	void OnViewChanged();
	void OnColumnClick(int column);
	void OnTotalPE();
	void OnCopy();

private:
	enum class View { Headers, Sections, Imports, Exports, Debug, Resources, Version, Manifest, DotNet, _Count };

	// a view's table: its columns, which of them sort as numbers, and its rows
	struct Table {
		std::vector<std::pair<PCWSTR, int>> Columns;	// name, width at 96 DPI
		std::vector<bool> Numeric;
		std::vector<std::vector<std::wstring>> Rows;
		std::vector<std::vector<uint64_t>> Keys;		// the numeric columns' values, by row
	};

	void BuildTables();
	// Resources, Version, Manifest and .NET
	void BuildMoreTables();
	void ShowView();

	CWindow m_Dlg;
	std::wstring m_Path;
	std::optional<WinSys::PEFile> m_File;
	DWORD m_Error{ 0 };
	Table m_Tables[(int)View::_Count];
	CListViewCtrl m_List;
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
};
