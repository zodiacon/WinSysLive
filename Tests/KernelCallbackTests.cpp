#include "pch.h"
#include "TestFramework.h"
#include <WinSys/KernelCallbacks.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// KernelCallbacks: the registration functions' kinds (the numbered WFP ones by their stem), the kinds' wording, and live
// reads: the Filter Manager's filters (elevated only) and the drivers whose imports register callbacks

TEST(KernelCallbacks_KindOf) {
	CHECK(KernelCallbacks::KindOf("PsSetCreateProcessNotifyRoutine") == CallbackKind::Process);
	CHECK(KernelCallbacks::KindOf("PsSetCreateProcessNotifyRoutineEx2") == CallbackKind::Process);
	CHECK(KernelCallbacks::KindOf("PsSetCreateThreadNotifyRoutineEx") == CallbackKind::Thread);
	CHECK(KernelCallbacks::KindOf("PsSetLoadImageNotifyRoutine") == CallbackKind::ImageLoad);
	CHECK(KernelCallbacks::KindOf("CmRegisterCallbackEx") == CallbackKind::Registry);
	CHECK(KernelCallbacks::KindOf("ObRegisterCallbacks") == CallbackKind::Object);
	CHECK(KernelCallbacks::KindOf("FltRegisterFilter") == CallbackKind::FileSystem);
	CHECK(KernelCallbacks::KindOf("FwpsCalloutRegister0") == CallbackKind::Network);
	CHECK(KernelCallbacks::KindOf("FwpsCalloutRegister3") == CallbackKind::Network);
	// the stem alone, a non-number after it, and the unregistration functions aren't
	CHECK(KernelCallbacks::KindOf("FwpsCalloutRegister") == CallbackKind::None);
	CHECK(KernelCallbacks::KindOf("FwpsCalloutRegisterX") == CallbackKind::None);
	CHECK(KernelCallbacks::KindOf("FwpsCalloutUnregisterById0") == CallbackKind::None);
	CHECK(KernelCallbacks::KindOf("PsRemoveCreateThreadNotifyRoutine") == CallbackKind::None);
	CHECK(KernelCallbacks::KindOf("CmUnRegisterCallback") == CallbackKind::None);
	CHECK(KernelCallbacks::KindOf("ExAllocatePool2") == CallbackKind::None);
	CHECK(KernelCallbacks::KindOf("") == CallbackKind::None);
	// a prefix of a longer name isn't it either
	CHECK(KernelCallbacks::KindOf("PsSetCreateProcessNotify") == CallbackKind::None);
	CHECK(std::wstring(KernelCallbacks::Describe("ObRegisterCallbacks")).find(L"Handle") != std::wstring::npos);
	CHECK(std::wstring(KernelCallbacks::Describe("ExAllocatePool2")).empty());
}

TEST(KernelCallbacks_KindsText) {
	CHECK_EQ(KernelCallbacks::KindsText(CallbackKind::None), std::wstring());
	CHECK_EQ(KernelCallbacks::KindsText(CallbackKind::Registry), std::wstring(L"Registry"));
	CHECK_EQ(KernelCallbacks::KindsText(CallbackKind::Registry | CallbackKind::Process | CallbackKind::LogonSession),
		std::wstring(L"Process, Registry, Logon Session"));
	CHECK_EQ(KernelCallbacks::KindsText(CallbackKind::ImageLoad | CallbackKind::PlugAndPlay), std::wstring(L"Image Load, Plug and Play"));
	CHECK_EQ(std::wstring(KernelCallbacks::FileSystemName(2)), std::wstring(L"NTFS"));	// FLT_FSTYPE_NTFS
}

TEST(KernelCallbacks_LiveMinifilters) {
	auto filters = KernelCallbacks::EnumMinifilters();
	if (!SecurityHelper::IsRunningElevated()) {
		CHECK(!filters);
		return;
	}
	CHECK(filters.has_value());
	if (!filters)
		return;
	CHECK(!filters->empty());
	// highest altitude first
	for (size_t i = 1; i < filters->size(); i++)
		CHECK(::wcstod((*filters)[i - 1].Altitude.c_str(), nullptr) >= ::wcstod((*filters)[i].Altitude.c_str(), nullptr));
	// FileInfo (altitude 40500) is in every Windows since 8, on the system volume
	auto it = std::ranges::find_if(*filters, [](auto& f) { return ::_wcsicmp(f.Name.c_str(), L"FileInfo") == 0; });
	CHECK(it != filters->end());
	if (it == filters->end())
		return;
	CHECK_EQ(it->Altitude, std::wstring(L"40500"));
	CHECK(!it->Legacy);
	CHECK(it->ImagePath.ends_with(L"\\fileinfo.sys") || it->ImagePath.ends_with(L"\\FileInfo.sys"));
	CHECK_EQ((size_t)it->InstanceCount, it->Instances.size());
	WCHAR windows[MAX_PATH];
	::GetWindowsDirectory(windows, _countof(windows));
	std::wstring drive(windows, 2);
	CHECK(std::ranges::any_of(it->Instances, [&](auto& i) { return ::_wcsicmp(i.DosVolume.c_str(), drive.c_str()) == 0 && !i.Name.empty(); }));
}

TEST(KernelCallbacks_LiveCallbackDrivers) {
	auto result = KernelCallbacks::EnumCallbackDrivers();
	CHECK(result.Modules > 50);
	CHECK(!result.Drivers.empty());
	// a minifilter imports FltRegisterFilter: fileinfo.sys is always loaded
	auto it = std::ranges::find_if(result.Drivers, [](auto& d) { return ::_wcsicmp(d.Name.c_str(), L"fileinfo.sys") == 0; });
	CHECK(it != result.Drivers.end());
	if (it != result.Drivers.end())
		CHECK((it->Kinds & CallbackKind::FileSystem) == CallbackKind::FileSystem);
	// some driver watches processes (cng.sys, ahcache.sys, the antimalware's), and every listed driver has an import
	CHECK(std::ranges::any_of(result.Drivers, [](auto& d) { return (d.Kinds & CallbackKind::Process) != CallbackKind::None; }));
	for (auto& d : result.Drivers) {
		CHECK(!d.Imports.empty());
		for (auto& i : d.Imports)
			CHECK(i.Kind != CallbackKind::None && (d.Kinds & i.Kind) == i.Kind);
	}
	// nearly every module's file is readable (not elevated: still readable, the paths come without elevation)
	CHECK(result.Unreadable.size() * 10 < result.Modules);
}
