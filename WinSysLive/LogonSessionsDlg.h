#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/LsaSecurity.h>

//
// System > Logon Sessions: a modal, resizable dialog with the system's logon sessions (WinSys
// LsaSecurity::EnumLogonSessions, LsaEnumerateLogonSessions) in a list, sortable by any column, and the selected one's
// details below it, in sections drawn bold and shaded. Another user's session has only its logon ID unless elevated
// (grayed, its details saying why). Refresh reads them again, keeping the selection by logon ID; Copy copies the list
// that last had the focus: every session (tab separated, with a header) or the selected one's details.
//
class CLogonSessionsDlg :
	public CDialogImpl<CLogonSessionsDlg>,
	public CDialogResize<CLogonSessionsDlg>,
	public CDpiDialogResize<CLogonSessionsDlg> {
public:
	enum { IDD = IDD_LOGON_SESSIONS };

	BEGIN_DLGRESIZE_MAP(CLogonSessionsDlg)
		// not a group: WTL's groups lay out along one axis, and with DLSZ_SIZE_X a group of two lists one above the
		// other gave the upper one back its first width
		DLGRESIZE_CONTROL(IDC_LS_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_LS_DETAILS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_LS_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_LS_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_LS_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CLogonSessionsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_LS_LIST, LVN_ITEMCHANGED, OnSessionChanged)
		NOTIFY_HANDLER(IDC_LS_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_LS_LIST, NM_CUSTOMDRAW, OnListCustomDraw)
		NOTIFY_HANDLER(IDC_LS_DETAILS, NM_CUSTOMDRAW, OnDetailsCustomDraw)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_ID_HANDLER(IDC_LS_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_LS_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CLogonSessionsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CLogonSessionsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { LogonId, User, Type, Session, Package, LogonTime, LogonServer, Sid, _Count };

	struct Row {
		std::wstring Name, Value;
		bool Header{ false };	// a section's title
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSessionChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDetailsCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// reads the sessions again, keeping the selected one
	void Load();
	void Sort();
	// the selected session's details, into the lower list
	void ShowDetails();
	WinSys::LogonSessionData const* GetSelectedSession() const;
	std::wstring GetText(WinSys::LogonSessionData const& session, Column column) const;
	static int Compare(LPARAM index1, LPARAM index2, LPARAM param);

	CListViewCtrl m_List, m_Details;
	CFont m_BoldFont;
	std::vector<WinSys::LogonSessionData> m_Sessions;	// each row's item data is its index here
	std::vector<Row> m_Rows;	// the details'
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
	HWND m_CopyList{ nullptr };	// the list Copy copies: the one that last had the focus
};
