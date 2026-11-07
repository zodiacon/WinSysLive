#pragma once

#include "resource.h"
#include <functional>

//
// Options > Global Hotkey...: the key that shows the main window from anywhere (GlobalHotKey.h), in a hotkey control;
// Backspace or Clear empties it (none). OK hands the key to apply, the frame's registration: a key another program has
// already taken (or one without Ctrl or Alt) is refused in the dialog's status line, which stays open.
//
class CHotKeyDlg : public CDialogImpl<CHotKeyDlg> {
public:
	enum { IDD = IDD_HOTKEY };

	// apply: registers the key (0: none), false with the last error set when it can't
	CHotKeyDlg(WORD hotKey, std::function<bool(WORD)> apply) : m_HotKey(hotKey), m_Apply(std::move(apply)) {}

	WORD GetHotKey() const {
		return m_HotKey;
	}

	BEGIN_MSG_MAP(CHotKeyDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_HK_KEY, EN_CHANGE, OnChanged)
		COMMAND_ID_HANDLER(IDC_HK_CLEAR, OnClear)
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
	LRESULT OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void UpdateStatus();

	CHotKeyCtrl m_Key;
	WORD m_HotKey;
	std::function<bool(WORD)> m_Apply;
};
