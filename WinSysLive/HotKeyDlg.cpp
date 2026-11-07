#include "pch.h"
#include "HotKeyDlg.h"
#include "GlobalHotKey.h"
#include <WinSys/Helpers.h>

LRESULT CHotKeyDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Key.Attach(GetDlgItem(IDC_HK_KEY));
	// a key alone, or with Shift only, is taken as Ctrl+Alt and the key
	m_Key.SetRules(HKCOMB_NONE | HKCOMB_S, HOTKEYF_CONTROL | HOTKEYF_ALT);
	m_Key.SetHotKey(LOBYTE(m_HotKey), HIBYTE(m_HotKey));
	m_Key.SetFocus();
	UpdateStatus();
	CenterWindow(GetParent());
	return FALSE;	// the focus is set
}

void CHotKeyDlg::UpdateStatus() {
	auto key = (WORD)m_Key.GetHotKey();
	SetDlgItemText(IDC_HK_STATUS, key ? std::format(L"Pressing {} anywhere shows WinSysLive (or minimizes it when it's in front).",
		GlobalHotKey::Text(key)).c_str() : L"No hotkey.");
}

LRESULT CHotKeyDlg::OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_Key)
		UpdateStatus();
	return 0;
}

LRESULT CHotKeyDlg::OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Key.SetHotKey(0, 0);
	UpdateStatus();
	m_Key.SetFocus();
	return 0;
}

LRESULT CHotKeyDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		auto key = (WORD)m_Key.GetHotKey();
		std::wstring error;
		if (key && !GlobalHotKey::IsValid(key))
			error = L"A hotkey needs Ctrl or Alt.";
		else if (m_Apply && !m_Apply(key))
			error = std::format(L"{} can't be registered: {}", GlobalHotKey::Text(key),
				::GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED ? L"another program uses it." : WinSys::Helpers::GetErrorText(::GetLastError()));
		if (!error.empty()) {
			SetDlgItemText(IDC_HK_STATUS, error.c_str());
			::MessageBeep(MB_ICONWARNING);
			m_Key.SetFocus();
			return 0;
		}
		m_HotKey = key;
	}
	EndDialog(wID);
	return 0;
}
