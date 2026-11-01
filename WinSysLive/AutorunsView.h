#pragma once

#include "resource.h"
#include "VirusTotalChecker.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include "SignatureVerifier.h"
#include <WinSys/Autoruns.h>
#include <VirtualListView.h>

//
// The Autoruns tab: what starts by itself (WinSys Autoruns: Run keys, startup folders, Active Setup, scheduled tasks,
// auto-start services and drivers, Winlogon, AppInit, BootExecute, LSA packages, image hijacks), as Sysinternals'
// Autoruns. Scanned on a thread of its own (WM_AUTORUNS_SCANNED; under a second) when the tab is first shown, when
// it's activated a minute or more after the last scan, and on Scan Again; not on refresh ticks. Signatures are verified
// as in the Kernel Modules tab (WM_AUTORUNS_SIGNED), and Hide Microsoft Entries (the AutorunsHideMicrosoft setting, on
// by default) leaves out entries whose image has a valid Microsoft signature, shown until verified. Run keys and startup
// items are enabled and disabled as Task Manager does (StartupApproved), tasks through the Task Scheduler; Run values,
// startup files, tasks and hijack values can be deleted (after a confirmation). Jump to Entry opens the registry key in
// Regedit, the file in Explorer, or the Task Scheduler. An image that isn't there is highlighted, disabled entries grayed.
//
class CAutorunsView :
	public CViewBase<CAutorunsView>,
	public CVirtualListView<CAutorunsView> {
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
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
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

	static constexpr UINT WM_AUTORUNS_SCANNED = WM_APP + 0x1B0;
	static constexpr UINT WM_AUTORUNS_SIGNED = WM_APP + 0x1B1;

	BEGIN_MSG_MAP(CAutorunsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_AUTORUNS_SCANNED, OnScanned)
		MESSAGE_HANDLER(WM_AUTORUNS_SIGNED, OnSigned)
		MESSAGE_HANDLER(VirusTotalChecker::WM_VIRUSTOTAL, OnVirusTotal)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CAutorunsView>)
		CHAIN_MSG_MAP(CViewBase<CAutorunsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_AUTORUNS_ENABLE, OnEnable)
		COMMAND_ID_HANDLER(ID_AUTORUNS_DELETE, OnDelete)
		COMMAND_ID_HANDLER(ID_AUTORUNS_JUMP, OnJump)
		COMMAND_ID_HANDLER(ID_AUTORUNS_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_AUTORUNS_OPENLOCATION, OnOpenLocation)
		COMMAND_ID_HANDLER(ID_AUTORUNS_HIDEMICROSOFT, OnHideMicrosoft)
		COMMAND_ID_HANDLER(ID_AUTORUNS_RESCAN, OnRescan)
		COMMAND_ID_HANDLER(ID_VT_REPORT, OnVirusTotalCommand)
		COMMAND_ID_HANDLER(ID_VT_CHECK, OnVirusTotalCommand)
		COMMAND_ID_HANDLER(ID_VT_SUBMIT, OnVirusTotalCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CAutorunsView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Entry, Category, Enabled, Description, Company, Version, Signature, VirusTotal, ImagePath, Command, Location,
		Details, Modified, TaskState, Triggers, LastRun, LastResult, NextRun, RunAs, Author,
		_Count
	};

	struct Row {
		WinSys::AutorunEntry Entry;
		std::wstring Key;	// category, location, name and command: an entry across scans
	};

	enum { RescanAge = 60000 };	// ms: activating the tab scans again after this long

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVirusTotal(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVirusTotalCommand(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEnable(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDelete(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnJump(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideMicrosoft(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRescan(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void StartScan();
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;

	std::wstring GetText(Row const& row, Column column) const;
	std::wstring GetSignatureText(WinSys::AutorunEntry const& entry) const;
	// a valid signature by Microsoft
	bool IsMicrosoft(WinSys::AutorunEntry const& entry) const;
	bool Matches(Row const& row) const;
	// the list's rows: the entries that match the filter and aren't hidden, sorted as the list is
	void ShowRows(std::wstring const& selected);
	std::wstring GetSelectedKey() const;
	Row const* GetSelected() const;
	Row* FindRow(std::wstring const& key);
	void SelectRow(std::wstring const& key);
	void UpdateStatus();
	// a failed change, with the elevation hint when access was denied
	void ReportError(PCWSTR action, DWORD error);
	static std::wstring KeyOf(WinSys::AutorunEntry const& entry);

	CListViewCtrl m_List;
	CFilterBox m_Filter;
	int m_MenuColumn{ -1 };
	std::vector<Row> m_All;	// the last scan's
	std::vector<Row const*> m_Rows;	// into m_All: the list's rows, in display order
	SignatureVerifier m_Signatures;
	bool m_HideMicrosoft{ true };
	bool m_Active{ false };
	// the scan
	std::jthread m_Worker;
	bool m_Scanning{ false };
	ULONGLONG m_LastScan{ 0 };	// GetTickCount64 when the last scan started; 0: never
	int64_t m_ScanTime{ 0 };	// FILETIME of the last scan's result
	std::mutex m_ResultLock;
	std::optional<std::vector<WinSys::AutorunEntry>> m_Result;
};
