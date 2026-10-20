#pragma once

#include <TreeListView/include/TreeListView.h>
#include <CustomSplitterWindow.h>
#include <HexControl/SimpleHexControl.h>
#include <wil/resource.h>
#include "ProcessPages.h"

//
// The Memory page's hex view: 16 bytes a line when they fit its width, 8 otherwise, set again whenever it's resized (by
// the page or the splitter)
//
class CMemoryHexView : public CHexControl {
public:
	// the font the control is given; its average character width is the control's column width
	static constexpr PCWSTR FontName = L"Consolas";
	static constexpr int FontSize = 100;	// tenths of a point

	// the width the view takes for the given bytes a line, with 64-bit addresses, its scroll bar and its border
	int GetWidthFor(int bytesPerLine);

	BEGIN_MSG_MAP(CMemoryHexView)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(CHexControl)
	END_MSG_MAP()

private:
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	int m_CharWidth{ 0 };
};

//
// The Memory page: the process's address space (VirtualQueryEx) in a tree on the left, each allocation's first region
// with the allocation's other regions as its children, and the bytes of the selected committed region in a read-only
// hex view on the right, read from the process as they're shown (ProcessMemoryBuffer). The map is read when the page
// is first shown and on Refresh.
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
		std::wstring Details;	// a mapping's file, on its allocation's first region
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// reads the address space; the region at the given address is selected afterwards, if there is one
	void Load(void const* select = nullptr);
	void ShowBytes(Region const* region);
	void Layout(int cx, int cy);
	std::wstring GetText(Region const& region, Column column) const;

	ProcessProperties const& m_Props;
	wil::unique_handle m_hProcess;
	bool m_CanRead{ false };	// m_hProcess has PROCESS_VM_READ
	CCustomSplitterWindow m_Splitter;
	CTreeListViewCtrl m_Tree;
	CMemoryHexView m_Hex;
	std::vector<Region> m_Regions;	// in address order; the tree's item data are indices
	std::unique_ptr<IBufferManager> m_Bytes;	// the hex view's
	std::wstring m_Summary;	// of the map, in the status line
	CSize m_ButtonSize;	// the template's
	bool m_Loaded{ false };
};
