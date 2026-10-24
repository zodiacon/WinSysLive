#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/Handles.h>
#include <VirtualListView.h>

//
// The Object Types tab, as ObjectExplorer's: the kernel's object types (WinSys ObjectTypes::Enum, one query) with their
// object and handle counts and peaks, pool type and default charges, valid access mask, generic mapping (decoded by
// WinSys AccessMaskDecoder) and invalid attributes. Read again on every refresh tick, also while hidden; a count that
// changed since the previous refresh has its cell colored for that refresh, green up and red down. Each type has its
// Handles page icon (GetHandleIcon). The filter box matches the name; Copy copies the selected row's shown columns.
//
class CObjectTypesView :
	public CViewBase<CObjectTypesView>,
	public CVirtualListView<CObjectTypesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	bool CanCopy() const override {
		return m_List.GetSelectedCount() > 0;
	}
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override {
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CObjectTypesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CObjectTypesView>)
		CHAIN_MSG_MAP(CViewBase<CObjectTypesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		CHAIN_MSG_MAP_ALT(CViewBase<CObjectTypesView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Name, Index, Objects, Handles, PeakObjects, PeakHandles, PoolType, PagedCharge, NonPagedCharge,
		ValidAccess, GenericRead, GenericWrite, GenericExecute, GenericAll, InvalidAttributes, SecurityRequired,
		MaintainHandleCount, _Count };

	struct Row {
		WinSys::ObjectTypeInfo Type;
		// since the previous refresh, for Objects, Handles, PeakObjects and PeakHandles (Column order): -1, 0 or 1
		int8_t Change[4]{};
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the ObjectTypeColumns setting
	void SaveColumns() const;

	// the types read again, each compared with its previous counts
	void Update();
	std::wstring GetText(Row const& row, Column column) const;
	static std::wstring AttributesText(uint32_t attributes);
	// the list's rows: the types that match the filter, sorted as the list is; selected: the type to select (by index)
	void ShowRows(int selected);
	int GetSelectedIndex() const;
	void SelectType(int index);
	void UpdateStatus();

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::vector<Row> m_All;
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	COLORREF m_RowText{ CLR_DEFAULT }, m_RowTextBk{ CLR_DEFAULT };	// the row being drawn, as the list chose them
	bool m_Active{ false };
};
