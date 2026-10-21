#include "pch.h"
#include "RunDlg.h"
#include "AppSettings.h"
#include "ViewBase.h"
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>

namespace {
	const size_t MaxHistory = 20;

	std::wstring GetText(CWindow win) {
		CString text;
		win.GetWindowText(text);
		text.Trim();
		return std::wstring(text);
	}

	// the RunHistory setting: the command lines, newest first, one a line
	std::vector<std::wstring> LoadHistory() {
		std::vector<std::wstring> history;
		std::wstring text = AppSettings::Get().RunHistory();
		for (size_t start = 0; start < text.size(); ) {
			auto end = text.find(L'\n', start);
			if (end == std::wstring::npos)
				end = text.size();
			if (end > start)
				history.push_back(text.substr(start, end - start));
			start = end + 1;
		}
		return history;
	}

	void FillHistory(CComboBox combo) {
		for (auto& command : LoadHistory())
			combo.AddString(command.c_str());
		if (combo.GetCount() > 0)
			combo.SetCurSel(0);
	}

	// first, once (ignoring case), the oldest past MaxHistory dropped
	void AddToHistory(std::wstring const& command) {
		auto history = LoadHistory();
		std::erase_if(history, [&](auto& c) { return ::_wcsicmp(c.c_str(), command.c_str()) == 0; });
		history.insert(history.begin(), command);
		if (history.size() > MaxHistory)
			history.resize(MaxHistory);
		std::wstring text;
		for (auto& c : history)
			text += (text.empty() ? L"" : L"\n") + c;
		auto& settings = AppSettings::Get();
		settings.RunHistory(text);
		settings.Save();
	}

	// a program into the combo box's edit, quoted if its path has spaces (the first token is the file)
	void Browse(HWND hOwner, CComboBox combo) {
		COMDLG_FILTERSPEC const types[] = {
			{ L"Programs", L"*.exe;*.com;*.bat;*.cmd;*.msc;*.cpl" },
			{ L"All Files", L"*.*" },
		};
		SuspendResumeHook sr;
		CShellFileOpenDialog dlg(nullptr, FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST, nullptr, types, _countof(types));
		if (dlg.DoModal(hOwner) != IDOK)
			return;
		CString path;
		if (FAILED(dlg.GetFilePath(path)))
			return;
		if (path.Find(L' ') >= 0)
			path = L"\"" + path + L"\"";
		combo.SetWindowText(path);
		combo.SetFocus();
		combo.SetEditSel(path.GetLength(), path.GetLength());
	}

	void ShowError(HWND hOwner, PCWSTR operation, DWORD error) {
		AtlMessageBox(hOwner, std::format(L"{} failed: {}", operation, WinSys::Helpers::GetErrorText(error)).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
	}
}

LRESULT CRunDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CComboBox combo(GetDlgItem(IDC_RUN_COMMAND));
	FillHistory(combo);
	CenterWindow(GetParent());
	GotoDlgCtrl(combo);
	return FALSE;
}

LRESULT CRunDlg::OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Browse(m_hWnd, CComboBox(GetDlgItem(IDC_RUN_COMMAND)));
	return 0;
}

LRESULT CRunDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto command = GetText(GetDlgItem(IDC_RUN_COMMAND));
	if (command.empty()) {
		::MessageBeep(MB_ICONASTERISK);
		GotoDlgCtrl(GetDlgItem(IDC_RUN_COMMAND));
		return 0;
	}
	bool elevated = IsDlgButtonChecked(IDC_RUN_ELEVATED) == BST_CHECKED;
	if (!WinSys::Process::Run(command, elevated, m_hWnd)) {
		// a declined elevation prompt needs no message
		if (auto error = ::GetLastError(); error != ERROR_CANCELLED)
			ShowError(m_hWnd, std::format(L"Running \"{}\"", command).c_str(), error);
		GotoDlgCtrl(GetDlgItem(IDC_RUN_COMMAND));
		return 0;
	}
	AddToHistory(command);
	EndDialog(IDOK);
	return 0;
}

LRESULT CRunDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}

LRESULT CRunAsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CComboBox combo(GetDlgItem(IDC_RUNAS_COMMAND));
	FillHistory(combo);
	auto user = AppSettings::Get().RunAsUser();
	SetDlgItemText(IDC_RUNAS_USER, user.c_str());
	CheckDlgButton(IDC_RUNAS_PROFILE, BST_CHECKED);
	CenterWindow(GetParent());
	// the user known, the password is what's left to type
	GotoDlgCtrl(GetDlgItem(user.empty() ? IDC_RUNAS_COMMAND : IDC_RUNAS_PASSWORD));
	return FALSE;
}

LRESULT CRunAsDlg::OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Browse(m_hWnd, CComboBox(GetDlgItem(IDC_RUNAS_COMMAND)));
	return 0;
}

LRESULT CRunAsDlg::OnNetOnly(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the process runs as the caller then: no other user's profile to load
	GetDlgItem(IDC_RUNAS_PROFILE).EnableWindow(IsDlgButtonChecked(IDC_RUNAS_NETONLY) != BST_CHECKED);
	return 0;
}

LRESULT CRunAsDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto command = GetText(GetDlgItem(IDC_RUNAS_COMMAND));
	auto user = GetText(GetDlgItem(IDC_RUNAS_USER));
	for (auto [text, id] : { std::pair{ &command, IDC_RUNAS_COMMAND }, std::pair{ &user, IDC_RUNAS_USER } })
		if (text->empty()) {
			::MessageBeep(MB_ICONASTERISK);
			GotoDlgCtrl(GetDlgItem(id));
			return 0;
		}

	// the password as typed (not trimmed), wiped once used
	CEdit edit(GetDlgItem(IDC_RUNAS_PASSWORD));
	std::wstring password(edit.GetWindowTextLength() + 1, L'\0');
	password.resize(edit.GetWindowText(password.data(), (int)password.size()));
	bool netOnly = IsDlgButtonChecked(IDC_RUNAS_NETONLY) == BST_CHECKED;
	bool profile = IsDlgButtonChecked(IDC_RUNAS_PROFILE) == BST_CHECKED;
	auto pid = WinSys::Process::RunAs(command, user, password, profile, netOnly);
	auto error = ::GetLastError();
	::SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
	if (!pid) {
		ShowError(m_hWnd, std::format(L"Running \"{}\" as {}", command, user).c_str(), error);
		// a wrong password is the likely mistake
		auto field = error == ERROR_LOGON_FAILURE ? IDC_RUNAS_PASSWORD : IDC_RUNAS_COMMAND;
		GotoDlgCtrl(GetDlgItem(field));
		if (field == IDC_RUNAS_PASSWORD)
			edit.SetSelAll();
		return 0;
	}
	AddToHistory(command);
	auto& settings = AppSettings::Get();
	settings.RunAsUser(user);
	settings.Save();
	EndDialog(IDOK);
	return 0;
}

LRESULT CRunAsDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
