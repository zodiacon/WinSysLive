#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/PowerRequests.h>
#include <functional>

//
// System > Power Requests: a modal, resizable dialog of what keeps the system or the display awake and what will wake
// it, as powercfg /requests and /waketimers (WinSys PowerRequests; elevation needed), read again every 2 seconds while
// it's open (the selections kept by row). The system timer's resolution on top (current, finest and coarsest: a current
// finer than the coarsest means a program asked for it, which costs power). The requests: what each holds (Display,
// System, Away mode, Execution, Performance boost, Active lock screen, with counts above 1), the requester (process,
// service with its name, or driver with its device), PID and reason; Show Inactive adds the requests created but not
// set. The wake timers below: when each expires, its period, requester and reason. Both lists sort by any column;
// double-click (or Go to Process) goes to a process's or service host's process, closing the dialog; Copy copies the
// list that last had the focus (its selected rows, or all).
//
class CPowerRequestsDlg :
	public CDialogImpl<CPowerRequestsDlg>,
	public CDialogResize<CPowerRequestsDlg>,
	public CDpiDialogResize<CPowerRequestsDlg> {
public:
	enum { IDD = IDD_POWERREQUESTS };

	// goToProcess: selects a process in the Processes tab
	explicit CPowerRequestsDlg(std::function<void(uint32_t)> goToProcess) : m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CPowerRequestsDlg)
		DLGRESIZE_CONTROL(IDC_PR_RESOLUTION, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PR_INACTIVE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PR_REQUESTS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_PR_TIMERS_LABEL, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PR_TIMERS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PR_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PR_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CPowerRequestsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_ID_HANDLER(IDC_PR_INACTIVE, OnShowInactive)
		COMMAND_ID_HANDLER(IDC_PR_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_POWER_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CPowerRequestsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CPowerRequestsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// a list's rows as text, its sort and the PID of each row (0: none)
	struct Table {
		std::vector<PCWSTR> Columns;
		std::vector<int> Widths;	// at 96 DPI
		std::vector<std::vector<std::wstring>> Rows;
		std::vector<uint32_t> Pids;
		std::vector<int64_t> SortKeys;	// a column's numeric key (the wake timers' due time), else empty
		int SortColumn{ -1 };
		bool SortAscending{ true };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnShowInactive(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Load();
	static void InitColumns(CListViewCtrl& list, Table const& table);
	// the rows in their order, keeping the selected ones by their text
	static void Fill(CListViewCtrl& list, Table& table);
	CListViewCtrl* FocusList();
	Table& TableOf(HWND list);
	// the selected row's PID in the list that last had the focus
	uint32_t SelectedPid();

	std::function<void(uint32_t)> m_GoToProcess;
	CListViewCtrl m_Requests, m_Timers;
	Table m_RequestTable, m_TimerTable;
	HWND m_FocusList{ nullptr };
	bool m_ShowInactive{ false };
	DWORD m_Error{ 0 };
};
