#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/ObjectNamespace.h>
#include <VirtualListView.h>
#include <CustomSplitterWindow.h>

//
// The Object Manager tab, as WinObj and ObjectExplorer have it: the object namespace's directories in a tree on the left
// (filled as nodes are expanded), the selected directory's objects on the right (WinSys ObjectNamespace), in a vertical
// splitter. The list is read again on every tick while the tab is active, merged by name: new objects green for two
// refreshes, removed ones red once; the tree's children of the selected directory follow it. Symbolic links show their
// targets (read once per link) and creation times; Handles is read for the rows shown (an object opened by name through
// its type's open function, so only the types WinSys ObjectNamespace::Open knows, and only those the user may open), again
// on every refresh. Double-clicking a directory opens it, a symbolic link goes to its target (its deepest directory in the
// namespace and the object in it), anything else opens its properties: the handle sheet (CHandleSheet) on a handle this
// process opens by name, with the most access it gets (MAXIMUM_ALLOWED). The filter box matches the name, type and
// target. The directory, the splitter's position and the columns are kept in the ObjectManagerPath,
// ObjectManagerSplitterPos and ObjectManagerColumns settings.
//
class CObjectManagerView :
	public CViewBase<CObjectManagerView>,
	public CVirtualListView<CObjectManagerView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	// the tree's directory (its path) or the list's object, as OnCopy
	bool CanCopy() const override {
		return ::GetFocus() == m_Tree ? m_Tree.GetSelectedItem() != nullptr : m_List.GetSelectedCount() > 0;
	}
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void OnFrameIdle() override;
	void SaveSettings() override;
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override;

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CObjectManagerView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		// the tree's, which the splitter forwards, before the list's handlers see them
		NOTIFY_HANDLER(TreeId, TVN_SELCHANGED, OnTreeSelChanged)
		NOTIFY_HANDLER(TreeId, TVN_ITEMEXPANDING, OnTreeExpanding)
		NOTIFY_HANDLER(TreeId, NM_RCLICK, OnTreeRightClick)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CObjectManagerView>)
		CHAIN_MSG_MAP(CViewBase<CObjectManagerView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_OBJMGR_COPYPATH, OnCopyPath)
		COMMAND_ID_HANDLER(ID_OBJMGR_COPYDIRECTORY, OnCopyDirectory)
		COMMAND_ID_HANDLER(ID_OBJMGR_OPEN, OnOpen)
		COMMAND_ID_HANDLER(ID_OBJMGR_GOTOTARGET, OnGoToTarget)
		COMMAND_ID_HANDLER(ID_OBJMGR_DIRPROPERTIES, OnDirectoryProperties)
		COMMAND_ID_HANDLER(ID_VIEW_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		CHAIN_MSG_MAP_ALT(CViewBase<CObjectManagerView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Name, Type, Target, Handles, Created, FullPath, _Count };
	enum { TreeId = 100 };

	struct Row {
		std::wstring Name, Type, Target;
		int64_t Created{ 0 };	// a symbolic link's
		int New{ 0 };	// refreshes left with the new highlight
		bool Removed{ false };	// gone: shown for RemovedTicks more refreshes, red
		int RemovedTicks{ 0 };
		// read for the rows shown, again on every refresh
		mutable std::optional<uint32_t> Handles;
		mutable bool HandlesRead{ false };
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTreeSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnTreeExpanding(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnTreeRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyPath(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyDirectory(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpen(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToTarget(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDirectoryProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	// the tree: a node's path from its ancestors' names; its subdirectories inserted when first expanded (item data 1),
	// then kept in step with the directory's listing while it's the selected one
	std::wstring GetItemPath(HTREEITEM item) const;
	void FillNode(HTREEITEM item);
	void SyncNode(HTREEITEM item, std::vector<WinSys::NamespaceEntry> const& entries);
	HTREEITEM FindChild(HTREEITEM parent, std::wstring const& name) const;
	// selects the directory's node, expanding its ancestors; null if a component isn't a listable directory
	HTREEITEM SelectDirectory(std::wstring const& path);
	// the selected directory read again; newDirectory: a directory just selected (no highlights, the top of the list)
	void Load(bool newDirectory);
	std::wstring GetText(Row const& row, Column column) const;
	std::wstring FullPath(Row const& row) const;
	bool Matches(Row const& row) const;
	// the list's rows: those that match the filter, sorted as the list is (by name until a header is clicked)
	void ShowRows(std::wstring const& selected);
	Row const* GetSelected() const;
	void SelectRow(std::wstring const& name);
	void GoToTarget(std::wstring const& target);
	void ShowProperties(std::wstring const& path, std::wstring const& type);
	void UpdateStatus();
	static UINT ObjectIcon(std::wstring const& type);

	CCustomSplitterWindow m_Splitter;
	CTreeViewCtrl m_Tree;
	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::wstring m_Path;	// the selected directory
	std::vector<Row> m_All;	// its objects
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	DWORD m_Error{ 0 };	// listing the directory's, if it failed
	COLORREF m_RowText{ CLR_DEFAULT }, m_RowTextBk{ CLR_DEFAULT };	// the row being drawn, as the list chose them
	bool m_Active{ false };
	bool m_Loaded{ false };	// the tree and the first directory, when the tab is first shown
};
