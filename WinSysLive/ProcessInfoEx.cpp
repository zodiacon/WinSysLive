#include "pch.h"
#include "ProcessInfoEx.h"
#include "VersionInfo.h"
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>

using namespace WinSys;

const std::wstring& ProcessInfoEx::GetImagePath() {
	if (!m_ImagePathDone) {
		m_ImagePathDone = true;
		if (auto process = Process::OpenById(Id); process)
			m_ImagePath = process->GetFullImageName();
		// the native path is only available when running elevated
		if (m_ImagePath.empty() && !GetNativeImagePath().empty())
			m_ImagePath = Helpers::GetDosNameFromNtName(GetNativeImagePath().c_str());
	}
	return m_ImagePath;
}

const std::wstring& ProcessInfoEx::GetUser() {
	if (!m_UserDone) {
		m_UserDone = true;
		// when elevated, the snapshot has the user's SID; otherwise, ask the process token
		if (ExtendedInfo)
			m_User = GetUserName();
		else if (auto process = Process::OpenById(Id); process)
			m_User = process->GetUserName();
	}
	return m_User;
}

const std::wstring& ProcessInfoEx::GetCommandLine() {
	if (!m_CommandLineDone) {
		m_CommandLineDone = true;
		if (auto process = Process::OpenById(Id); process)
			m_CommandLine = process->GetCommandLine();
	}
	return m_CommandLine;
}

const std::wstring& ProcessInfoEx::GetDescription() {
	if (!m_DescriptionDone) {
		m_DescriptionDone = true;
		// many processes share an image (svchost.exe...), and reading the version resource is file I/O: it's cached
		m_Description = GetCachedVersionInfo(GetImagePath()).Description;
	}
	return m_Description;
}

Process* ProcessInfoEx::GetProcess() {
	if (!m_ProcessDone) {
		m_ProcessDone = true;
		// the Idle process (0) can't be opened
		if (Id != 0)
			m_Process = Process::OpenById(Id);
	}
	return m_Process.get();
}

PCWSTR ProcessInfoEx::GetArchitecture() {
	if (!m_ArchitectureDone) {
		m_ArchitectureDone = true;
		if (auto process = GetProcess())
			m_Architecture = process->IsWow64Process() ? L"32-bit" : L"64-bit";
	}
	return m_Architecture;
}

VirtualizationState ProcessInfoEx::GetVirtualization() {
	if (!m_VirtualizationDone) {
		m_VirtualizationDone = true;
		if (auto process = GetProcess())
			m_Virtualization = process->GetVirtualizationState();
	}
	return m_Virtualization;
}

bool ProcessInfoEx::IsInJob() {
	if (!m_InJobDone) {
		m_InJobDone = true;
		if (auto process = GetProcess())
			m_InJob = process->IsInJob();
	}
	return m_InJob;
}

IntegrityLevel ProcessInfoEx::GetIntegrityLevel() {
	if (!m_IntegrityDone) {
		m_IntegrityDone = true;
		if (auto process = GetProcess())
			m_Integrity = process->GetIntegrityLevel();
	}
	return m_Integrity;
}

ProcessProtection ProcessInfoEx::GetProtection() {
	if (!m_ProtectionDone) {
		m_ProtectionDone = true;
		if (auto process = GetProcess())
			m_Protection = process->GetProtection();
	}
	return m_Protection;
}

void ProcessInfoEx::QueryExtendedBasicInfo() {
	if (m_ExtendedBasicDone)
		return;
	m_ExtendedBasicDone = true;
	// one query for both flags; limited query access is enough, also for protected processes
	if (auto process = GetProcess()) {
		PROCESS_EXTENDED_BASIC_INFORMATION info{ sizeof(info) };
		if (NT_SUCCESS(::NtQueryInformationProcess(process->Handle(), ProcessBasicInformation, &info, sizeof(info), nullptr))) {
			m_Protected = info.IsProtectedProcess;
			m_Secure = info.IsSecureProcess;
		}
	}
}

bool ProcessInfoEx::IsProtected() {
	QueryExtendedBasicInfo();
	return m_Protected;
}

bool ProcessInfoEx::IsSecure() {
	QueryExtendedBasicInfo();
	return m_Secure;
}

bool ProcessInfoEx::IsImmersive() {
	if (!m_ImmersiveDone) {
		m_ImmersiveDone = true;
		if (auto process = GetProcess())
			m_Immersive = process->IsImmersive();
	}
	return m_Immersive;
}

int ProcessInfoEx::GetMemoryPriority() {
	auto process = GetProcess();
	return process ? process->GetMemoryPriority() : -1;
}

IoPriority ProcessInfoEx::GetIoPriority() {
	auto process = GetProcess();
	return process ? process->GetIoPriority() : IoPriority::Unknown;
}

int ProcessInfoEx::GetGdiObjects() {
	auto process = GetProcess();
	return process ? (int)process->GetGdiObjectCount() : -1;
}

int ProcessInfoEx::GetUserObjects() {
	auto process = GetProcess();
	return process ? (int)process->GetUserObjectCount() : -1;
}
