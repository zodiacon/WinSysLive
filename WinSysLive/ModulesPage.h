#pragma once

#include <VirtualListView.h>
#include <WinSys/ProcessModuleTracker.h>
#include "ColumnLayout.h"
#include "FilterBox.h"
#include "resource.h"

struct IMainFrame;

//
// The Modules tab of the Processes view's lower pane: the images and mapped data files of the selected process,
// from WinSys's ProcessModuleTracker (which walks the address space, so it needs the process opened with
// VM read access). Modules that were loaded are highlighted for a while, unloaded ones are shown for one refresh.
// The columns are chosen from the header's context menu and kept in the ModuleColumns setting.
//
class CModulesPage :
	public CWindowImpl<CModulesPage>,
	public CVirtualListView<CModulesPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_ModulesPage", 0, COLOR_WINDOW)

	// for the frame's image list (the icon of a module's mapping type); before Create
	void SetFrame(IMainFrame* frame) {
		m_Frame = frame;
	}
	// the frame's image list again, after it took a new DPI's size, so the rows take its height
	void ResetImageList();
	// nullptr: no process. Without enumerating, the list is empty until the next Refresh
	void SetProcess(std::shared_ptr<ProcessInfoEx> process, bool enumerate = true);
	// enumerates the modules again
	void Refresh();
	// Ctrl+F while the page has the focus
	void FocusFilter() {
		m_Filter.Focus();
	}
	void SetListFont(HFONT font) {
		m_List.SetFont(font);
		m_Filter.SetFont(font);
		// a hidden page's header isn't all repainted otherwise
		m_List.GetHeader().Invalidate();
	}

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;

	BEGIN_MSG_MAP(CModulesPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_PAGE_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_PAGE_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_MODULE_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_MODULE_GOTOFILELOCATION, OnGoToFileLocation)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		CHAIN_MSG_MAP(CVirtualListView<CModulesPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	// puts the column layout in its setting and saves it (also done when the page is destroyed)
	void SaveColumns() const;

private:
	enum class Column {
		Name, BaseAddress, Size, Type, ImageBase, Description, Company, Version, Characteristics, Path,
		_Count
	};

	struct ModuleRow {
		std::shared_ptr<WinSys::ModuleInfo> Module;
		bool New{ false };		// highlighted as loaded
		bool Unloaded{ false };	// highlighted as unloaded, for one refresh
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	ModuleRow const* GetSelectedRow() const;
	// the rows that match the filter (any shown column contains it), sorted as the list is, with the selection kept
	void ShowRows(void* selected);
	// columns: the shown ones, taken once for all the rows (from the header)
	bool Matches(ModuleRow const& row, std::vector<ColumnLayout::ShownColumn> const& columns) const;
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::wstring GetText(ModuleRow const& row, Column column) const;
	void SelectModule(void* base);
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// above the list
	std::vector<ModuleRow> m_AllRows;	// every row of the last refresh; m_Rows has the matching ones
	IMainFrame* m_Frame{ nullptr };
	std::shared_ptr<ProcessInfoEx> m_Process;
	std::unique_ptr<WinSys::ProcessModuleTracker> m_Tracker;
	std::vector<ModuleRow> m_Rows;	// in display order
	std::unordered_map<WinSys::ModuleInfo const*, int> m_NewTicks;	// refreshes left to highlight a loaded module
	bool m_FirstEnum{ true };	// everything is new on the first enumeration: nothing to highlight
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
};
