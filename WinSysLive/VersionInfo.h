#pragma once

#include <WinSys/FileVersion.h>

//
// Strings from an executable's version resource (WinSys FileVersion), read once per file (by path, ignoring case) and
// cached for the lifetime of the app. Any thread (the property sheets' Modules pages use it too).
//
WinSys::FileVersionInfo const& GetCachedVersionInfo(std::wstring const& path);
