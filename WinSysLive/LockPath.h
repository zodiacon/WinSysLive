#pragma once

#include <string_view>
#include <cwctype>

//
// Who's locking a file or folder (Find Handle or DLL's Whole Path): a handle's or mapping's DOS path names the file
// itself or, for a folder, the folder or anything under it. Case-insensitive, as NTFS names are to Win32; a trailing
// backslash on the path (C:\Temp\, C:\) is the folder's. Header-only, for the tests.
//
namespace LockPath {
	inline bool EqualNoCase(std::wstring_view a, std::wstring_view b) {
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++)
			if (std::towlower(a[i]) != std::towlower(b[i]))
				return false;
		return true;
	}

	inline bool Matches(std::wstring_view name, std::wstring_view path) {
		while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/'))
			path.remove_suffix(1);
		if (path.empty())
			return false;
		// a drive's root was C: once the backslash went
		bool root = path.size() == 2 && path[1] == L':';
		if (!root && EqualNoCase(name, path))
			return true;
		return name.size() > path.size() && name[path.size()] == L'\\' && EqualNoCase(name.substr(0, path.size()), path);
	}
}
