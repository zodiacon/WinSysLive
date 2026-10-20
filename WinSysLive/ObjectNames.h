#pragma once

#include <WinSys/Handles.h>
#include <functional>

//
// Names for kernel objects, shared by the Handles page (the UI thread) and Find Handle or DLL (a worker thread): all of
// it is safe to use from any thread.
//

// \REGISTRY\MACHINE\... and \REGISTRY\USER\<SID>\... as the registry editor shows them (HKLM\..., HKCU\..., HKU\...)
std::wstring RegistryName(std::wstring ntName);

// NT device paths (\Device\HarddiskVolume3\...) to drive letters, from a table read when it's constructed (WinSys
// Helpers::GetDosNameFromNtName builds a shared one without a lock)
class DosNames {
public:
	DosNames();
	// the name itself if no drive maps it
	std::wstring Convert(std::wstring const& ntName) const;

private:
	std::vector<std::pair<std::wstring, std::wstring>> m_Devices;	// "\Device\HarddiskVolume3\", "C:\"
};

// the icon (IDI_*, in the frame's image list) for a handle of the object type, with the name QueryHandleName gave it:
// files keep NT names only for devices and pipes, which get the device icon
UINT GetHandleIcon(std::wstring const& type, std::wstring const& name);

// the name the Handles page shows for a handle in another process (hProcess has PROCESS_DUP_HANDLE): processes and
// threads by ID and image name (processName: the image name of a process ID, or empty), tokens by user and logon
// session, files as DOS paths, keys as HKLM\...; other objects by their names. File names are queried through
// nameQuery (a file's query can hang) and only if queryFiles; the rest directly. Empty if it has no name or can't be
// queried
std::wstring QueryHandleName(HANDLE hProcess, WinSys::HandleEntry const& entry, WinSys::ObjectNameQuery& nameQuery,
	DosNames const& dosNames, std::function<std::wstring(DWORD pid)> const& processName, bool queryFiles = true);
