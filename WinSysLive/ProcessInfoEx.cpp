#include "pch.h"
#include "ProcessInfoEx.h"
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>

#pragma comment(lib, "version")

using namespace WinSys;

namespace {
	std::wstring ReadFileDescription(std::wstring const& path) {
		DWORD handle;
		auto size = ::GetFileVersionInfoSize(path.c_str(), &handle);
		if (size == 0)
			return L"";
		auto buffer = std::make_unique<BYTE[]>(size);
		if (!::GetFileVersionInfo(path.c_str(), 0, size, buffer.get()))
			return L"";

		// the first language listed; English (US), Unicode if there's no list
		struct Translation {
			WORD Language, CodePage;
		} *translation;
		UINT len;
		std::wstring sub = L"\\StringFileInfo\\040904B0\\FileDescription";
		if (::VerQueryValue(buffer.get(), L"\\VarFileInfo\\Translation", (void**)&translation, &len) && len >= sizeof(Translation))
			sub = std::format(L"\\StringFileInfo\\{:04X}{:04X}\\FileDescription", translation->Language, translation->CodePage);
		PCWSTR text;
		if (!::VerQueryValue(buffer.get(), sub.c_str(), (void**)&text, &len) || len == 0)
			return L"";
		return text;
	}
}

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
		auto& path = GetImagePath();
		if (!path.empty()) {
			// many processes share an image (svchost.exe...); reading the version resource is file I/O
			static std::unordered_map<std::wstring, std::wstring> cache;
			auto key = path;
			::CharLowerBuff(key.data(), (DWORD)key.size());
			auto it = cache.find(key);
			if (it == cache.end())
				it = cache.insert({ key, ReadFileDescription(path) }).first;
			m_Description = it->second;
		}
	}
	return m_Description;
}

PCWSTR ProcessInfoEx::GetArchitecture() {
	if (!m_ArchitectureDone) {
		m_ArchitectureDone = true;
		if (auto process = Process::OpenById(Id); process)
			m_Architecture = process->IsWow64Process() ? L"32-bit" : L"64-bit";
	}
	return m_Architecture;
}

VirtualizationState ProcessInfoEx::GetVirtualization() {
	if (!m_VirtualizationDone) {
		m_VirtualizationDone = true;
		if (auto process = Process::OpenById(Id); process)
			m_Virtualization = process->GetVirtualizationState();
	}
	return m_Virtualization;
}

bool ProcessInfoEx::IsInJob() {
	if (!m_InJobDone) {
		m_InJobDone = true;
		if (auto process = Process::OpenById(Id); process)
			m_InJob = process->IsInJob();
	}
	return m_InJob;
}
