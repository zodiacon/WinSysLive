#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/ProcessMemory.h>
#include <functional>
#include <thread>
#include <mutex>

//
// The Memory page's strings: the options and the last scan's results, kept by the page so the dialog shows them
// again when it's opened again
//
struct MemoryStrings {
	WinSys::StringScanOptions Options{ 5, true, true, 500000 };	// a minimum length of 5; at most 500,000 strings
	bool WholeSpace{ false };	// else the selected region
	std::vector<WinSys::MemoryString> Results;
	bool Truncated{ false };	// the result limit was reached, or the scan was stopped
	std::wstring Scope;			// what was scanned, for the status line
	std::wstring Filter;
};

//
// Strings... on the Memory page: the runs of text in the selected region or the whole address space (WinSys
// ProcessMemory::FindStrings: ASCII and UTF-16, at least the minimum length), as Sysinternals' Strings, scanned on a
// thread of its own with the progress in the status line; Scan becomes Stop meanwhile, and closing stops it. The list
// (owner data, as there can be 500,000 rows) has each string's address, kind, length and region (the page's Type and
// Details), sorts by any column and is filtered by the text or the address. Go To (also a double-click or Enter) closes
// the dialog and shows the string's bytes in the hex view; Copy copies the selected rows (or all shown), Save writes
// the shown rows to a text file.
//
class CMemoryStringsDlg :
	public CDialogImpl<CMemoryStringsDlg>,
	public CDialogResize<CMemoryStringsDlg>,
	public CDpiDialogResize<CMemoryStringsDlg> {
public:
	enum { IDD = IDD_MEMORY_STRINGS };

	static constexpr UINT WM_STRINGS_PROGRESS = WM_APP + 0x1D0;	// wParam: percent
	static constexpr UINT WM_STRINGS_DONE = WM_APP + 0x1D1;

	// region: the selected region's range, if there is one; describe names the region holding an address
	CMemoryStringsDlg(MemoryStrings& state, HANDLE hProcess, std::optional<std::pair<uint64_t, uint64_t>> region,
		std::function<std::wstring(uint64_t)> describe)
		: m_State(state), m_hProcess(hProcess), m_Region(region), m_Describe(std::move(describe)) {}

	// after IDOK: the string to show
	uint64_t GetAddress() const {
		return m_GoToAddress;
	}
	size_t GetByteLength() const {
		return m_GoToLength;
	}

	BEGIN_DLGRESIZE_MAP(CMemoryStringsDlg)
		DLGRESIZE_CONTROL(IDC_MSTR_SCAN, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_MSTR_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_MSTR_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_MSTR_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_MSTR_GOTO, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_MSTR_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_MSTR_SAVE, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CMemoryStringsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_STRINGS_PROGRESS, OnProgress)
		MESSAGE_HANDLER(WM_STRINGS_DONE, OnDone)
		COMMAND_HANDLER(IDC_MSTR_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_MSTR_LIST, LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_HANDLER(IDC_MSTR_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_MSTR_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_HANDLER(IDC_MSTR_LIST, LVN_ITEMCHANGED, OnItemChanged)
		COMMAND_ID_HANDLER(IDC_MSTR_SCAN, OnScan)
		COMMAND_ID_HANDLER(IDC_MSTR_GOTO, OnGoTo)
		COMMAND_ID_HANDLER(IDOK, OnGoTo)
		COMMAND_ID_HANDLER(IDC_MSTR_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_MSTR_SAVE, OnSave)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CMemoryStringsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CMemoryStringsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Address, Kind, Length, Region, String, _Count };

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDone(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnScan(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the options from the controls into m_State; false (with a message) if they make no scan
	bool ReadOptions();
	void StartScan();
	void StopScan();
	// m_Shown: the results the filter matches, sorted
	void ShowRows();
	void UpdateStatus();
	void EnableControls();
	std::wstring GetText(WinSys::MemoryString const& s, Column column) const;
	// the shown rows (the selected ones, if any), tab separated, with the column names first
	std::wstring RowsText(bool selectedOnly) const;

	MemoryStrings& m_State;
	HANDLE m_hProcess;
	std::optional<std::pair<uint64_t, uint64_t>> m_Region;
	std::function<std::wstring(uint64_t)> m_Describe;
	CListViewCtrl m_List;
	std::vector<int> m_Shown;	// indices into m_State.Results, in display order
	mutable std::wstring m_Text;	// LVN_GETDISPINFO's
	int m_SortColumn{ (int)Column::Address };
	bool m_SortAscending{ true };
	uint64_t m_GoToAddress{ 0 };
	size_t m_GoToLength{ 0 };
	// the scan, joined when the dialog goes (it stops within a chunk)
	std::jthread m_Worker;
	bool m_Scanning{ false };
	int m_Percent{ 0 };
	std::mutex m_ResultLock;
	std::vector<WinSys::MemoryString> m_Result;
	bool m_ResultTruncated{ false };
	bool m_Stopped{ false };	// Stop was clicked
};
