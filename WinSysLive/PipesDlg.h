#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Pipes.h>

//
// System > Pipes and Mailslots: a modal, resizable dialog, read-only. Every named pipe and mailslot (WinSys
// Pipes::EnumPipes and EnumMailslots, their file systems' directories: nothing is opened, as opening a pipe would
// connect to it) in a list, sortable by any column, the pipes' current and maximum instances; the Kind column has
// pipe.ico or mailbox.ico. The filter matches the
// name (and the kind); Refresh reads again, keeping the filter and the sort; Copy copies the selected rows, or every
// row shown when none is.
//
class CPipesDlg :
	public CDialogImpl<CPipesDlg>,
	public CDialogResize<CPipesDlg>,
	public CDpiDialogResize<CPipesDlg> {
public:
	enum { IDD = IDD_PIPES };

	BEGIN_DLGRESIZE_MAP(CPipesDlg)
		DLGRESIZE_CONTROL(IDC_PM_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PM_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_PM_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PM_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PM_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CPipesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_PM_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_PM_LIST, LVN_COLUMNCLICK, OnColumnClick)
		COMMAND_ID_HANDLER(IDC_PM_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_PM_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CPipesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CPipesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Kind, Name, Instances, MaxInstances, Created, _Count };

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// reads both lists again
	void Load();
	// the list's rows: the entries the filter matches, sorted
	void ShowRows();
	std::wstring GetText(WinSys::PipeEntry const& e, Column column) const;
	static int CALLBACK Compare(LPARAM index1, LPARAM index2, LPARAM param);

	CListViewCtrl m_List;
	std::vector<WinSys::PipeEntry> m_Entries;	// each row's item data is its index here
	std::wstring m_Filter;	// lowercase
	DWORD m_PipesError{ 0 }, m_MailslotsError{ 0 };	// why a list couldn't be read
	int m_SortColumn{ (int)Column::Name };
	bool m_SortAscending{ true };
};
