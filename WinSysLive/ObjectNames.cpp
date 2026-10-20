#include "pch.h"
#include "ObjectNames.h"
#include "resource.h"
#include <WinSys/Sid.h>

using namespace WinSys;

std::wstring RegistryName(std::wstring name) {
	// \REGISTRY\MACHINE\... and \REGISTRY\USER\<SID>\... as the registry editor shows them
	static const std::wstring currentUser = [] {
		std::wstring sid;
		HANDLE hToken;
		if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
			BYTE buffer[256];
			DWORD len;
			if (NT_SUCCESS(::NtQueryInformationToken(hToken, TokenUser, buffer, sizeof(buffer), &len)))
				sid = L"\\REGISTRY\\USER\\" + Sid(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid).AsString();
			::CloseHandle(hToken);
		}
		return sid;
	}();
	auto replace = [&](std::wstring const& prefix, PCWSTR with) {
		if (prefix.empty() || ::_wcsnicmp(name.c_str(), prefix.c_str(), prefix.size()) != 0)
			return false;
		// the whole prefix or up to a backslash, not part of a longer name
		if (name.size() > prefix.size() && name[prefix.size()] != L'\\')
			return false;
		name = with + name.substr(prefix.size());
		return true;
	};
	// the first that matches
	(void)(replace(currentUser + L"_Classes", L"HKCU\\Software\\Classes") || replace(currentUser, L"HKCU")
		|| replace(L"\\REGISTRY\\MACHINE", L"HKLM") || replace(L"\\REGISTRY\\USER", L"HKU"));
	return name;
}

UINT GetHandleIcon(std::wstring const& type, std::wstring const& name) {
	if (type == L"File")
		// devices and pipes keep their NT names; files on drives get DOS paths
		return name.starts_with(L"\\") ? IDI_DEVICE : IDI_FILE;
	static const std::unordered_map<std::wstring, UINT> icons{
		{ L"Event", IDI_EVENT }, { L"Mutant", IDI_MUTEX }, { L"Semaphore", IDI_SEMAPHORE },
		{ L"Timer", IDI_TIMER }, { L"IRTimer", IDI_TIMER }, { L"Key", IDI_KEY }, { L"ALPC Port", IDI_ALPC },
		{ L"Desktop", IDI_DESKTOP }, { L"WindowStation", IDI_WINSTA }, { L"Composition", IDI_GRAPHICS },
		{ L"Directory", IDI_DIRECTORY }, { L"Section", IDI_SECTION }, { L"Session", IDI_SESSION }, { L"Token", IDI_TOKEN }, { L"Thread", IDI_THREADS },
		{ L"Job", IDI_JOB },
		{ L"EtwRegistration", IDI_ETW }, { L"EtwConsumer", IDI_ETW }, { L"EtwSessionDemuxEntry", IDI_ETW },
		{ L"FilterConnectionPort", IDI_PORT }, { L"FilterCommunicationPort", IDI_PORT },
	};
	if (auto it = icons.find(type); it != icons.end())
		return it->second;
	// DxgkSharedResource, DxgkCompositionObject...
	return type.starts_with(L"Dxgk") ? IDI_DIRECTX : IDI_OBJECT;
}

DosNames::DosNames() {
	auto drives = ::GetLogicalDrives();
	for (int drive = 0; drives; drive++, drives >>= 1) {
		WCHAR letter[] = L"X:";
		letter[0] = WCHAR(L'A' + drive);
		WCHAR device[MAX_PATH];
		if ((drives & 1) && ::QueryDosDevice(letter, device, _countof(device)))
			m_Devices.push_back({ std::wstring(device) + L"\\", std::wstring(letter) + L"\\" });
	}
}

std::wstring DosNames::Convert(std::wstring const& name) const {
	for (auto& [device, letter] : m_Devices)
		if (::_wcsnicmp(name.c_str(), device.c_str(), device.size()) == 0)
			return letter + name.substr(device.size());
	return name;
}

std::wstring QueryHandleName(HANDLE hProcess, HandleEntry const& entry, ObjectNameQuery& nameQuery, DosNames const& dosNames,
	std::function<std::wstring(DWORD pid)> const& processName, bool queryFiles) {
	if (!hProcess)
		return L"";
	auto& type = ObjectTypes::GetName(entry.TypeIndex);
	bool file = type == L"File";
	if (file && !queryFiles)
		return L"";

	// a copy of the handle in this process. Processes and threads are queried for their IDs, which the handle's own
	// access may not allow, so they're duplicated with the access that needs first
	wil::unique_handle h;
	auto source = (HANDLE)(ULONG_PTR)entry.Value;
	DWORD queryAccess = type == L"Process" ? PROCESS_QUERY_LIMITED_INFORMATION : type == L"Thread" ? THREAD_QUERY_LIMITED_INFORMATION : 0;
	if ((!queryAccess || !::DuplicateHandle(hProcess, source, ::GetCurrentProcess(), h.addressof(), queryAccess, FALSE, 0))
		&& !::DuplicateHandle(hProcess, source, ::GetCurrentProcess(), h.addressof(), 0, FALSE, DUPLICATE_SAME_ACCESS))
		return L"";

	// processes and threads have no names: their IDs and image names are more useful
	auto processText = [&](DWORD pid, HANDLE hTarget) {
		std::wstring name = processName ? processName(pid) : L"";
		if (name.empty() && hTarget) {
			// not known (it may have exited): the image path from the handle
			WCHAR path[MAX_PATH];
			DWORD size = _countof(path);
			if (::QueryFullProcessImageName(hTarget, 0, path, &size)) {
				auto slash = wcsrchr(path, L'\\');
				name = slash ? slash + 1 : path;
			}
		}
		return name.empty() ? std::format(L"PID: {}", pid) : std::format(L"PID: {}, {}", pid, name);
	};
	if (type == L"Process") {
		if (auto pid = ::GetProcessId(h.get()))
			return processText(pid, h.get());
		return L"";
	}
	if (type == L"Thread") {
		if (auto tid = ::GetThreadId(h.get()))
			return std::format(L"TID: {}, {}", tid, processText(::GetProcessIdOfThread(h.get()), nullptr));
		return L"";
	}
	if (type == L"Token") {
		// the user, and the logon session
		BYTE buffer[256];
		DWORD len;
		TOKEN_STATISTICS stats;
		if (NT_SUCCESS(::NtQueryInformationToken(h.get(), TokenUser, buffer, sizeof(buffer), &len))
			&& NT_SUCCESS(::NtQueryInformationToken(h.get(), TokenStatistics, &stats, sizeof(stats), &len)))
			return std::format(L"{}:{:x}", Sid(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid).UserName(), stats.AuthenticationId.LowPart);
		return L"";
	}

	std::wstring name;
	if (file) {
		// a file opened for synchronous I/O blocks the query while I/O is in progress (a pipe being read): a worker
		// thread with a timeout
		auto result = nameQuery.GetName(h.get());
		if (!result || result->empty())
			return L"";
		return dosNames.Convert(*result);
	}
	// other objects' queries don't block
	BYTE buffer[2048];
	ULONG len;
	if (!NT_SUCCESS(::NtQueryObject(h.get(), ObjectNameInformation, buffer, sizeof(buffer), &len)))
		return L"";
	auto& info = *reinterpret_cast<OBJECT_NAME_INFORMATION const*>(buffer);
	if (!info.Name.Buffer || info.Name.Length == 0)
		return L"";
	name.assign(info.Name.Buffer, info.Name.Length / sizeof(WCHAR));
	return type == L"Key" ? RegistryName(std::move(name)) : name;
}
