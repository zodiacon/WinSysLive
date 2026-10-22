#include "pch.h"
#include "SymbolSettingsDlg.h"
#include "AppSettings.h"
#include <WTLHelper.h>

SymbolPathOptions CSymbolSettingsDlg::LoadOptions() {
	auto& s = AppSettings::Get();
	SymbolPathOptions options;
	options.UseEnvironment = s.SymbolUseEnvPath();
	options.UseServer = s.SymbolServerEnabled();
	options.ServerUrl = s.SymbolServerUrl();
	options.Cache = s.SymbolCache();
	options.ExtraPaths = s.SymbolExtraPaths();
	return options;
}

LRESULT CSymbolSettingsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto env = GetEnvironmentSymbolPath();
	SetDlgItemText(IDC_SYM_ENV_VALUE, env.empty() ? L"(not set)" : env.c_str());
	// an empty cache is TotalPE's
	CEdit(GetDlgItem(IDC_SYM_CACHE)).SetCueBannerText(TotalPESymbolCache().c_str(), TRUE);
	Show(LoadOptions());
	CenterWindow(GetParent());
	return TRUE;
}

void CSymbolSettingsDlg::Show(SymbolPathOptions const& options) {
	CheckDlgButton(IDC_SYM_USE_ENV, options.UseEnvironment ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SYM_USE_SERVER, options.UseServer ? BST_CHECKED : BST_UNCHECKED);
	SetDlgItemText(IDC_SYM_SERVER_URL, options.ServerUrl.c_str());
	SetDlgItemText(IDC_SYM_CACHE, options.Cache.c_str());
	SetDlgItemText(IDC_SYM_EXTRA, options.ExtraPaths.c_str());
	UpdateUI();
}

std::wstring CSymbolSettingsDlg::GetText(UINT id) const {
	CString text;
	GetDlgItemText(id, text);
	return (PCWSTR)text.Trim();
}

SymbolPathOptions CSymbolSettingsDlg::GetOptions() const {
	SymbolPathOptions options;
	options.UseEnvironment = IsDlgButtonChecked(IDC_SYM_USE_ENV) == BST_CHECKED;
	options.UseServer = IsDlgButtonChecked(IDC_SYM_USE_SERVER) == BST_CHECKED;
	options.ServerUrl = GetText(IDC_SYM_SERVER_URL);
	options.Cache = GetText(IDC_SYM_CACHE);
	options.ExtraPaths = GetText(IDC_SYM_EXTRA);
	return options;
}

void CSymbolSettingsDlg::UpdateUI() {
	bool server = IsDlgButtonChecked(IDC_SYM_USE_SERVER) == BST_CHECKED;
	GetDlgItem(IDC_SYM_SERVER_URL).EnableWindow(server);
	auto path = BuildSymbolSearchPath(GetOptions());
	SetDlgItemText(IDC_SYM_PREVIEW, path.empty() ? L"(none: only beside the images)" : path.c_str());
}

LRESULT CSymbolSettingsDlg::OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	UpdateUI();
	return 0;
}

LRESULT CSymbolSettingsDlg::OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SuspendResumeHook sr;
	CShellFileOpenDialog dlg(nullptr, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
	if (dlg.DoModal(m_hWnd) == IDOK) {
		CString path;
		if (SUCCEEDED(dlg.GetFilePath(path)))
			SetDlgItemText(IDC_SYM_CACHE, path);
	}
	return 0;
}

LRESULT CSymbolSettingsDlg::OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Show({});
	return 0;
}

LRESULT CSymbolSettingsDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto options = GetOptions();
	if (options.UseServer) {
		auto& url = options.ServerUrl;
		if (::_wcsnicmp(url.c_str(), L"http://", 7) != 0 && ::_wcsnicmp(url.c_str(), L"https://", 8) != 0) {
			AtlMessageBox(m_hWnd, L"The symbol server URL must start with http:// or https://", IDR_MAINFRAME, MB_ICONWARNING);
			GetDlgItem(IDC_SYM_SERVER_URL).SetFocus();
			return 0;
		}
	}
	auto& s = AppSettings::Get();
	s.SymbolUseEnvPath(options.UseEnvironment);
	s.SymbolServerEnabled(options.UseServer);
	s.SymbolServerUrl(options.ServerUrl);
	s.SymbolCache(options.Cache);
	s.SymbolExtraPaths(options.ExtraPaths);
	s.Save();
	EndDialog(wID);
	return 0;
}

LRESULT CSymbolSettingsDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
