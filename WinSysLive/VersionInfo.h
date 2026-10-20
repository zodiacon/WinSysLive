#pragma once

//
// Strings from an executable's version resource, read once per file (by path, ignoring case) and cached for the
// lifetime of the app. UI thread only.
//
struct FileVersionInfo {
	std::wstring Description, Company, Version;
};

FileVersionInfo const& GetCachedVersionInfo(std::wstring const& path);
// not cached: for other threads
FileVersionInfo ReadVersionInfo(std::wstring const& path);
