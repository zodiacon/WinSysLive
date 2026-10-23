#include "pch.h"
#include "VirusTotalDlg.h"
#include "VirusTotalChecker.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>

LRESULT CVirusTotalDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CEdit edit(GetDlgItem(IDC_VT_KEY));
	m_PasswordChar = edit.GetPasswordChar();
	edit.SetWindowText(m_Key.c_str());
	edit.SetLimitText(256);
	SetDlgItemInt(IDC_VT_RATE, m_Rate, FALSE);
	CenterWindow(GetParent());
	return TRUE;
}

LRESULT CVirusTotalDlg::OnShowKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CEdit edit(GetDlgItem(IDC_VT_KEY));
	edit.SetPasswordChar(IsDlgButtonChecked(IDC_VT_SHOWKEY) ? 0 : m_PasswordChar);
	edit.Invalidate();
	return 0;
}

LRESULT CVirusTotalDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK) {
		CString key;
		GetDlgItemText(IDC_VT_KEY, key);
		key.Trim();
		BOOL ok;
		int rate = (int)GetDlgItemInt(IDC_VT_RATE, &ok, FALSE);
		if (!ok || rate < 1 || rate > 1000) {
			AtlMessageBox(m_hWnd, L"The requests per minute go from 1 to 1000 (4 on the free API).", IDR_MAINFRAME, MB_ICONWARNING);
			GotoDlgCtrl(GetDlgItem(IDC_VT_RATE));
			return 0;
		}
		::SecureZeroMemory(m_Key.data(), m_Key.size() * sizeof(wchar_t));
		m_Key = (PCWSTR)key;
		m_Rate = rate;
	}
	EndDialog(wID);
	return 0;
}

LRESULT CVirusTotalDlg::OnLink(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto link = reinterpret_cast<NMLINK*>(hdr);
	::ShellExecute(m_hWnd, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

namespace VirusTotalCommands {
	bool RequireKey(HWND owner) {
		if (VirusTotalChecker::Get().HasApiKey())
			return true;
		AtlMessageBox(owner, L"Checking files on VirusTotal needs an API key: set one in Options > VirusTotal...\n\n"
			L"Open Report works without one.", IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}

	void OpenReport(HWND owner, std::wstring const& path) {
		std::wstring sha256;
		if (auto known = VirusTotalChecker::Get().Peek(path); known && !known->Sha256.empty())
			sha256 = known->Sha256;
		else {
			CWaitCursor wait;
			auto hash = WinSys::FileHash::Sha256(path);
			if (!hash) {
				AtlMessageBox(owner, std::format(L"Can't read {}: {}", path, WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
					IDR_MAINFRAME, MB_ICONERROR);
				return;
			}
			sha256 = *hash;
		}
		::ShellExecute(owner, L"open", WinSys::VirusTotal::ReportUrl(sha256).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	void CheckAgain(HWND owner, std::wstring const& path) {
		if (RequireKey(owner))
			VirusTotalChecker::Get().Recheck(path);
	}

	void Submit(HWND owner, std::wstring const& path) {
		if (!RequireKey(owner))
			return;
		WIN32_FILE_ATTRIBUTE_DATA data;
		std::wstring size;
		if (::GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data))
			size = L" (" + FormatHelper::FormatSize((int64_t)ULARGE_INTEGER{ data.nFileSizeLow, data.nFileSizeHigh }.QuadPart) + L")";
		auto text = std::format(L"Upload {}{} to VirusTotal?\n\nThe file is sent to VirusTotal, which shares it with its partners "
			L"and makes it available to its premium users. Don't submit files that hold private data.\n\n"
			L"The VirusTotal column follows the analysis, which takes a few minutes.", path, size);
		if (AtlMessageBox(owner, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK)
			VirusTotalChecker::Get().Submit(path);
	}

	void AppendMenu(HMENU menu, bool enabled) {
		CMenu sub;
		sub.CreatePopupMenu();
		UINT flags = MF_STRING | (enabled ? 0 : MF_GRAYED);
		sub.AppendMenu(flags, ID_VT_REPORT, L"Open &Report");
		sub.AppendMenu(flags, ID_VT_CHECK, L"&Check Again");
		sub.AppendMenu(flags, ID_VT_SUBMIT, L"&Submit File...");
		::AppendMenu(menu, MF_POPUP | (enabled ? 0 : MF_GRAYED), (UINT_PTR)sub.Detach(), L"&VirusTotal");
	}
}
