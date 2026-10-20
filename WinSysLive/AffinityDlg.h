#pragma once

#include "resource.h"

//
// A process's processor affinity: a check box for each processor in the system's mask (one processor group: a process
// whose threads span groups has no single mask), laid out in rows of eight when the dialog opens.
//
class CAffinityDlg : public CDialogImpl<CAffinityDlg> {
public:
	enum { IDD = IDD_AFFINITY };

	// title: for the caption (the process); mask: the current affinity; systemMask: the processors there are
	CAffinityDlg(std::wstring title, DWORD_PTR mask, DWORD_PTR systemMask);

	// after IDOK
	DWORD_PTR GetMask() const;

	BEGIN_MSG_MAP(CAffinityDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_AFF_ALL, OnSelect)
		COMMAND_ID_HANDLER(IDC_AFF_NONE, OnSelect)
		COMMAND_RANGE_HANDLER(FirstCheckId, FirstCheckId + 63, OnCheck)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelect(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the check boxes' IDs: processor n is FirstCheckId + n
	enum { FirstCheckId = 2000 };

	DWORD_PTR ReadMask() const;
	// OK needs at least one processor
	void UpdateOK();

	std::wstring m_Title;
	DWORD_PTR m_Mask, m_SystemMask;
};
