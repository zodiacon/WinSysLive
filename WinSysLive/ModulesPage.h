#pragma once

#include <VirtualListView.h>
#include <WinSys/ProcessModuleTracker.h>
#include "ColumnLayout.h"
#include "resource.h"

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

	// nullptr: no process
	void SetProcess(std::shared_ptr<ProcessInfoEx> process);
	// enumerates the modules again
	void Refresh();

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;

	BEGIN_MSG_MAP(CModulesPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		CHAIN_MSG_MAP(CVirtualListView<CModulesPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

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
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::wstring GetText(ModuleRow const& row, Column column) const;
	void SelectModule(void* base);
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	CListViewCtrl m_List;
	std::shared_ptr<ProcessInfoEx> m_Process;
	std::unique_ptr<WinSys::ProcessModuleTracker> m_Tracker;
	std::vector<ModuleRow> m_Rows;	// in display order
	std::unordered_map<WinSys::ModuleInfo const*, int> m_NewTicks;	// refreshes left to highlight a loaded module
	bool m_FirstEnum{ true };	// everything is new on the first enumeration: nothing to highlight
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
};
