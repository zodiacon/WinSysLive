#pragma once

#include <VirtualListView.h>
#include <WinSys/Handles.h>
#include "ColumnLayout.h"
#include "FilterBox.h"
#include "resource.h"

struct IMainFrame;

//
// The Handles tab of the Processes view's lower pane: the handles of the selected process (WinSys HandleEnumerator).
// The object addresses come from the system's handle table, which costs much more, so it's used only while the
// Address column is shown. Names are queried when a row is shown or sorted by name (through a duplicate of the
// handle, with a timeout: see ObjectNameQuery) and kept for the handle's lifetime. New handles are highlighted for
// a while, closed ones are shown for one refresh. The columns are chosen from the header's context menu and kept in
// the HandleColumns setting.
//
class CHandlesPage :
	public CWindowImpl<CHandlesPage>,
	public CVirtualListView<CHandlesPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_HandlesPage", 0, COLOR_WINDOW)

	// for the names of the processes and threads that handles refer to
	void SetFrame(IMainFrame* frame);
	// nullptr: no process. Without enumerating, the list is empty until the next Refresh
	void SetProcess(std::shared_ptr<ProcessInfoEx> process, bool enumerate = true);
	// enumerates the handles again
	void Refresh();
	// the Kill command (the toolbar, Del) while the page has the focus: closes the selected handle in its process,
	// after asking. Not the app's own handles
	bool CanCloseSelected() const;
	void CloseSelected();
	// the selected handle's properties (CHandleSheet): double-click, the row's menu, Properties (Alt+Enter)
	bool CanShowProperties() const;
	void ShowProperties();
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
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	int GetRowImage(HWND, int row, int col) const;

	BEGIN_MSG_MAP(CHandlesPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_PAGE_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_PAGE_COPY, OnCopy)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		COMMAND_ID_HANDLER(ID_HANDLE_PROPERTIES, OnHandleCommand)
		COMMAND_ID_HANDLER(ID_HANDLE_CLOSE, OnHandleCommand)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		CHAIN_MSG_MAP(CVirtualListView<CHandlesPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	// puts the column layout in its setting and saves it (also done when the page is destroyed)
	void SaveColumns() const;

private:
	enum class Column {
		Handle, Type, Name, Address, Access, DecodedAccess, Attributes,
		_Count
	};

	// a handle, kept from refresh to refresh while it's open (its name is queried once)
	struct HandleData {
		WinSys::HandleEntry Entry;
		std::optional<std::wstring> Name;	// nothing until queried
		int NewTicks{ 0 };	// refreshes left to highlight it as opened
	};
	struct HandleRow {
		std::shared_ptr<HandleData> Data;
		bool New{ false };
		bool Closed{ false };	// highlighted as closed, for one refresh
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the rows that match the filter (any shown column contains it), sorted as the list is, with the selection kept
	void ShowRows(uint64_t selected);
	bool Matches(HandleRow const& row) const;
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnHandleCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::wstring GetText(HandleRow const& row, Column column) const;
	// queries the name the first time (a closed handle can't be queried anymore)
	std::wstring const& GetName(HandleData& data, bool closed) const;
	std::wstring QueryName(WinSys::HandleEntry const& entry) const;
	void SelectHandle(uint64_t value);
	HandleRow const* GetSelectedRow() const;
	bool ShowsAddresses() const;
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);

	IMainFrame* m_Frame{ nullptr };
	CListViewCtrl m_List;
	CFilterBox m_Filter;	// above the list
	std::vector<HandleRow> m_AllRows;	// every row of the last refresh; m_Rows has the matching ones
	std::shared_ptr<ProcessInfoEx> m_Process;
	wil::unique_handle m_hProcess;	// with PROCESS_DUP_HANDLE, for the names; null if it can't be opened so
	std::vector<HandleRow> m_Rows;	// in display order
	std::unordered_map<uint64_t, std::shared_ptr<HandleData>> m_Handles;	// by handle value
	WinSys::HandleEnumerator m_Enumerator;	// keeps the system handle table's buffer
	mutable WinSys::ObjectNameQuery m_NameQuery;
	WinSys::DosNames m_DosNames;	// for the files' names
	bool m_FirstEnum{ true };	// everything is new on the first enumeration: nothing to highlight
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
};
