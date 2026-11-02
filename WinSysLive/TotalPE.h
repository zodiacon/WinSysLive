#pragma once

#include <string>

//
// Opening a file in TotalPE (the author's PE viewer, github.com/zodiacon/TotalPE). It's found as: the TotalPEPath
// setting (where the user pointed to it), this exe's folder (the Pavel Tools installer puts both there), its shell
// registration (HKCR\Applications\TotalPE.exe\shell\open\command, the installer's), App Paths, then the PATH (WinGet's
// links among others).
//
namespace TotalPE {
	// the executable, or empty if it's nowhere
	std::wstring Find();
	// the file in TotalPE ("TotalPE.exe" "<path>"); not found, the user is asked where it is (saved in TotalPEPath).
	// false if that's canceled or it doesn't start (said in a message box)
	bool Open(HWND hOwner, std::wstring const& path);
}
