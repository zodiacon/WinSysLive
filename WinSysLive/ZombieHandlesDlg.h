#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "Interfaces.h"
#include <WinSys/Zombies.h>

//
// Show All Handles in the Zombie Processes tab's row menu: a modal, resizable dialog with every handle keeping a zombie
// alive, in all its holders (the row's holder's selected), with the zombie's details above them. A row per handle: the
// holder (with its icon, from the frame's image list: the dialog runs on the UI thread), the handle's value, its access
// as a number and decoded (WinSys AccessMaskDecoder, a Process's rights), its attributes (Inherit, Protect from close)
// and what closing it did. Close Selected and Close All close handles in their holders after a confirmation; a closed
// handle stays listed, marked, and a failure shows its error in the row. The view scans again afterwards if anything was
// closed (Closed). Copy copies the selected rows, or all of them, tab separated.
//
class CZombieHandlesDlg :
	public CDialogImpl<CZombieHandlesDlg>,
	public CDialogResize<CZombieHandlesDlg>,
	public CDpiDialogResize<CZombieHandlesDlg> {
public:
	enum { IDD = IDD_ZOMBIE_HANDLES };

	// selectHolder: the process whose handles are selected at first
	CZombieHandlesDlg(IMainFrame* frame, WinSys::ZombieProcess zombie, uint32_t selectHolder);

	// whether any handle was closed, which changes what the view shows
	bool Closed() const {
		return m_Closed;
	}

	// closes one handle in its holder, only if the value still refers to a process; ERROR_SUCCESS or the error
	static DWORD CloseHandle(WinSys::ZombieHolder const& holder);

	BEGIN_DLGRESIZE_MAP(CZombieHandlesDlg)
		DLGRESIZE_CONTROL(IDC_ZH_INFO, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_ZH_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_ZH_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ZH_CLOSE, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ZH_CLOSEALL, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ZH_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CZombieHandlesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_ZH_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_ZH_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(IDC_ZH_CLOSE, OnCloseSelected)
		COMMAND_ID_HANDLER(IDC_ZH_CLOSEALL, OnCloseAll)
		COMMAND_ID_HANDLER(IDC_ZH_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CZombieHandlesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CZombieHandlesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Holder, HolderPid, Handle, Access, Rights, Attributes, Status, _Count };

	struct Row {
		WinSys::ZombieHolder Handle;
		std::wstring HolderName;
		bool Closed{ false };
		DWORD Error{ ERROR_SUCCESS };	// the last close's
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCloseSelected(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::wstring GetText(Row const& row, Column column) const;
	// the rows' texts again, after closing
	void UpdateRows();
	void UpdateButtons();
	void UpdateStatus();
	// the open handles among these rows (indices), after a confirmation that lists them
	void CloseRows(std::vector<int> const& rows);
	std::vector<int> GetSelectedRows() const;
	std::wstring ZombieName() const;

	IMainFrame* m_Frame;
	WinSys::ZombieProcess m_Zombie;
	uint32_t m_SelectHolder;
	std::vector<Row> m_Rows;	// in the list's order
	CListViewCtrl m_List;
	bool m_Closed{ false };
};
