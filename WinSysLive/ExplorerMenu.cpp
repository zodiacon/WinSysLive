#include "pch.h"
#include "ExplorerMenu.h"
#include <ShlObj.h>

namespace {
	// the classes the verb is on: every file, folders, drives
	PCWSTR const s_Classes[] = { L"*", L"Directory", L"Drive" };
	PCWSTR const VerbName = L"WinSysLive.Locks";

	std::wstring VerbKey(PCWSTR cls) {
		return std::format(L"Software\\Classes\\{}\\shell\\{}", cls, VerbName);
	}

	std::wstring ExePath() {
		WCHAR exe[MAX_PATH];
		::GetModuleFileName(nullptr, exe, _countof(exe));
		return exe;
	}

	std::wstring Command() {
		return std::format(L"\"{}\" /locks \"%1\"", ExePath());
	}
}

bool ExplorerMenu::IsRegistered() {
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, (VerbKey(s_Classes[0]) + L"\\command").c_str(), KEY_READ) != ERROR_SUCCESS)
		return false;
	WCHAR value[MAX_PATH * 2];
	ULONG chars = _countof(value);
	return key.QueryStringValue(nullptr, value, &chars) == ERROR_SUCCESS && ::_wcsicmp(value, Command().c_str()) == 0;
}

bool ExplorerMenu::Register() {
	auto exe = ExePath();
	auto command = Command();
	for (auto cls : s_Classes) {
		CRegKey verb, cmd;
		auto path = VerbKey(cls);
		// CRegKey returns the error (it doesn't set the last error)
		LONG error = verb.Create(HKEY_CURRENT_USER, path.c_str());
		if (error == ERROR_SUCCESS)
			error = verb.SetStringValue(nullptr, L"Who's Locking This? (WinSysLive)");
		if (error == ERROR_SUCCESS)
			error = verb.SetStringValue(L"Icon", std::format(L"\"{}\",0", exe).c_str());
		if (error == ERROR_SUCCESS)
			error = cmd.Create(HKEY_CURRENT_USER, (path + L"\\command").c_str());
		if (error == ERROR_SUCCESS)
			error = cmd.SetStringValue(nullptr, command.c_str());
		if (error != ERROR_SUCCESS) {
			Unregister();
			::SetLastError(error);
			return false;
		}
	}
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return true;
}

bool ExplorerMenu::Unregister() {
	bool ok = true;
	for (auto cls : s_Classes) {
		auto error = ::RegDeleteTree(HKEY_CURRENT_USER, VerbKey(cls).c_str());
		if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
			::SetLastError(error);
			ok = false;
		}
	}
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return ok;
}

std::optional<std::wstring> ExplorerMenu::GetLocksArgument(PCWSTR cmdLine) {
	if (!cmdLine || !*cmdLine)
		return std::nullopt;
	// CommandLineToArgvW takes the program's name first: given a stand-in, the arguments come after it
	int count = 0;
	auto args = ::CommandLineToArgvW(std::format(L"x {}", cmdLine).c_str(), &count);
	if (!args)
		return std::nullopt;
	std::optional<std::wstring> path;
	for (int i = 1; i + 1 < count; i++)
		if (::_wcsicmp(args[i], L"/locks") == 0) {
			path = args[i + 1];
			break;
		}
	::LocalFree(args);
	if (!path)
		return std::nullopt;
	// a drive's "%1" is "C:\", whose backslash escapes the closing quote: C:" comes out
	if (!path->empty() && path->back() == L'"')
		path->pop_back();
	if (path->size() == 2 && (*path)[1] == L':')
		*path += L'\\';
	return path->empty() ? std::nullopt : path;
}
