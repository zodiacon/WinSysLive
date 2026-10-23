#pragma once

#include "resource.h"

//
// Options > Highlighting: how long new items (processes, threads, modules, handles, connections, drivers, objects,
// files...) stay green and exited ones (exited, closed, unloaded, removed) red, in seconds (0 to MaxSeconds; zero turns
// it off, exited items then going at once). A line below says how many updates that is at the current interval, as the
// views count updates (Highlight.h). On OK the caller saves the settings and hands them to Highlight::Set.
//
class CHighlightDlg : public CDialogImpl<CHighlightDlg> {
public:
	enum { IDD = IDD_HIGHLIGHTING };
	static constexpr int MaxSeconds = 60;

	// interval: the update interval in milliseconds
	CHighlightDlg(int newSeconds, int exitedSeconds, int interval) : m_New(newSeconds), m_Exited(exitedSeconds), m_Interval(interval) {}

	int GetNewSeconds() const {
		return m_New;
	}
	int GetExitedSeconds() const {
		return m_Exited;
	}

	BEGIN_MSG_MAP(CHighlightDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_HL_NEW, EN_CHANGE, OnChanged)
		COMMAND_HANDLER(IDC_HL_EXITED, EN_CHANGE, OnChanged)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the edit's number, 0 to MaxSeconds
	int GetSeconds(int id) const;
	void UpdateTicks();

	int m_New, m_Exited, m_Interval;
};
