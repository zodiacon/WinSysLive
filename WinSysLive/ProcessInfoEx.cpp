#include "pch.h"
#include "ProcessInfoEx.h"
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
