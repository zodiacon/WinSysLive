#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/EtwSessions.h>
#include <functional>

//
// System > ETW Sessions: a modal, resizable dialog. The running ETW sessions (WinSys EtwSessions::Enum:
// name, mode, providers, buffers, events and buffers lost, log file) above the providers enabled in the selected one
// (name, or its GUID when TDH doesn't know it; level, keywords and the processes whose instances it enabled, named
// from the frame's snapshot). The filter keeps the sessions whose name, or one of whose providers' names or GUIDs,
// contains the text, and their matching providers (all of them when the session's name matched). Both lists sort by
// any column (numbers as numbers); Refresh reads again, keeping the selected session by name; Copy copies the list that
// last had the focus (its selected rows, or all shown). The kernel's loggers take events by flags and list no
// providers; most sessions need administrator rights to be seen (the status line says so when none could be).
// Stop Session (the button, or the session list's context menu) stops the selected session after a confirmation, which
// warns more for a session that isn't WinSysLive's (Windows and other programs rely on theirs). A "WinSysLive ..." session
// whose process (the number at its name's end) is gone, or is no longer WinSysLive, is orphaned (said in its name
// column): left by a WinSysLive that crashed or was killed, it holds one of the system's few sessions. Stop Orphaned
// stops them all (after a confirmation); it's enabled only when there are some.
//
class CEtwSessionsDlg :
	public CDialogImpl<CEtwSessionsDlg>,
	public CDialogResize<CEtwSessionsDlg>,
	public CDpiDialogResize<CEtwSessionsDlg> {
public:
	enum { IDD = IDD_ETWSESSIONS };

	// processName: a running process's image name by ID
	explicit CEtwSessionsDlg(std::function<std::wstring(uint32_t)> processName) : m_ProcessName(std::move(processName)) {}

	BEGIN_DLGRESIZE_MAP(CEtwSessionsDlg)
		// not a group: WTL's groups lay out along one axis (see the Logon Sessions dialog); the providers keep their height
		DLGRESIZE_CONTROL(IDC_ET_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ET_SESSIONS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_ET_PROVIDERS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ET_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ET_STOP, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ET_STOPORPHANED, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ET_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ET_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CEtwSessionsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_HANDLER(IDC_ET_SESSIONS, LVN_ITEMCHANGED, OnSessionChanged)
		NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_HANDLER(IDC_ET_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_ET_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_ET_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_ET_STOP, OnStop)
		COMMAND_ID_HANDLER(IDC_ET_STOPORPHANED, OnStopOrphaned)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CEtwSessionsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CEtwSessionsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// a list's columns, rows and sorting
	struct Table {
		struct Column {
			PCWSTR Name;
			int Width;	// at 96 DPI
			bool Numeric{ false };
		};
		std::vector<Column> Columns;
		std::vector<std::vector<std::wstring>> Rows;	// cells
		std::vector<int> Shown;	// row indices in display order; the list's item data
		int SortColumn{ 0 };
		bool SortAscending{ true };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnStopOrphaned(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSessionChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Load();
	// the sessions the filter keeps, sorted, keeping the selection by name
	void ShowSessions();
	// the selected session's providers (those the filter keeps)
	void ShowProviders();
	static void InitColumns(CListViewCtrl& list, Table const& table);
	static void Fill(CListViewCtrl& list, Table& table);
	static void Sort(Table& table);
	bool ProviderMatches(WinSys::EtwSessionProvider const& p) const;
	WinSys::EtwSession const* GetSelectedSession() const;
	std::wstring ProviderText(WinSys::EtwSessionProvider const& p) const;
	void UpdateStatus();

	std::function<std::wstring(uint32_t)> m_ProcessName;
	CListViewCtrl m_Sessions, m_Providers;
	Table m_SessionTable, m_ProviderTable;
	std::vector<WinSys::EtwSession> m_All;	// the session table's rows are its indices, in order
	std::vector<bool> m_Orphaned;	// by m_All's index
	std::wstring m_Result;	// the last stop's outcome, for the status line
	DWORD m_Error{ 0 };	// why nothing could be read
	std::wstring m_Filter;	// lowercase
	HWND m_CopyList{ nullptr };
	bool m_FillingSessions{ false };	// the selection changes while the list is filled aren't the user's
};
