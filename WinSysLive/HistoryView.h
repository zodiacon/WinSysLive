#pragma once

#include "resource.h"
#include "ViewBase.h"
#include <LogView/include/LogView.h>
#include <WinSys/Processes.h>
#include <WinSys/ServiceInfo.h>
#include <WinSys/Keys.h>
#include <unordered_set>

//
// The History tab: a log of what happened while it's open, in a LogView (its toolbar filters and searches the lines,
// its level buttons show or hide the warnings): processes started and exited (from the frame's snapshot: a start at
// the process's creation time, with its parent, user and command line; an exit with how long it ran and its exit
// code, through a handle opened when the process was first seen), and services started, stopped (a failure's exit
// code as a warning), paused or resumed, installed and deleted (enumerated on every refresh, compared by name;
// pending states are waited out). What's there when the tab opens is the baseline, not events. Each kind can be
// turned off in the log's context menu (the HistoryEvents setting, a mask of Event bits); the lines keep a PID, so
// a double-click or Go to Process shows the process. At most LineLimit lines are kept, the oldest dropped. Save Log
// (the context menu, or File > Export List) writes the lines the log's filter shows to a text file.
//
class CHistoryView : public CViewBase<CHistoryView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	bool CanCopy() const override;
	bool CanExport() const override;

	BEGIN_MSG_MAP(CHistoryView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(LGVN_CONTEXTMENU, OnLogContextMenu)
		NOTIFY_CODE_HANDLER(LGVN_ACTIVATE, OnLogActivate)
		CHAIN_MSG_MAP(CViewBase<CHistoryView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		// File > Export List: the log, as text
		COMMAND_ID_HANDLER(ID_FILE_EXPORT, OnSaveLog)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_HISTORY_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_HISTORY_SELECTALL, OnSelectAll)
		COMMAND_ID_HANDLER(ID_HISTORY_CLEAR, OnClear)
		COMMAND_RANGE_HANDLER(ID_HISTORY_EVENT_FIRST, ID_HISTORY_EVENT_LAST, OnToggleEvent)
		CHAIN_MSG_MAP_ALT(CViewBase<CHistoryView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	// the kinds of events, each a bit of the HistoryEvents setting (and a menu command, ID_HISTORY_EVENT_FIRST + kind)
	enum class Event {
		ProcessStarted, ProcessExited, ServiceStarted, ServiceStopped, ServicePaused, ServiceInstalled,
		_Count
	};

private:
	static constexpr size_t LineLimit = 100000;

	struct Line {
		int64_t Time;	// FILETIME, for the order within a refresh
		Event Kind;
		bool Warning;
		uint32_t Pid;	// 0: none
		std::wstring Text;
		PCWSTR Label;	// "Process started", "Service deleted"...
	};
	struct KnownService {
		WinSys::ServiceState State;
		uint32_t Pid;
		std::wstring DisplayName;
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLogContextMenu(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnLogActivate(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the lines the log's filter shows (all of them without one) to a UTF-8 text file
	LRESULT OnSaveLog(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleEvent(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	bool IsEnabled(Event kind) const;
	static PCWSTR EventName(Event kind);
	// the processes and services as they are now, without events
	void TakeBaseline();
	void CollectProcesses(std::vector<Line>& lines);
	void CollectServices(std::vector<Line>& lines);
	// "2026-10-23 21:30:05.123  Process started    <text>"
	static std::wstring Format(int64_t time, PCWSTR label, std::wstring const& text);
	static int64_t Now();
	uint32_t GetLinePid(size_t line) const;
	void UpdateStatus();

	CLogViewCtrl m_Log;
	uint32_t m_Events{ 0 };	// the enabled kinds, 1 << Event
	int64_t m_Since{ 0 };	// FILETIME: when the tab started tracking
	bool m_Active{ false };
	size_t m_MenuLine{ LGV_NOLINE };	// the line the context menu was opened on
	// the processes there when the tab opened: their first appearance as new isn't a start
	std::unordered_set<WinSys::ProcessOrThreadKey> m_BaselineProcesses;
	// each process seen, opened when it was, for its exit code once it's gone
	std::unordered_map<WinSys::ProcessOrThreadKey, std::unique_ptr<WinSys::Process>> m_Processes;
	std::unordered_map<std::wstring, KnownService> m_Services;	// by name
};
