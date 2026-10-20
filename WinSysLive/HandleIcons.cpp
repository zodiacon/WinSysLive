#include "pch.h"
#include "HandleIcons.h"
#include "resource.h"

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
