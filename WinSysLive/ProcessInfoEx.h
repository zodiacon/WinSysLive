#pragma once

#include <WinSys/ProcessManager.h>
#include <WinSys/Enums.h>

// Per-process data the views need beyond what WinSys provides. Each value is computed on first use and cached,
// as getting it means opening the process (and for the user name, an account lookup).
struct ProcessInfoEx : WinSys::ProcessInfo {
	const std::wstring& GetImagePath();		// Win32 path
	const std::wstring& GetUser();			// DOMAIN\user
	const std::wstring& GetCommandLine();
	const std::wstring& GetDescription();	// FileDescription from the image's version resource
	PCWSTR GetArchitecture();				// "64-bit" or "32-bit"; empty if the process can't be opened
	WinSys::VirtualizationState GetVirtualization();
	// whether the process is in a job; for when the snapshot has no job ID (not elevated)
	bool IsInJob();

private:
	std::wstring m_ImagePath, m_User, m_CommandLine, m_Description;
	PCWSTR m_Architecture{ L"" };
	WinSys::VirtualizationState m_Virtualization{ WinSys::VirtualizationState::Unknown };
	bool m_InJob{ false };
	bool m_ImagePathDone{ false }, m_UserDone{ false }, m_CommandLineDone{ false }, m_DescriptionDone{ false };
	bool m_ArchitectureDone{ false }, m_VirtualizationDone{ false }, m_InJobDone{ false };
};

using ProcessManager = WinSys::ProcessManager<ProcessInfoEx>;
