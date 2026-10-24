#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/PageFiles.h>

//
// System > Page Files: a modal, resizable dialog, read-only. The page files in use (WinSys PageFiles::Enum) with their
// size, use, peak and limits above; below, in sections drawn bold and shaded, the commit charge they back, how the page
// files are configured (WinSys PageFiles::GetConfiguration: automatic or per file, clearing at shutdown...; a change
// applies at the next boot) and the swap file. Refresh reads all again; Copy copies the list that last had the focus.
//
class CPageFilesDlg :
	public CDialogImpl<CPageFilesDlg>,
	public CDialogResize<CPageFilesDlg>,
	public CDpiDialogResize<CPageFilesDlg> {
public:
	enum { IDD = IDD_PAGEFILES };

	BEGIN_DLGRESIZE_MAP(CPageFilesDlg)
		// the page files keep their height (there are few), the details take the rest
		DLGRESIZE_CONTROL(IDC_PF_LIST, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PF_DETAILS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_PF_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PF_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PF_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CPageFilesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_PF_DETAILS, NM_CUSTOMDRAW, OnDetailsCustomDraw)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_ID_HANDLER(IDC_PF_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_PF_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CPageFilesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CPageFilesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	struct Row {
		std::wstring Name, Value;
		bool Header{ false };	// a section's title
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDetailsCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// reads the page files, the commit charge and the configuration again
	void Load();
	// a list's columns and rows, tab separated, with the column names first
	static std::wstring ListText(CListViewCtrl& list);

	CListViewCtrl m_List, m_Details;
	CFont m_BoldFont;
	std::vector<Row> m_Rows;	// the details'
	HWND m_CopyList{ nullptr };	// the list Copy copies: the one that last had the focus
};
