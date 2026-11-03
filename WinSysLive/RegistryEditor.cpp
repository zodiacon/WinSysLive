#include "pch.h"
#include "RegistryEditor.h"

void OpenRegistryEditor(HWND hWnd, std::wstring const& editorPath) {
	CRegKey key;
	if (key.Create(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit") == ERROR_SUCCESS)
		key.SetStringValue(L"LastKey", editorPath.c_str());
	::ShellExecute(hWnd, nullptr, L"regedit.exe", L"-m", nullptr, SW_SHOWNORMAL);
}
