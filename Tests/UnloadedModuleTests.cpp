#include "pch.h"
#include "TestFramework.h"
#include <WinSys/UnloadedModules.h>

using namespace WinSys;

// UnloadedModules::Parse on a captured trace laid out as RTL_UNLOAD_EVENT_TRACE (phnt), and a live query of a DLL this
// process loaded and freed

TEST(UnloadedModules_Parse) {
	RTL_UNLOAD_EVENT_TRACE trace[4]{};
	trace[0].BaseAddress = (PVOID)0x7FF800000000;
	trace[0].SizeOfImage = 0x2000;
	trace[0].Sequence = 1;
	trace[0].TimeDateStamp = 0x12345678;
	trace[0].Version[0] = (10 << 16) | 0;
	trace[0].Version[1] = (26100 << 16) | 1;
	wcscpy_s(trace[0].ImageName, L"first.dll");
	// unused (no base address)
	trace[1].Sequence = 9;
	trace[2].BaseAddress = (PVOID)0x7FF900000000;
	trace[2].Sequence = 2;
	// a name that fills the field, without a terminator
	wmemset(trace[2].ImageName, L'x', _countof(trace[2].ImageName));
	auto modules = UnloadedModules::Parse(trace, sizeof(RTL_UNLOAD_EVENT_TRACE), 4);
	CHECK_EQ(modules.size(), size_t(2));
	if (modules.size() == 2) {
		// newest first
		CHECK_EQ(modules[0].Sequence, 2u);
		CHECK_EQ(modules[0].Name.size(), size_t(32));
		CHECK_EQ(modules[1].Name, std::wstring(L"first.dll"));
		CHECK_EQ(modules[1].BaseAddress, uint64_t(0x7FF800000000));
		CHECK_EQ(modules[1].Size, uint64_t(0x2000));
		CHECK_EQ(modules[1].TimeDateStamp, 0x12345678u);
		CHECK_EQ(modules[1].Version, (uint64_t(10) << 48) | (uint64_t(26100) << 16) | 1);
	}
	// a larger element size (a newer layout) is stepped over by its size
	struct Larger {
		RTL_UNLOAD_EVENT_TRACE Trace;
		BYTE More[16];
	} larger[2]{};
	larger[1].Trace = trace[0];
	modules = UnloadedModules::Parse(larger, sizeof(Larger), 2);
	CHECK_EQ(modules.size(), size_t(1));
	CHECK(UnloadedModules::Parse(trace, 8, 4).empty());
	CHECK(UnloadedModules::Parse(nullptr, sizeof(RTL_UNLOAD_EVENT_TRACE), 4).empty());
}

TEST(UnloadedModules_LiveOwnProcess) {
	// a system DLL this process hasn't loaded
	PCWSTR name = nullptr;
	for (auto candidate : { L"msftedit.dll", L"riched20.dll", L"mscms.dll", L"wkscli.dll" })
		if (!::GetModuleHandle(candidate)) {
			name = candidate;
			break;
		}
	CHECK(name != nullptr);
	if (!name)
		return;
	auto module = ::LoadLibraryEx(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	CHECK(module != nullptr);
	if (!module)
		return;
	MODULEINFO info{};
	::GetModuleInformation(::GetCurrentProcess(), module, &info, sizeof(info));
	WCHAR path[MAX_PATH];
	::GetModuleFileName(module, path, _countof(path));
	::FreeLibrary(module);

	auto modules = UnloadedModules::Query(::GetCurrentProcessId());
	CHECK(modules.has_value());
	if (!modules)
		return;
	auto it = std::ranges::find_if(*modules, [&](auto& m) { return _wcsicmp(m.Name.c_str(), name) == 0 && m.BaseAddress == (uint64_t)module; });
	CHECK(it != modules->end());
	if (it == modules->end())
		return;
	CHECK_EQ(it->Size, uint64_t(info.SizeOfImage));
	// the newest unload, as it was just freed
	CHECK(it == modules->begin());

	// the version as the file's resource has it: the product version (msftedit.dll: file 6.2.29698.1000, product
	// 10.0.29698.1000; the trace has the latter)
	DWORD handle = 0;
	auto size = ::GetFileVersionInfoSize(path, &handle);
	std::vector<BYTE> data(size);
	VS_FIXEDFILEINFO* fixed = nullptr;
	UINT len = 0;
	if (size && ::GetFileVersionInfo(path, 0, size, data.data()) && ::VerQueryValue(data.data(), L"\\", (void**)&fixed, &len) && fixed)
		CHECK_EQ(it->Version, (uint64_t(fixed->dwProductVersionMS) << 32) | fixed->dwProductVersionLS);
	else
		CHECK(false);
}

TEST(UnloadedModules_LiveMissingProcess) {
	CHECK(!UnloadedModules::Query(3).has_value());
}
