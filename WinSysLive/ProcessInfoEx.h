#pragma once

#include <WinSys/ProcessManager.h>
#include <WinSys/Enums.h>
#include <WinSys/Processes.h>

// Per-process data the views need beyond what WinSys provides. Most values are computed on first use and cached,
// as getting them means opening the process (and for the user name, an account lookup). Values that change
// (GUI objects, memory and I/O priority) are queried on every call through a handle opened once.
struct ProcessInfoEx : WinSys::ProcessInfo {
	const std::wstring& GetImagePath();		// Win32 path
	const std::wstring& GetUser();			// DOMAIN\user
	const std::wstring& GetCommandLine();
	const std::wstring& GetDescription();	// FileDescription from the image's version resource
	PCWSTR GetArchitecture();				// "64-bit" or "32-bit"; empty if the process can't be opened
	WinSys::VirtualizationState GetVirtualization();
	// whether the process is in a job; for when the snapshot has no job ID (not elevated)
	bool IsInJob();
	WinSys::IntegrityLevel GetIntegrityLevel();	// Error if unknown
	WinSys::ProcessProtection GetProtection();	// Level 0: not protected (or unknown)
	// from PROCESS_EXTENDED_BASIC_INFORMATION; false if the process can't be opened
	bool IsProtected();
	bool IsSecure();		// runs in VTL 1 (IUM), like LsaIso
	bool IsImmersive();		// a UWP (Store) app

	// current values; -1 (IoPriority::Unknown) if the process can't be opened
	int GetMemoryPriority();
	WinSys::IoPriority GetIoPriority();
	int GetGdiObjects();
	int GetUserObjects();
	// EcoQoS (Task Manager's efficiency mode): execution speed throttled; nothing if it can't be queried
	std::optional<bool> GetEfficiencyMode();
	// a handle with PROCESS_QUERY_INFORMATION, opened once (the GPU statistics refuse a limited one); null if it can't
	// be opened (protected processes, other users' without elevation)
	HANDLE GetQueryInformationHandle();

private:
	// opened once with limited query access; nullptr if that fails
	WinSys::Process* GetProcess();

	std::unique_ptr<WinSys::Process> m_Process;
	bool m_ProcessDone{ false };
	std::unique_ptr<WinSys::Process> m_QueryInformation;
	bool m_QueryInformationDone{ false };
	WinSys::IntegrityLevel m_Integrity{ WinSys::IntegrityLevel::Error };
	WinSys::ProcessProtection m_Protection{};
	bool m_IntegrityDone{ false }, m_ProtectionDone{ false };
	// the protected and secure flags, queried once
	void QueryExtendedBasicInfo();
	bool m_ExtendedBasicDone{ false }, m_Protected{ false }, m_Secure{ false };
	bool m_ImmersiveDone{ false }, m_Immersive{ false };

	std::wstring m_ImagePath, m_User, m_CommandLine, m_Description;
	PCWSTR m_Architecture{ L"" };
	WinSys::VirtualizationState m_Virtualization{ WinSys::VirtualizationState::Unknown };
	bool m_InJob{ false };
	bool m_ImagePathDone{ false }, m_UserDone{ false }, m_CommandLineDone{ false }, m_DescriptionDone{ false };
	bool m_ArchitectureDone{ false }, m_VirtualizationDone{ false }, m_InJobDone{ false };
};

using ProcessManager = WinSys::ProcessManager<ProcessInfoEx>;
