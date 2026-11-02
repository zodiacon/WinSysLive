#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Autoruns.h>
#include <WinSys/Devices.h>
#include <WinSys/EventLog.h>
#include <WinSys/PowerRequests.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// the smaller pure helpers

namespace {
	std::wstring SystemDirectory() {
		WCHAR path[MAX_PATH];
		::GetSystemDirectoryW(path, _countof(path));
		return path;
	}

	bool SameText(std::wstring const& a, std::wstring const& b) {
		return ::_wcsicmp(a.c_str(), b.c_str()) == 0;
	}
}

TEST(Autoruns_ResolveImage) {
	auto system = SystemDirectory();
	// quoted, with arguments (files every Windows has, Server images too)
	CHECK(SameText(Autoruns::ResolveImage(L"\"" + system + L"\\where.exe\" /R c:\\ x"), system + L"\\where.exe"));
	// environment variables
	CHECK(SameText(Autoruns::ResolveImage(L"%SystemRoot%\\System32\\cmd.exe /c exit"), system + L"\\cmd.exe"));
	// rundll32's DLL, not rundll32 itself
	CHECK(SameText(Autoruns::ResolveImage(L"rundll32.exe shell32.dll,Control_RunDLL"), system + L"\\shell32.dll"));
	// on the search path
	CHECK(SameText(Autoruns::ResolveImage(L"where.exe"), system + L"\\where.exe"));
}

TEST(Devices_ProblemText) {
	CHECK_EQ(Devices::ProblemText(10), std::wstring(L"The device can't start (code 10)"));
	CHECK_EQ(Devices::ProblemText(22), std::wstring(L"Disabled (code 22)"));
	CHECK_EQ(Devices::ProblemText(9999), std::wstring(L"Problem code 9999"));
}

TEST(Devices_LiveTree) {
	// the tree from the root: a parent before its children, the root first
	auto devices = Devices::Enum(false);
	CHECK(devices.has_value());
	if (!devices || devices->empty())
		return;
	CHECK_EQ((*devices)[0].Parent, -1);
	CHECK_EQ((*devices)[0].Depth, 0);
	for (int i = 1; i < (int)devices->size(); i++) {
		auto& d = (*devices)[i];
		CHECK(d.Parent >= 0 && d.Parent < i);
		if (d.Parent >= 0 && d.Parent < i)
			CHECK_EQ(d.Depth, (*devices)[d.Parent].Depth + 1);
	}
}

TEST(Devices_LiveResourcesAndDrivers) {
	// some started PCI device has memory or interrupts, and its stack has a function driver with an image on disk, then
	// the bus driver (pci) at the bottom; a device that isn't there isn't found
	auto devices = Devices::Enum(false);
	CHECK(devices.has_value());
	if (!devices)
		return;
	bool withResources = false, withStack = false;
	for (auto& d : *devices) {
		if (d.Enumerator != L"PCI" || d.Service.empty() || d.HasProblem() || d.IsDisabled())
			continue;
		auto resources = Devices::GetResources(d.InstanceId);
		CHECK(resources.has_value());
		if (resources) {
			for (auto& r : *resources) {
				if (r.Type == DeviceResourceType::Memory || r.Type == DeviceResourceType::LargeMemory || r.Type == DeviceResourceType::Io)
					CHECK(r.End >= r.Start);
				if (r.Type == DeviceResourceType::Memory || r.Type == DeviceResourceType::LargeMemory || r.Type == DeviceResourceType::Interrupt)
					withResources = true;
			}
		}
		auto stack = Devices::GetDriverStack(d.InstanceId);
		CHECK(stack.has_value());
		if (stack && !stack->empty()) {
			auto function = std::ranges::find_if(*stack, [](auto& s) { return s.Role == DriverRole::Function; });
			CHECK(function != stack->end());
			if (function != stack->end() && !function->Path.empty() && ::GetFileAttributesW(function->Path.c_str()) != INVALID_FILE_ATTRIBUTES)
				withStack = true;
			CHECK(stack->back().Role == DriverRole::Bus);
			// the roles in the stack's order, from the top
			for (size_t i = 1; i < stack->size(); i++)
				CHECK((int)(*stack)[i - 1].Role <= (int)(*stack)[i].Role);
		}
	}
	CHECK(withResources);
	CHECK(withStack);
	CHECK(!Devices::GetResources(L"PCI\\NO_SUCH_DEVICE\\0").has_value());
}

TEST(EventLog_LevelNames) {
	CHECK_EQ(std::wstring(EventLog::LevelName(1)), std::wstring(L"Critical"));
	CHECK_EQ(std::wstring(EventLog::LevelName(2)), std::wstring(L"Error"));
	CHECK_EQ(std::wstring(EventLog::LevelName(3)), std::wstring(L"Warning"));
	CHECK_EQ(std::wstring(EventLog::LevelName(0)), std::wstring(L"Information"));
}

TEST(EventLog_LiveQueryIsInReverseLogOrder) {
	// in reverse record order, the log's: not always by time, as a clock set back (a new VM syncing after boot, as on the
	// CI runners) gives later records earlier times; the Events tab sorts the merged logs by time itself
	auto events = EventLog::Query(L"System", 4, 24, 200);
	CHECK(events.has_value());
	if (!events)
		return;
	for (size_t i = 1; i < events->size(); i++)
		CHECK((*events)[i - 1].RecordId > (*events)[i].RecordId);
	for (auto& e : *events)
		CHECK(e.Level <= 4);
}

TEST(EventLog_AuditLevelText) {
	EventLogEntry e;
	e.Level = 0;
	e.Keywords = EventLogEntry::AuditFailureKeyword | 0x8010;
	CHECK(e.IsAuditFailure());
	CHECK_EQ(std::wstring(EventLog::LevelText(e)), std::wstring(L"Audit Failure"));
	e.Keywords = EventLogEntry::AuditSuccessKeyword;
	CHECK(e.IsAuditSuccess() && !e.IsAuditFailure());
	CHECK_EQ(std::wstring(EventLog::LevelText(e)), std::wstring(L"Audit Success"));
	e.Keywords = 0x8000000000000000;	// a classic event's keywords: no audit bits
	e.Level = 2;
	CHECK_EQ(std::wstring(EventLog::LevelText(e)), std::wstring(L"Error"));
}

TEST(EventLog_LiveSecurityQuery) {
	// the Security log needs elevation; with warnings, only failures (and real warnings and errors) come back
	auto events = EventLog::Query(L"Security", 3, 24, 500);
	if (!SecurityHelper::IsRunningElevated()) {
		CHECK(!events.has_value());
		return;
	}
	CHECK(events.has_value());
	if (!events)
		return;
	for (auto& e : *events)
		CHECK(e.IsAuditFailure() || (e.Level >= 1 && e.Level <= 3));
	// with information, the audit successes too (the log has some in any hour: logons, privileges)
	auto all = EventLog::Query(L"Security", 4, 24, 50);
	CHECK(all.has_value());
	if (all) {
		CHECK(!all->empty());
		CHECK(std::ranges::any_of(*all, [](auto& e) { return e.IsAuditSuccess(); }));
	}
}

TEST(PowerRequests_TypeNames) {
	CHECK_EQ(std::wstring(PowerRequests::TypeName(PowerRequestType::Display)), std::wstring(L"Display"));
	CHECK_EQ(std::wstring(PowerRequests::TypeName(PowerRequestType::ActiveLockScreen)), std::wstring(L"Active lock screen"));
}
