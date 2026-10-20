#pragma once

#include <VirtualListView.h>
#include <WinSys/Handles.h>
#include "ColumnLayout.h"
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
	void SetListFont(HFONT font) {
		m_List.SetFont(font);
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
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		CHAIN_MSG_MAP(CVirtualListView<CHandlesPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

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
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::wstring GetText(HandleRow const& row, Column column) const;
	// queries the name the first time (a closed handle can't be queried anymore)
	std::wstring const& GetName(HandleData& data, bool closed) const;
	std::wstring QueryName(WinSys::HandleEntry const& entry) const;
	// the icon for an object type (files depend on the name)
	static UINT GetTypeIcon(std::wstring const& type);
	void SelectHandle(uint64_t value);
	bool ShowsAddresses() const;
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	IMainFrame* m_Frame{ nullptr };
	CListViewCtrl m_List;
	std::shared_ptr<ProcessInfoEx> m_Process;
	wil::unique_handle m_hProcess;	// with PROCESS_DUP_HANDLE, for the names; null if it can't be opened so
	std::vector<HandleRow> m_Rows;	// in display order
	std::unordered_map<uint64_t, std::shared_ptr<HandleData>> m_Handles;	// by handle value
	WinSys::HandleEnumerator m_Enumerator;	// keeps the system handle table's buffer
	mutable WinSys::ObjectNameQuery m_NameQuery;
	bool m_FirstEnum{ true };	// everything is new on the first enumeration: nothing to highlight
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
};
