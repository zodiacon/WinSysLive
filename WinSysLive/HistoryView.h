#pragma once

#include "resource.h"
#include "ViewBase.h"
#include <LogView/include/LogView.h>
#include "HistoryRecorder.h"

//
// The History tab: the HistoryRecorder's lines (processes started and exited, services started, stopped, paused or
// resumed, installed and deleted), in a LogView (its toolbar filters and searches the lines, its level buttons show or
// hide the warnings). The recorder is acquired when the view is created (Recorders::AcquireHistory): with Record from
// Startup the frame's, whose lines since the app started are shown at once, else a new one whose baseline is the
// snapshot then. Each refresh appends the lines recorded since (the frame updates the recorder first). Each kind can
// be turned off in the log's context menu (the HistoryEvents setting, a mask of Event bits); the lines keep a PID, so
// a double-click or Go to Process shows the process. Clear clears the recorder too. Save Log (the context menu, or
// File > Export List) writes the lines the log's filter shows to a text file.
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

	using Event = HistoryRecorder::Event;

private:
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

	// the recorder's lines not shown yet
	void AppendNewLines();
	uint32_t GetLinePid(size_t line) const;
	void UpdateStatus();

	CLogViewCtrl m_Log;
	std::shared_ptr<HistoryRecorder> m_Recorder;
	uint64_t m_Shown{ 0 };	// the recorder's sequence number after the last line shown
	bool m_Active{ false };
	size_t m_MenuLine{ LGV_NOLINE };	// the line the context menu was opened on
};
