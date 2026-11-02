#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include <WinSys/EventLog.h>
#include <VirtualListView.h>
#include <thread>
#include <mutex>
#include <unordered_set>

//
// The Events tab: the System, Application and Security logs' recent events (WinSys EventLog: the last 24 hours, at most
// 5,000 a log), newest first, and the new ones as they're written (EventLogWatcher, whose callbacks queue them for
// WM_EVENTS_NEW). Errors and critical events always; Warnings (on at first: with them the Security log's audit failures)
// and Information (off: with it the audit successes) on the toolbar, and two drop-downs: Logs (System, Application and
// Security at first; also Setup, Windows PowerShell, PowerShell, Defender, Task Scheduler and Sysmon logs, those that
// exist) and the time span (the last hour, 24 hours, 7 days or 30 days); all saved (EventsWarnings, EventsInformation,
// EventsLogs, EventsHours); a change reads the logs again and restarts the watcher. Security needs elevation (the status
// line says so without it). Columns: time, level, log, source,
// event ID, process (the logging process's name while it runs and started before the event, from the frame's snapshot),
// PID, message (its first line; Details has all of it). Errors are highlighted, warnings tinted. The filter matches
// source, message, event ID, log and process. Double-click or Details shows the whole event; Go to Process; Copy (the
// row with the whole message). Read on a thread of its own (WM_EVENTS_READ) when the tab opens and on Refresh.
//
class CEventsView :
	public CViewBase<CEventsView>,
	public CVirtualListView<CEventsView>,
	public WinSys::EventLogSink {
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
		LayoutBar();
	}
	void UpdateLayout(BOOL resizeBars = TRUE);

	// EventLogSink, on wevtapi's threads
	void OnEvent(WinSys::EventLogEntry&& entry) override;

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	static constexpr UINT WM_EVENTS_READ = WM_APP + 0x1D0;
	static constexpr UINT WM_EVENTS_NEW = WM_APP + 0x1D1;

	BEGIN_MSG_MAP(CEventsView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_EVENTS_READ, OnRead)
		MESSAGE_HANDLER(WM_EVENTS_NEW, OnNewEvents)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_RANGE_HANDLER(ID_EVENTS_WARNINGS, ID_EVENTS_REFRESH, OnBarCommand)
		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnBarDropDown)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CEventsView>)
		CHAIN_MSG_MAP(CViewBase<CEventsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_COLUMN_RESET, OnResetColumns)
		COMMAND_ID_HANDLER(ID_EVENT_DETAILS, OnDetails)
		COMMAND_ID_HANDLER(ID_EVENT_GOTOPROCESS, OnGoToProcess)
		COMMAND_RANGE_HANDLER(ID_EVENTS_WARNINGS, ID_EVENTS_REFRESH, OnBarCommand)
		COMMAND_ID_HANDLER(ID_EVENT_OPENVIEWER, OnOpenViewer)
		COMMAND_ID_HANDLER(ID_EVENT_ALERT, OnAlertLikeThis)
		CHAIN_MSG_MAP_ALT(CViewBase<CEventsView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Time, Level, Log, Source, EventId, Process, ProcessId, Message, _Count };
	enum { MaxEvents = 5000, MaxTotal = 15000 };	// MaxEvents a log, so Security's audits don't push out the others

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNewEvents(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBarCommand(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBarDropDown(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnOpenViewer(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAlertLikeThis(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDetails(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the highest level shown: 2 (errors), 3 (warnings) or 4 (information)
	uint8_t MaxLevel() const;
	std::vector<std::wstring> Channels() const;
	// the logs or the time span changed: saved, the span's button text, the watcher and the read
	void LogsChanged();
	std::wstring SpanText() const;
	// a new event the toolbar's options keep (the watcher's query keeps them already; level 0 is told by its keywords)
	bool Included(WinSys::EventLogEntry const& e) const;
	void StartRead();
	void StartWatching();
	void LayoutBar();
	void UpdateBar();
	void UpdateStatus();
	bool Matches(WinSys::EventLogEntry const& e) const;
	// the list's rows: the events that match the filter, sorted as the list is
	void ShowRows();
	std::wstring GetText(WinSys::EventLogEntry const& e, Column column) const;
	// the logging process's name, if it still runs and started before the event (its ID may be another's since)
	std::wstring ProcessName(WinSys::EventLogEntry const& e) const;
	bool IsRunning(WinSys::EventLogEntry const& e) const;
	WinSys::EventLogEntry const* GetSelected() const;
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	void SaveColumns() const;
	static std::wstring KeyOf(WinSys::EventLogEntry const& e);

	CListViewCtrl m_List;
	CFilterBox m_Filter;
	CToolBarCtrl m_Bar;
	CFont m_BarFont;
	int m_MenuColumn{ -1 };
	std::vector<WinSys::EventLogEntry> m_All;	// newest first
	std::unordered_set<std::wstring> m_Keys;	// m_All's, so a live event already read isn't added again
	std::vector<WinSys::EventLogEntry const*> m_Rows;	// into m_All
	bool m_Warnings{ true }, m_Information{ false };
	std::vector<std::wstring> m_Logs;	// the logs read and watched, in the order chosen
	int m_Hours{ 24 };
	bool m_Active{ false };
	std::vector<std::pair<std::wstring, DWORD>> m_Failed;	// the logs that couldn't be read, and why
	// the read
	std::jthread m_Worker;
	bool m_Reading{ false };
	std::mutex m_Lock;	// m_Result and m_Queue
	std::optional<std::vector<WinSys::EventLogEntry>> m_Result;
	std::vector<std::pair<std::wstring, DWORD>> m_ResultFailed;
	// the live events
	WinSys::EventLogWatcher m_Watcher;
	std::vector<WinSys::EventLogEntry> m_Queue;
	int m_NewSinceRead{ 0 };
};
