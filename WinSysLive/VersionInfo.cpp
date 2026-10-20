#include "pch.h"
#include "VersionInfo.h"

WinSys::FileVersionInfo const& GetCachedVersionInfo(std::wstring const& path) {
	static std::unordered_map<std::wstring, WinSys::FileVersionInfo> cache;
	auto key = path;
	::CharLowerBuff(key.data(), (DWORD)key.size());
	auto it = cache.find(key);
	if (it == cache.end())
		it = cache.insert({ std::move(key), path.empty() ? WinSys::FileVersionInfo{} : WinSys::FileVersion::Read(path) }).first;
	return it->second;
}
