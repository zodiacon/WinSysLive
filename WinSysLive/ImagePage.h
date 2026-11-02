#pragma once

#include "resource.h"
#include "ProcessPages.h"
#include "DpiLayout.h"
#include <WinSys/PEFile.h>

//
// The process sheet's Image page: the process's executable as a PE file (WinSys PEFile, read from disk when the page is
// first shown), one list for the view chosen above it: Headers (the file and optional headers' fields in words, then the
// data directories in use), Sections (name, virtual address and size, raw offset and size, protection and contents),
// Imports (library, function, hint or ordinal, delay-loaded), Exports (ordinal, name, RVA, forwarder). A header sorts
// by its column (the numbers by value). The summary beside the view: machine, format, subsystem and the counts. Open in
// TotalPE opens the file in TotalPE (TotalPE.h); Copy copies the selected rows (or all) with the header.
//
class CProcImagePage :
	public CPropertyPageImpl<CProcImagePage>,
	public CDpiDynamicDialogLayout<CProcImagePage> {
public:
	enum { IDD = IDD_PROC_IMAGE };

	explicit CProcImagePage(ProcessProperties const& props);

	BEGIN_MSG_MAP(CProcImagePage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_PE_VIEW, CBN_SELCHANGE, OnViewChanged)
		COMMAND_ID_HANDLER(IDC_PE_TOTALPE, OnTotalPE)
		COMMAND_ID_HANDLER(IDC_PE_COPY, OnCopy)
		NOTIFY_HANDLER(IDC_PE_LIST, LVN_COLUMNCLICK, OnColumnClick)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcImagePage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcImagePage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class View { Headers, Sections, Imports, Exports };

	// a view's table: its columns, which of them sort as numbers, and its rows
	struct Table {
		std::vector<std::pair<PCWSTR, int>> Columns;	// name, width at 96 DPI
		std::vector<bool> Numeric;
		std::vector<std::vector<std::wstring>> Rows;
		std::vector<std::vector<uint64_t>> Keys;		// the numeric columns' values, by row
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnViewChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTotalPE(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	void BuildTables();
	void ShowView();

	ProcessProperties const& m_Props;
	std::optional<WinSys::PEFile> m_File;
	DWORD m_Error{ 0 };
	Table m_Tables[4];
	CListViewCtrl m_List;
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
};
