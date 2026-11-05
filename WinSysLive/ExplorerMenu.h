#pragma once

#include <optional>
#include <string>

//
// Options > Who's Locking in Explorer's Menu: a verb of the current user's (HKCU\Software\Classes) on files (*), folders
// (Directory) and drives (Drive), "Who's Locking This?", which starts the exe with /locks "<path>". The started instance
// passes the path to a running one (WM_COPYDATA, CopyDataLocks), which may be elevated (the message is let through UIPI),
// and exits; without one it runs and opens Find Handle or DLL on the path itself (CMainFrame::ShowLocks). On Windows 11
// the verb is under Show More Options, as any classic verb.
//
namespace ExplorerMenu {
	// WM_COPYDATA's dwData: the data is the path, with its terminating null
	constexpr ULONG_PTR CopyDataLocks = 0x4B434F4C;	// "LOCK"

	// the verb is there for this exe (another copy's counts as not)
	bool IsRegistered();
	bool Register();
	bool Unregister();
	// the path after /locks on the command line, if it has one
	std::optional<std::wstring> GetLocksArgument(PCWSTR cmdLine);
}
