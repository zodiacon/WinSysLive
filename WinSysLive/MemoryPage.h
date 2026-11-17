#pragma once

#include <TreeListView/include/TreeListView.h>
#include <wil/resource.h>
#include "ProcessPages.h"
#include "MemoryDialogs.h"
#include "MemoryStringsDlg.h"
#include "MemoryRegionDlg.h"

//
// The Memory page: the process's address space (VirtualQueryEx) in a tree, each allocation's first region with the
// allocation's other regions as its children. The Details column names a mapping's file, and the regions holding the
// PEB and the threads' TEBs and stacks (with the 32-bit ones of a WOW64 process). The map is read when the page is first
// shown and on Refresh. View (or a double-click on a region) opens the selected committed region's bytes in a modeless
// dialog of its own (CMemoryRegionDlg), one a region, several open at once; their Edit toggles ask the page for the
// handle to write with (OpenForWriting: confirmed once, as it opens the process for writing).
// Find (text, ASCII or Unicode, or hex bytes, in the selected region or the whole address space; WinSys ProcessMemory::Find)
// selects the region of the first match after the caret (of the region's dialog) and shows its bytes in the region's
// dialog; Find Next goes on after it. Go To shows the region of an address with the caret on it. Strings
// (CMemoryStringsDlg) lists the text in the region or the address space; its Go To shows a string. Save writes the
// selected region's bytes to a file (what can't be read as zeros).
//
class CProcMemoryPage : public CPropertyPageImpl<CProcMemoryPage> {
public:
	enum { IDD = IDD_PROC_MEMORY };

	explicit CProcMemoryPage(ProcessProperties const& props);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcMemoryPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		COMMAND_ID_HANDLER(IDC_MEM_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_MEM_FIND, OnFind)
		COMMAND_ID_HANDLER(IDC_MEM_FINDNEXT, OnFind)
		COMMAND_ID_HANDLER(IDC_MEM_GOTO, OnGoTo)
		COMMAND_ID_HANDLER(IDC_MEM_SAVE, OnSave)
		COMMAND_ID_HANDLER(IDC_MEM_STRINGS, OnStrings)
		COMMAND_ID_HANDLER(IDC_MEM_VIEW, OnView)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelChanged)
		NOTIFY_CODE_HANDLER(TLVN_ACTIVATE, OnActivate)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcMemoryPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Address, Size, State, Type, Protection, AllocationProtection, Details,
		_Count
	};

	struct Region {
		MEMORY_BASIC_INFORMATION Info;
		std::wstring Details;	// a mapping's file, on its allocation's first region; what the region holds (PEB, TEB, stack)
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnActivate(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStrings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the region holding an address, for the strings: its type and what it holds (its allocation's file)
	std::wstring DescribeAddress(uint64_t address);

	// the parent of a dialog a command shows: the region dialog it came from (the hex view's keys), else the page
	HWND GetDialogParent(HWND from) const;
	// the open dialog of the region at the address, or nullptr
	CMemoryRegionDlg* FindRegionDlg(uint64_t base) const;
	// opens the region's dialog, or activates the open one; nullptr, with the reason in the status line, if its bytes
	// can't be shown
	CMemoryRegionDlg* OpenRegion(Region const& region);
	// the handle to write with, opened once after asking (over the parent); null if it's declined or can't be opened
	HANDLE OpenForWriting(HWND parent);
	// runs the search from the caret (or after the last match, for Find Next)
	void Search(bool next);
	// selects the region holding the address and shows its bytes in its dialog, with the caret on the address and the
	// length after it selected; false if no region holds it
	bool Reveal(uint64_t address, size_t length, std::wstring const& note = L"");
	// the caret of the selected region's dialog as an address, or the selected region's start; 0 if there's neither
	uint64_t GetCurrentAddress() const;
	// the map's summary and the note in the status line
	void SetNote(std::wstring const& note);
	// View for the selected region
	void EnableButtons();

	// reads the address space; the region at the given address is selected afterwards, if there is one
	void Load(void const* select = nullptr);
	// names the regions holding the PEB, and each thread's TEB and stack, in their Details
	void LabelProcessStructures();
	// the region at the address, or nullptr
	Region* FindRegion(ULONG_PTR address);
	// adds the text to the region's Details
	static void AddDetails(Region& region, std::wstring const& text);
	// the selected region, or nullptr
	Region const* GetSelectedRegion() const;
	void Layout(int cx, int cy);
	std::wstring GetText(Region const& region, Column column) const;

	ProcessProperties const& m_Props;
	wil::unique_handle m_hProcess;
	bool m_CanRead{ false };	// m_hProcess has PROCESS_VM_READ
	CTreeListViewCtrl m_Tree;
	std::vector<Region> m_Regions;	// in address order; the tree's item data are indices
	std::vector<HTLITEM> m_Items;	// the regions' items
	wil::unique_handle m_hWrite;	// PROCESS_VM_WRITE and PROCESS_VM_OPERATION, once a dialog's Edit is confirmed
	// the open region dialogs, which delete themselves (removed from here as they go)
	std::vector<CMemoryRegionDlg*> m_RegionDlgs;
	MemorySearch m_Search;
	bool m_Searched{ false };
	uint64_t m_Found{ 0 };	// the last match; Find Next goes on after it
	MemoryStrings m_Strings;	// the Strings dialog's options and last scan
	std::wstring m_Summary;	// of the map, in the status line
	CSize m_ButtonSize;	// the template's
	bool m_Loaded{ false };
};
