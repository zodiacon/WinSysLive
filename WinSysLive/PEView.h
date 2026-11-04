#pragma once

#include <WinSys/PEFile.h>
#include <functional>

//
// A PE file shown in a dialog's controls (IDC_PE_VIEW, a combo box of the views; IDC_PE_SUMMARY; IDC_PE_LIST, a report
// list view; IDC_PE_STATUS; IDC_PE_TOTALPE and IDC_PE_COPY buttons): the process sheet's Image page and the module
// dialog (CPEFileDlg) host it and route their messages to it. The file (WinSys PEFile) is read by Init; one list shows
// the view chosen: Headers (the signature, the file and optional headers' fields in words, then the data directories in use),
// Sections (name, virtual address and size, raw offset and size, protection and contents), Imports (library, function,
// hint or ordinal, delay-loaded), Exports (ordinal, name, RVA, forwarder), Debug (the debug directory's entries: the
// CodeView's PDB path, GUID and age, which symbols match; the extended DLL characteristics, CET among them), Resources
// (type, name, language, size, code page), Icons (each icon of each icon group drawn at a fixed preview size, with its
// own size, colors, bytes and RT_ICON ID: PEFile::GetIconGroup, CreateIconFromResourceEx, which takes PNG ones), Version (the version resource's fixed versions and strings: WinSys
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

	// called when the list is replaced (leaving the Icons view), for a host whose layout holds the list's window
	// (AFX_DIALOG_LAYOUT's CDynamicDialogLayout; CDialogResize finds it by its ID, which the new one keeps)
	std::function<void(HWND oldList, HWND newList)> ListReplaced;

private:
	enum class View { Headers, Sections, Imports, Exports, Debug, Resources, Icons, Version, Manifest, DotNet, _Count };

	// a view's table: its columns, which of them sort as numbers, and its rows
	struct Table {
		std::vector<std::pair<PCWSTR, int>> Columns;	// name, width at 96 DPI
		std::vector<bool> Numeric;
		std::vector<std::vector<std::wstring>> Rows;
		std::vector<std::vector<uint64_t>> Keys;		// the numeric columns' values, by row
		std::vector<int> Images;	// each row's image in m_IconImages (the Icons view's); empty: no images
	};

	void BuildTables();
	// Resources, Version, Manifest and .NET
	void BuildMoreTables();
	void ShowView();
	// extended styles and column DPI scaling, on the dialog's list and on each new one
	void SetupList();
	// a new list in the old one's place: a list view that had an image list keeps its rows' height and the first
	// column's indent with none (comctl32: neither LVM_SETIMAGELIST nor WM_SETFONT shrinks them back)
	void RecreateList();

	CWindow m_Dlg;
	std::wstring m_Path;
	std::optional<WinSys::PEFile> m_File;
	DWORD m_Error{ 0 };
	Table m_Tables[(int)View::_Count];
	CListViewCtrl m_List;
	CImageListManaged m_IconImages;	// the icons at the preview size (the list shares it: LVS_SHAREIMAGELISTS)
	bool m_ListHasImages{ false };	// the list was given m_IconImages (it's replaced for a view without them)
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
};
