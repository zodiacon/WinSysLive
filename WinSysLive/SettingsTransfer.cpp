#include "pch.h"
#include "SettingsTransfer.h"
#include "AppSettings.h"
#include <ShlObj.h>

std::wstring SettingsTransfer::PendingPath() {
	PWSTR folder = nullptr;
	std::wstring path;
	if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
		path = std::wstring(folder) + L"\\ScorpioSoftware\\WinSysLive\\PendingImport.ini";
	::CoTaskMemFree(folder);
	return path;
}

bool SettingsTransfer::Export(std::wstring const& path) {
	// a new file as UTF-16 (a BOM): WritePrivateProfileString then writes Unicode, not the ANSI code page
	{
		wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!file)
			return false;
		WORD bom = 0xFEFF;
		DWORD written;
		if (!::WriteFile(file.get(), &bom, sizeof(bom), &written, nullptr))
			return false;
	}
	return AppSettings::Get().SaveToFile(path.c_str());
}

bool SettingsTransfer::StageImport(std::wstring const& path) {
	auto pending = PendingPath();
	if (pending.empty()) {
		::SetLastError(ERROR_PATH_NOT_FOUND);
		return false;
	}
	auto folder = pending.substr(0, pending.rfind(L'\\'));
	::SHCreateDirectoryEx(nullptr, folder.c_str(), nullptr);
	return ::CopyFile(path.c_str(), pending.c_str(), FALSE);
}

namespace {
	// the [General] values of from over those of to, as text (both written by Settings, so in the same formats)
	void MergeValues(std::wstring const& from, std::wstring const& to) {
		std::vector<WCHAR> names(1 << 16), value(1 << 16);
		auto length = ::GetPrivateProfileString(L"General", nullptr, L"", names.data(), (DWORD)names.size(), from.c_str());
		for (auto name = names.data(); name < names.data() + length && *name; name += ::wcslen(name) + 1) {
			::GetPrivateProfileString(L"General", name, L"", value.data(), (DWORD)value.size(), from.c_str());
			::WritePrivateProfileString(L"General", name, value.data(), to.c_str());
		}
	}
}

bool SettingsTransfer::ApplyPendingImport() {
	auto pending = PendingPath();
	if (pending.empty() || ::GetFileAttributes(pending.c_str()) == INVALID_FILE_ATTRIBUTES)
		return false;
	auto& settings = AppSettings::Get();
	// LoadFromFile makes the file where Save writes: the storage loaded from is put back, and written with the import
	auto storage = settings.GetPath();
	// a file without some settings (from an older version) would read them as 0 or empty: the current settings go to a
	// file first, the imported values over them, and that's what's loaded
	auto merged = pending.substr(0, pending.rfind(L'\\')) + L"\\Merged.ini";
	bool loaded = Export(merged);
	if (loaded) {
		MergeValues(pending, merged);
		loaded = settings.LoadFromFile(merged.c_str());
		::DeleteFile(merged.c_str());
	}
	if (!storage.empty()) {
		if (storage.size() > 1 && storage[1] == L':') {
			settings.SaveToFile(storage.c_str());
			settings.LoadFromFile(storage.c_str());
		}
		else {
			settings.SaveToKey(storage.c_str());
			settings.LoadFromKey(storage.c_str());
		}
	}
	::DeleteFile(pending.c_str());
	return loaded;
}
