#pragma once

#include <WinSys/ProcessManager.h>

// Per-process data the views need beyond what WinSys provides. Each value is computed on first use and cached,
// as getting it means opening the process (and for the user name, an account lookup).
struct ProcessInfoEx : WinSys::ProcessInfo {
	const std::wstring& GetImagePath();		// Win32 path
	const std::wstring& GetUser();			// DOMAIN\user
	const std::wstring& GetCommandLine();

private:
	std::wstring m_ImagePath, m_User, m_CommandLine;
	bool m_ImagePathDone{ false }, m_UserDone{ false }, m_CommandLineDone{ false };
};

using ProcessManager = WinSys::ProcessManager<ProcessInfoEx>;
