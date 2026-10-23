#pragma once

#include "resource.h"

//
// Options > VirusTotal: the API key (kept DPAPI-encrypted for the current user in the VirusTotalKey setting; the
// edit hides it unless Show is checked) and the requests a minute (VirusTotalRate: 4 on the free API). The link opens the
// account's API key page. On OK the caller saves the settings and hands them to VirusTotalChecker.
//
class CVirusTotalDlg : public CDialogImpl<CVirusTotalDlg> {
public:
	enum { IDD = IDD_VIRUSTOTAL };

	CVirusTotalDlg(std::wstring key, int rate) : m_Key(std::move(key)), m_Rate(rate) {}
	~CVirusTotalDlg() {
		::SecureZeroMemory(m_Key.data(), m_Key.size() * sizeof(wchar_t));
	}

	std::wstring const& GetKey() const {
		return m_Key;
	}
	int GetRate() const {
		return m_Rate;
	}

	BEGIN_MSG_MAP(CVirusTotalDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_VT_SHOWKEY, OnShowKey)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		NOTIFY_HANDLER(IDC_VT_LINK, NM_CLICK, OnLink)
		NOTIFY_HANDLER(IDC_VT_LINK, NM_RETURN, OnLink)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnShowKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnLink(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	std::wstring m_Key;
	int m_Rate;
	WCHAR m_PasswordChar{ 0 };
};

//
// The rows' VirusTotal commands, shared by the Processes view, the Modules pages (also on the property sheets' threads)
// and the Kernel Modules view; owner: the window for message boxes
//
namespace VirusTotalCommands {
	// the file's page on virustotal.com (its hash, computed here unless known); no API key needed
	void OpenReport(HWND owner, std::wstring const& path);
	// looks the hash up again (the VirusTotal column shows the result)
	void CheckAgain(HWND owner, std::wstring const& path);
	// uploads the file after a confirmation that says who gets it
	void Submit(HWND owner, std::wstring const& path);
	// the VirusTotal submenu's items, appended to a row menu (Open Report, Check Again, Submit File...)
	void AppendMenu(HMENU menu, bool enabled);
}
