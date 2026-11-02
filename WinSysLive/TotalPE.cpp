#include "pch.h"
#include "TotalPE.h"
#include "resource.h"
#include "AppSettings.h"
#include <WinSys/Helpers.h>

namespace {
	bool Exists(std::wstring const& path) {
		auto attributes = ::GetFileAttributes(path.c_str());
		return !path.empty() && attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	// a registry value's string (expanded), empty if there's none
	std::wstring RegistryString(HKEY root, PCWSTR key, PCWSTR value) {
		WCHAR text[MAX_PATH * 2]{};
		DWORD size = sizeof(text) - sizeof(WCHAR);
		if (::RegGetValue(root, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, text, &size) != ERROR_SUCCESS)
			return L"";
		return text;
	}

	// the executable of a command line ("C:\x\TotalPE.exe" "%1", or unquoted up to the first space)
	std::wstring CommandExecutable(std::wstring const& command) {
		if (command.empty())
			return L"";
		if (command[0] == L'"') {
			auto end = command.find(L'"', 1);
			return end == std::wstring::npos ? L"" : command.substr(1, end - 1);
		}
		auto space = command.find(L' ');
		return command.substr(0, space);
	}
}

std::wstring TotalPE::Find() {
	if (auto saved = AppSettings::Get().TotalPEPath(); Exists(saved))
		return saved;
	// beside this exe
	WCHAR self[MAX_PATH];
	if (::GetModuleFileName(nullptr, self, _countof(self))) {
		std::wstring path(self);
		path = path.substr(0, path.rfind(L'\\') + 1) + L"TotalPE.exe";
		if (Exists(path))
			return path;
	}
	// the installer's shell registration, then App Paths (either hive)
	if (auto path = CommandExecutable(RegistryString(HKEY_CLASSES_ROOT, L"Applications\\TotalPE.exe\\shell\\open\\command", nullptr)); Exists(path))
		return path;
	for (auto root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
		if (auto path = RegistryString(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\TotalPE.exe", nullptr); Exists(path))
			return path;
	WCHAR found[MAX_PATH];
	if (::SearchPath(nullptr, L"TotalPE.exe", nullptr, _countof(found), found, nullptr))
		return found;
	return L"";
}

bool TotalPE::Open(HWND hOwner, std::wstring const& path) {
	auto exe = Find();
	if (exe.empty()) {
		if (AtlMessageBox(hOwner, L"TotalPE wasn't found (it comes with Pavel Tools, or from github.com/zodiacon/TotalPE).\n\nLocate TotalPE.exe?",
			IDR_MAINFRAME, MB_ICONQUESTION | MB_YESNO) != IDYES)
			return false;
		CFileDialog dlg(TRUE, L"exe", L"TotalPE.exe", OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"TotalPE (TotalPE.exe)\0TotalPE.exe\0Programs (*.exe)\0*.exe\0", hOwner);
		if (dlg.DoModal(hOwner) != IDOK)
			return false;
		exe = dlg.m_szFileName;
		auto& settings = AppSettings::Get();
		settings.TotalPEPath(exe);
		settings.Save();
	}
	auto args = L"\"" + path + L"\"";
	auto result = (INT_PTR)::ShellExecute(hOwner, L"open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
	if (result <= 32) {
		AtlMessageBox(hOwner, std::format(L"TotalPE ({}) didn't start: {}", exe, WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}
	return true;
}
