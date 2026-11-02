#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/EventLog.h>
#include <functional>

//
// The Events tab's Details: a modal, resizable dialog of one event. The level's icon (the system's stock error, warning
// or information icon; the key for the Security log's audits, as the list) and the source and event ID as its title, the
// summary of its data under it (the tab's Details: the entry's single condensed data item, CEventsView::Condense); the event's
// fields as selectable read-only text in two columns (log, source, level, event ID, time, record ID, process with its
// name, thread, computer, keywords); the message in a scrolling box with its line breaks, or, with Show XML, the event's
// XML (WinSys EventLog::GetXml, by record ID), indented. Previous and Next walk the tab's rows (the view's callback
// moves its selection and returns the event, a copy: the view's events move as new ones arrive); Go to Process (while
// the logging process runs and started before the event), Copy (the fields and the message, or the XML) and Close.
//
class CEventDetailsDlg :
	public CDialogImpl<CEventDetailsDlg>,
	public CDialogResize<CEventDetailsDlg>,
	public CDpiDialogResize<CEventDetailsDlg> {
public:
	enum { IDD = IDD_EVENTDETAILS };

	struct Callbacks {
		// the event delta rows from the one with the key (the view's KeyOf), the view's selection moved to it; none at an end
		std::function<std::optional<WinSys::EventLogEntry>(WinSys::EventLogEntry const&, int)> Move;
		// the logging process's image name, if it still runs and started before the event
		std::function<std::wstring(WinSys::EventLogEntry const&)> ProcessName;
		std::function<void(uint32_t)> GoToProcess;
	};

	CEventDetailsDlg(WinSys::EventLogEntry entry, Callbacks callbacks) : m_Entry(std::move(entry)), m_Callbacks(std::move(callbacks)) {}

	BEGIN_DLGRESIZE_MAP(CEventDetailsDlg)
		DLGRESIZE_CONTROL(IDC_ED_TITLE, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_SUMMARY, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_SOURCE, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_EVENTID, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_RECORDID, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_THREAD, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_KEYWORDS, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ED_XML, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_ED_MESSAGE, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_ED_PREVIOUS, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ED_NEXT, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ED_GOTOPROCESS, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ED_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CEventDetailsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_ID_HANDLER(IDC_ED_XML, OnShowXml)
		COMMAND_ID_HANDLER(IDC_ED_PREVIOUS, OnMove)
		COMMAND_ID_HANDLER(IDC_ED_NEXT, OnMove)
		COMMAND_ID_HANDLER(IDC_ED_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(IDC_ED_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CEventDetailsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CEventDetailsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnShowXml(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnMove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the fields, the icon, the message or the XML, the buttons' states
	void ShowEntry();
	void ShowText();
	// the fields as "Name: value" lines
	std::wstring FieldsText() const;
	// the event's XML, indented (read once per event)
	std::wstring const& Xml();
	// the edits' line breaks are CRLF
	static std::wstring WithCrLf(std::wstring const& text);
	static std::wstring Indent(std::wstring const& xml);

	WinSys::EventLogEntry m_Entry;
	Callbacks m_Callbacks;
	std::optional<std::wstring> m_Xml;	// the shown event's, indented
	CFont m_TitleFont;
	CIcon m_Icon;
};
