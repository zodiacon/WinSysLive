#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/Zombies.h>
#include <VirtualListView.h>
#include <thread>
#include <mutex>

//
// The Zombie Processes tab: processes that exited but are kept alive by open handles (WinSys ZombieProcesses, which
// duplicates every Process handle in the system: elevated, or it sees only the handles of processes it can open). A row
// per process keeping a zombie alive (a leaking holder may have hundreds of handles to it), with the zombie's columns and
// the holder's, so sorting by the zombie groups its holders. Scanned on a worker thread when the tab is shown and then
// every ScanInterval while it's active (not on every tick: thousands of duplicates), and again after handles are closed.
// The filter box matches the zombie, the holder, their PIDs and the image path. Double-click or Go to Holder selects the
// holder; Show All Handles opens CZombieHandlesDlg on the row's zombie (the Handle Values column has only the first
// ShownHandles); Close Handles closes the row's handles in its holder and Close All Handles every handle to the row's zombie
// (after a confirmation), which frees it.
//
class CZombiesView :
	public CViewBase<CZombiesView>,
	public CVirtualListView<CZombiesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
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
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	// posted by the scanning thread when it's done
	static constexpr UINT WM_ZOMBIES_FOUND = WM_APP + 0x170;

	BEGIN_MSG_MAP(CZombiesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_ZOMBIES_FOUND, OnZombiesFound)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		CHAIN_MSG_MAP(CVirtualListView<CZombiesView>)
		CHAIN_MSG_MAP(CViewBase<CZombiesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_ZOMBIE_GOTOHOLDER, OnGoToHolder)
		COMMAND_ID_HANDLER(ID_ZOMBIE_SHOWHANDLES, OnShowHandles)
		COMMAND_ID_HANDLER(ID_ZOMBIE_CLOSEHANDLE, OnCloseHandle)
		COMMAND_ID_HANDLER(ID_ZOMBIE_CLOSEALL, OnCloseAll)
		CHAIN_MSG_MAP_ALT(CViewBase<CZombiesView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Zombie, Pid, Exited, DeadFor, ExitCode, Holder, HolderPid, Handles, Handle, Access, ImagePath, _Count };

	// a process keeping a zombie alive, with its handles to it
	struct Row {
		WinSys::ZombieProcess const* Zombie;	// into m_Zombies
		uint32_t HolderPid;
		std::vector<WinSys::ZombieHolder> Handles;
		std::wstring Key;	// the zombie (PID and creation time) and the holder
	};

	enum { ScanInterval = 5000 };	// ms
	static constexpr size_t ShownHandles = 4;	// in the Handle Values column

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnZombiesFound(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToHolder(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnShowHandles(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseHandle(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the ZombieColumns setting
	void SaveColumns() const;

	// a scan on the worker thread, unless one is running
	void StartScan();
	// closes the handles (each in its holder); the error of the first that failed, else ERROR_SUCCESS
	static DWORD CloseHandles(std::vector<WinSys::ZombieHolder> const& holders);
	std::wstring GetText(Row const& row, Column column) const;
	std::wstring GetProcessName(uint32_t pid) const;
	static std::wstring ZombieName(WinSys::ZombieProcess const& zombie);
	bool Matches(Row const& row) const;
	// the list's rows: the ones that match the filter, sorted as the list is; selected: the key of the row to select
	void ShowRows(std::wstring const& selected);
	std::wstring GetSelectedKey() const;
	Row const* GetSelected() const;
	void SelectRow(std::wstring const& key);
	void UpdateStatus();

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::vector<WinSys::ZombieProcess> m_Zombies;	// the last scan's
	std::vector<Row> m_All;	// a row per holder of m_Zombies
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	int64_t m_ScanTime{ 0 };	// FILETIME of the last scan; 0: none yet
	uint64_t m_LastScan{ 0 };	// GetTickCount64 when the last scan started
	DWORD m_ScanError{ 0 };	// the last scan's, if it failed
	bool m_Active{ false };
	// the worker and its result, which OnZombiesFound takes
	std::jthread m_Worker;
	bool m_Scanning{ false };
	std::mutex m_ResultLock;
	std::optional<std::vector<WinSys::ZombieProcess>> m_Result;
	DWORD m_ResultError{ 0 };
};
