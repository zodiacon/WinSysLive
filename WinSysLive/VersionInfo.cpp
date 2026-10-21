#include "pch.h"
#include "VersionInfo.h"
#include <mutex>

WinSys::FileVersionInfo const& GetCachedVersionInfo(std::wstring const& path) {
	// node-based: a reference stays valid while others are added
	static std::unordered_map<std::wstring, WinSys::FileVersionInfo> cache;
	static std::mutex lock;
	std::lock_guard guard(lock);
	auto key = path;
	::CharLowerBuff(key.data(), (DWORD)key.size());
	auto it = cache.find(key);
	if (it == cache.end())
		it = cache.insert({ std::move(key), path.empty() ? WinSys::FileVersionInfo{} : WinSys::FileVersion::Read(path) }).first;
	return it->second;
}
