#include "pch.h"
#include "VersionInfo.h"

#pragma comment(lib, "version")

FileVersionInfo ReadVersionInfo(std::wstring const& path) {
	FileVersionInfo info;
	DWORD handle;
	auto size = ::GetFileVersionInfoSize(path.c_str(), &handle);
	if (size == 0)
		return info;
	auto buffer = std::make_unique<BYTE[]>(size);
	if (!::GetFileVersionInfo(path.c_str(), 0, size, buffer.get()))
		return info;

	// the first language listed; English (US), Unicode if there's no list
	struct Translation {
		WORD Language, CodePage;
	} *translation;
	UINT len;
	std::wstring prefix = L"\\StringFileInfo\\040904B0\\";
	if (::VerQueryValue(buffer.get(), L"\\VarFileInfo\\Translation", (void**)&translation, &len) && len >= sizeof(Translation))
		prefix = std::format(L"\\StringFileInfo\\{:04X}{:04X}\\", translation->Language, translation->CodePage);
	auto query = [&](PCWSTR name) -> std::wstring {
		PCWSTR text;
		if (!::VerQueryValue(buffer.get(), (prefix + name).c_str(), (void**)&text, &len) || len == 0)
			return L"";
		return text;
	};
	info.Description = query(L"FileDescription");
	info.Company = query(L"CompanyName");
	info.Version = query(L"FileVersion");
	return info;
}

FileVersionInfo const& GetCachedVersionInfo(std::wstring const& path) {
	static std::unordered_map<std::wstring, FileVersionInfo> cache;
	auto key = path;
	::CharLowerBuff(key.data(), (DWORD)key.size());
	auto it = cache.find(key);
	if (it == cache.end())
		it = cache.insert({ std::move(key), path.empty() ? FileVersionInfo{} : ReadVersionInfo(path) }).first;
	return it->second;
}
