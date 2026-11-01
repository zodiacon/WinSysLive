#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "Snapshot.h"
#include <mutex>
#include <thread>

//
// File > Snapshots: a modal, resizable dialog to save the system's state to a file and compare two states (Snapshot,
// SnapshotCompare). Before is a snapshot file or none; After a file or the system now (the default). Compare lists what
// After has that Before hadn't (green), what's gone (red) and what changed (amber); with no Before, After's contents.
// Save Snapshot captures the system to a file and makes it Before, ready for a later Compare against the system.
// Capturing takes seconds (every process's modules, the autoruns), so it runs on a thread of its own
// (WM_SNAPSHOT_CAPTURED), with the buttons disabled; closing doesn't wait for it, as the thread shares only a state of
// its own. The list sorts by any column, the filter matches every column, Copy copies the selected rows (or all shown)
// and Open File Location (also a double-click) shows a row's file.
//
class CSnapshotDlg :
	public CDialogImpl<CSnapshotDlg>,
	public CDialogResize<CSnapshotDlg>,
	public CDpiDialogResize<CSnapshotDlg> {
public:
	enum { IDD = IDD_SNAPSHOTS };

	static constexpr UINT WM_SNAPSHOT_CAPTURED = WM_APP + 0x1C0;

	BEGIN_DLGRESIZE_MAP(CSnapshotDlg)
		DLGRESIZE_CONTROL(IDC_SN_BEFORE, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_SN_BEFORE_OPEN, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SN_BEFORE_CLEAR, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SN_AFTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_SN_AFTER_OPEN, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SN_AFTER_NOW, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SN_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_SN_COMPARE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SN_SAVE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_SN_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_SN_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SN_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CSnapshotDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SNAPSHOT_CAPTURED, OnCaptured)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_SN_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_SN_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_SN_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_SN_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_ID_HANDLER(IDC_SN_BEFORE_OPEN, OnOpen)
		COMMAND_ID_HANDLER(IDC_SN_AFTER_OPEN, OnOpen)
		COMMAND_ID_HANDLER(IDC_SN_BEFORE_CLEAR, OnClearBefore)
		COMMAND_ID_HANDLER(IDC_SN_AFTER_NOW, OnAfterNow)
		COMMAND_ID_HANDLER(IDC_SN_COMPARE, OnCompare)
		COMMAND_ID_HANDLER(IDC_SN_SAVE, OnSave)
		COMMAND_ID_HANDLER(IDC_SN_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_SNAPSHOT_OPENLOCATION, OnOpenLocation)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CSnapshotDlg>)
		CHAIN_MSG_MAP(CDialogResize<CSnapshotDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Kind, Change, Name, Details, Path, _Count };
	// what a capture is for
	enum class Purpose { Compare, Save };

	// what the capturing thread shares with the dialog, which may close first
	struct CaptureState {
		std::mutex Lock;
		std::optional<Snapshot> Result;
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCaptured(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnOpen(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClearBefore(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAfterNow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCompare(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void StartCapture(Purpose purpose);
	void EnableButtons();
	// Compare with the snapshots in hand (After captured already when it's the system)
	void ShowComparison();
	// the list's rows: m_Rows that the filter matches, sorted
	void ShowRows();
	void UpdateFields();
	std::wstring GetText(SnapshotRow const& row, Column column) const;
	// "ZODIAC, 10/31/2026 9:15 PM"
	static std::wstring Describe(Snapshot const& s);
	static int CALLBACK Compare(LPARAM index1, LPARAM index2, LPARAM param);

	CListViewCtrl m_List;
	std::optional<Snapshot> m_Before, m_After;	// m_After: a file's, or the last capture of the system
	std::wstring m_BeforePath, m_AfterPath;	// empty: none / the system
	std::vector<SnapshotRow> m_Rows;	// each list row's item data is its index here
	bool m_Comparison{ false };	// m_Rows compare two snapshots (else they're one's contents)
	std::wstring m_Filter;	// lowercase
	int m_SortColumn{ (int)Column::Kind };
	bool m_SortAscending{ true };
	// the capture
	std::shared_ptr<CaptureState> m_Capture;
	bool m_Capturing{ false };
	Purpose m_Purpose{ Purpose::Compare };
	std::wstring m_SavePath;	// for Purpose::Save
};
