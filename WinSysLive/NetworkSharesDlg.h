#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/NetworkShares.h>

//
// System > Network Shares: a modal, resizable dialog, read-only. The local shares (WinSys NetworkShares::Enum) in a
// list, by name, and the selected one's details below it in sections drawn bold and shaded: the share (path, type,
// comment, uses), its settings (offline caching, access-based enumeration, encryption) and its share permissions. When
// only NetShareEnum's level 1 is allowed (the paths, uses and permissions documented as needing elevation), the status
// line says so. Double-click opens a disk share's folder;
// Refresh reads again, keeping the selection by name; Copy copies the list that last had the focus.
//
class CNetworkSharesDlg :
	public CDialogImpl<CNetworkSharesDlg>,
	public CDialogResize<CNetworkSharesDlg>,
	public CDpiDialogResize<CNetworkSharesDlg> {
public:
	enum { IDD = IDD_SHARES };

	BEGIN_DLGRESIZE_MAP(CNetworkSharesDlg)
		DLGRESIZE_CONTROL(IDC_SH_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_SH_DETAILS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SH_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SH_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SH_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CNetworkSharesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_SH_LIST, LVN_ITEMCHANGED, OnShareChanged)
		NOTIFY_HANDLER(IDC_SH_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_HANDLER(IDC_SH_DETAILS, NM_CUSTOMDRAW, OnDetailsCustomDraw)
		NOTIFY_CODE_HANDLER(NM_SETFOCUS, OnListFocus)
		COMMAND_ID_HANDLER(IDC_SH_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_SH_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CNetworkSharesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CNetworkSharesDlg>)
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
	LRESULT OnShareChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDetailsCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnListFocus(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// reads the shares again, keeping the selected one
	void Load();
	void ShowDetails();
	WinSys::NetworkShare const* GetSelectedShare() const;

	CListViewCtrl m_List, m_Details;
	CFont m_BoldFont;
	std::vector<WinSys::NetworkShare> m_Shares;	// by name; each row's item data is its index here
	uint32_t m_Level{ 0 };	// the NetShareEnum level they came at
	std::vector<Row> m_Rows;	// the details'
	HWND m_CopyList{ nullptr };	// the list Copy copies: the one that last had the focus
};
