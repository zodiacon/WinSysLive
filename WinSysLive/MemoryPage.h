#pragma once

#include <TreeListView/include/TreeListView.h>
#include <CustomSplitterWindow.h>
#include <HexControl/SimpleHexControl.h>
#include <wil/resource.h>
#include "ProcessPages.h"
#include "MemoryDialogs.h"

//
// The Memory page's hex view: 16 bytes a line when they fit its width, 8 otherwise, set again whenever it's resized (by
// the page or the splitter). Ctrl+F and F3 are the page's Find and Find Next (Ctrl+G, the control's own, is its Go To);
// Insert doesn't switch to insert mode: a process's memory can't grow
//
class CMemoryHexView : public CHexControl {
public:
	// the font the control is given; its average character width is the control's column width
	static constexpr PCWSTR FontName = L"Consolas";
	static constexpr int FontSize = 100;	// tenths of a point

	// the width the view takes for the given bytes a line, with 64-bit addresses, its scroll bar and its border
	int GetWidthFor(int bytesPerLine);
	// where Ctrl+F and F3 go
	void SetPage(HWND page) {
		m_Page = page;
	}

	BEGIN_MSG_MAP(CMemoryHexView)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		CHAIN_MSG_MAP(CHexControl)
	END_MSG_MAP()

private:
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	int m_CharWidth{ 0 };
	UINT m_CharWidthDpi{ 0 };	// the DPI m_CharWidth was measured at
	HWND m_Page{ nullptr };
};

//
// The Memory page: the process's address space (VirtualQueryEx) in a tree on the left, each allocation's first region
// with the allocation's other regions as its children, and the bytes of the selected committed region in a read-only
// hex view on the right, read from the process as they're shown (ProcessMemoryBuffer). The Details column names a
// mapping's file, and the regions holding the PEB and the threads' TEBs and stacks (with the 32-bit ones of a WOW64
// process). The map is read when the page is first shown and on Refresh. The Hex View check box shows or hides the hex
// view (hidden when the page opens); while it's hidden, no bytes are read.
// Find (text, ASCII or Unicode, or hex bytes, in the selected region or the whole address space; WinSys ProcessMemory::Find)
// selects the region of the first match after the caret and the bytes in the hex view; Find Next goes on after it. Go To
// shows the region of an address with the caret on it. Save writes the selected region's bytes to a file (what can't be
// read as zeros). Edit (confirmed once, as it opens the process for writing) makes the hex view writable: each change is
// written to the process at once (WinSys ProcessMemory::Write); a failed write is in the status line, and the bytes are
// read again.
//
class CProcMemoryPage : public CPropertyPageImpl<CProcMemoryPage> {
public:
	enum { IDD = IDD_PROC_MEMORY };

	explicit CProcMemoryPage(ProcessProperties const& props);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcMemoryPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		COMMAND_ID_HANDLER(IDC_MEM_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_MEM_HEXVIEW, OnToggleHexView)
		COMMAND_ID_HANDLER(IDC_MEM_FIND, OnFind)
		COMMAND_ID_HANDLER(IDC_MEM_FINDNEXT, OnFind)
		COMMAND_ID_HANDLER(IDC_MEM_GOTO, OnGoTo)
		COMMAND_ID_HANDLER(IDC_MEM_SAVE, OnSave)
		COMMAND_ID_HANDLER(IDC_MEM_EDIT, OnToggleEdit)
		NOTIFY_CODE_HANDLER(NMHX_GOTO_REQUESTED, OnGoToRequested)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelChanged)
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
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleHexView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToRequested(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// runs the search from the caret (or after the last match, for Find Next)
	void Search(bool next);
	// selects the region holding the address and shows its bytes (the hex view too), with the caret on the address and
	// the length after it selected; false if no region holds it
	bool Reveal(uint64_t address, size_t length);
	// the hex view's caret as an address, or the selected region's start; 0 if there's neither
	uint64_t GetCurrentAddress() const;
	void ShowHexView();
	// the map's summary and the note in the status line
	void SetNote(std::wstring const& note);

	// reads the address space; the region at the given address is selected afterwards, if there is one
	void Load(void const* select = nullptr);
	// names the regions holding the PEB, and each thread's TEB and stack, in their Details
	void LabelProcessStructures();
	// the region at the address, or nullptr
	Region* FindRegion(ULONG_PTR address);
	// adds the text to the region's Details
	static void AddDetails(Region& region, std::wstring const& text);
	// the region's bytes in the hex view, while it's shown
	void ShowBytes(Region const* region);
	// the selected region, or nullptr
	Region const* GetSelectedRegion() const;
	void Layout(int cx, int cy);
	std::wstring GetText(Region const& region, Column column) const;

	ProcessProperties const& m_Props;
	wil::unique_handle m_hProcess;
	bool m_CanRead{ false };	// m_hProcess has PROCESS_VM_READ
	CCustomSplitterWindow m_Splitter;
	CTreeListViewCtrl m_Tree;
	CMemoryHexView m_Hex;
	std::vector<Region> m_Regions;	// in address order; the tree's item data are indices
	std::vector<HTLITEM> m_Items;	// the regions' items
	wil::unique_handle m_hWrite;	// PROCESS_VM_WRITE and PROCESS_VM_OPERATION, once Edit is checked
	bool m_Edit{ false };
	MemorySearch m_Search;
	bool m_Searched{ false };
	uint64_t m_Found{ 0 };	// the last match; Find Next goes on after it
	std::unique_ptr<IBufferManager> m_Bytes;	// the hex view's
	std::wstring m_Summary;	// of the map, in the status line
	CSize m_ButtonSize, m_CheckSize, m_EditSize;	// the template's
	bool m_ShowHex{ false };
	bool m_Loaded{ false };
};
