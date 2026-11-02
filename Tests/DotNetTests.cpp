#include "pch.h"
#include "TestFramework.h"
#include <WinSys/DotNet.h>

using namespace WinSys;

// DotNetInfo::ApplyRundownEvent on payloads laid out as Microsoft-Windows-DotNETRuntimeRundown's events (verified against
// Windows PowerShell 5.1, the .NET Framework 4 CLR, and pwsh 7 on .NET 10)

namespace {
	struct Payload {
		std::vector<uint8_t> Bytes;

		template<typename T>
		Payload& Put(T value) {
			auto p = reinterpret_cast<uint8_t const*>(&value);
			Bytes.insert(Bytes.end(), p, p + sizeof(T));
			return *this;
		}
		Payload& String(std::wstring const& text) {
			auto p = reinterpret_cast<uint8_t const*>(text.c_str());
			Bytes.insert(Bytes.end(), p, p + (text.size() + 1) * sizeof(WCHAR));
			return *this;
		}
	};

	enum : uint16_t { DCStartComplete = 145, ModuleDCStart = 153, AssemblyDCStart = 155, AppDomainDCStart = 157, RuntimeInformation = 187 };

	Payload RuntimePayload(uint16_t instance, uint16_t sku) {
		Payload p;
		p.Put(instance).Put(sku);
		for (uint16_t v : { 10, 0, 14, 0 })		// BCL
			p.Put(v);
		for (uint16_t v : { 10, 0, 1426, 52326 })	// VM
			p.Put(v);
		p.Put((uint32_t)1).Put((uint8_t)0).String(L"pwsh -NoProfile").Put(GUID{}).String(L"C:\\Program Files\\PowerShell\\7\\coreclr.dll");
		return p;
	}

	bool Apply(uint16_t id, uint8_t version, Payload const& p, DotNetProcessDetails& details, std::vector<uint16_t>& completed) {
		return DotNetInfo::ApplyRundownEvent(id, version, p.Bytes.data(), p.Bytes.size(), details, completed);
	}
}

TEST(DotNet_RuntimeInformation) {
	DotNetProcessDetails d;
	std::vector<uint16_t> completed;
	CHECK(!Apply(RuntimeInformation, 0, RuntimePayload(9, 2), d, completed));
	CHECK_EQ(d.Runtimes.size(), 1u);
	if (!d.Runtimes.empty()) {
		auto& rt = d.Runtimes[0];
		CHECK_EQ(rt.ClrInstanceId, (uint16_t)9);
		CHECK(rt.Core);
		CHECK_EQ(rt.Version, std::wstring(L"10.0.1426.52326"));
		CHECK_EQ(rt.BclVersion, std::wstring(L"10.0.14.0"));
		CHECK_EQ(rt.CommandLine, std::wstring(L"pwsh -NoProfile"));
		CHECK_EQ(rt.RuntimePath, std::wstring(L"C:\\Program Files\\PowerShell\\7\\coreclr.dll"));
	}
	DotNetProcessDetails framework;
	Apply(RuntimeInformation, 0, RuntimePayload(8, 1), framework, completed);
	CHECK(!framework.Runtimes.empty() && !framework.Runtimes[0].Core);
}

TEST(DotNet_AppDomainVersions) {
	DotNetProcessDetails d;
	std::vector<uint16_t> completed;
	Payload v1;
	v1.Put((uint64_t)0x8000668E30).Put((uint32_t)3).String(L"clrhost").Put((uint32_t)1).Put((uint16_t)9);
	Apply(AppDomainDCStart, 1, v1, d, completed);
	Payload v0;
	v0.Put((uint64_t)0x1234).Put((uint32_t)4).String(L"SharedDomain");
	Apply(AppDomainDCStart, 0, v0, d, completed);
	CHECK_EQ(d.AppDomains.size(), 2u);
	if (d.AppDomains.size() == 2) {
		CHECK_EQ(d.AppDomains[0].Name, std::wstring(L"clrhost"));
		CHECK_EQ(d.AppDomains[0].Index, 1u);
		CHECK_EQ(d.AppDomains[0].Flags, 3u);
		CHECK_EQ(d.AppDomains[1].Name, std::wstring(L"SharedDomain"));
		CHECK_EQ(d.AppDomains[1].Index, 0u);
	}
}

TEST(DotNet_AssemblyVersionsAndBindingId) {
	// version 1 has a BindingID before the flags; version 0 doesn't
	DotNetProcessDetails d;
	std::vector<uint16_t> completed;
	Payload v1;
	v1.Put((uint64_t)0xA1).Put((uint64_t)0xD1).Put((uint64_t)0xB1).Put((uint32_t)0x10)
		.String(L"System.Private.CoreLib, Version=10.0.0.0, Culture=neutral, PublicKeyToken=7cec85d7bea7798e").Put((uint16_t)9);
	Apply(AssemblyDCStart, 1, v1, d, completed);
	Payload v0;
	v0.Put((uint64_t)0xA2).Put((uint64_t)0xD2).Put((uint32_t)0x5).String(L"System.Data, Version=4.0.0.0");
	Apply(AssemblyDCStart, 0, v0, d, completed);
	CHECK_EQ(d.Assemblies.size(), 2u);
	if (d.Assemblies.size() == 2) {
		CHECK_EQ(d.Assemblies[0].Id, (uint64_t)0xA1);
		CHECK_EQ(d.Assemblies[0].AppDomainId, (uint64_t)0xD1);
		CHECK_EQ(d.Assemblies[0].Flags, 0x10u);
		CHECK(d.Assemblies[0].FullName.starts_with(L"System.Private.CoreLib, Version=10.0.0.0"));
		CHECK_EQ(d.Assemblies[1].Flags, 0x5u);
		CHECK_EQ(d.Assemblies[1].FullName, std::wstring(L"System.Data, Version=4.0.0.0"));
	}
}

TEST(DotNet_Module) {
	DotNetProcessDetails d;
	std::vector<uint16_t> completed;
	Payload p;
	p.Put((uint64_t)0x11).Put((uint64_t)0xA1).Put((uint32_t)0x1B).Put((uint32_t)0)
		.String(L"C:\\Windows\\Microsoft.Net\\assembly\\GAC_64\\System.Data\\System.Data.dll").String(L"C:\\Windows\\assembly\\NativeImages\\System.Data.ni.dll");
	Apply(ModuleDCStart, 2, p, d, completed);
	CHECK_EQ(d.Modules.size(), 1u);
	if (!d.Modules.empty()) {
		CHECK_EQ(d.Modules[0].AssemblyId, (uint64_t)0xA1);
		CHECK_EQ(d.Modules[0].Flags, 0x1Bu);
		CHECK(d.Modules[0].NativePath.ends_with(L"System.Data.ni.dll"));
	}
}

TEST(DotNet_CompleteWhenEveryRuntimeIs) {
	DotNetProcessDetails d;
	std::vector<uint16_t> completed;
	Apply(RuntimeInformation, 0, RuntimePayload(8, 1), d, completed);
	Apply(RuntimeInformation, 0, RuntimePayload(9, 1), d, completed);
	Payload nine;
	nine.Put((uint16_t)9);
	CHECK(!Apply(DCStartComplete, 1, nine, d, completed));	// runtime 8 still running down
	CHECK(!d.Complete);
	Payload eight;
	eight.Put((uint16_t)8);
	CHECK(Apply(DCStartComplete, 1, eight, d, completed));
	CHECK(d.Complete);
	// version 0 carries no instance: the only runtime
	DotNetProcessDetails single;
	std::vector<uint16_t> none;
	CHECK(Apply(DCStartComplete, 0, Payload{}, single, none));
}

TEST(DotNet_TruncatedPayloadsAddNothing) {
	DotNetProcessDetails d;
	std::vector<uint16_t> completed;
	Payload cut;
	cut.Put((uint64_t)0xA1).Put((uint64_t)0xD1);	// no flags, no name
	Apply(AssemblyDCStart, 0, cut, d, completed);
	auto runtime = RuntimePayload(9, 2);
	runtime.Bytes.resize(runtime.Bytes.size() - 4);	// the path's end cut off
	Apply(RuntimeInformation, 0, runtime, d, completed);
	Payload unterminated;
	unterminated.Put((uint64_t)1).Put((uint32_t)0).Put((wchar_t)L'x');	// a name without its terminator
	Apply(AppDomainDCStart, 0, unterminated, d, completed);
	CHECK(d.Assemblies.empty());
	CHECK(d.Runtimes.empty());
	CHECK(d.AppDomains.empty());
}

namespace {
	// a payload as the runtime writes it
	struct PayloadWriter {
		std::vector<uint8_t> Bytes;
		template<typename T>
		void Put(T value) {
			auto p = reinterpret_cast<uint8_t const*>(&value);
			Bytes.insert(Bytes.end(), p, p + sizeof(T));
		}
		void String(std::wstring const& text) {
			Put<uint32_t>((uint32_t)text.size() + 1);
			for (auto c : text)
				Put<wchar_t>(c);
			Put<wchar_t>(0);
		}
	};
}

TEST(DotNet_ParseProcessInfo) {
	PayloadWriter w;
	w.Put<uint64_t>(1234);
	GUID cookie{ 0x11223344, 0x5566, 0x7788, { 1, 2, 3, 4, 5, 6, 7, 8 } };
	w.Put(cookie);
	w.String(L"\"C:\\app\\app.exe\" --run");
	w.String(L"Windows");
	w.String(L"x64");
	w.String(L"app");
	w.String(L"8.0.10+81cabf2857");
	auto info = DotNetInfo::ParseProcessInfo(w.Bytes.data(), w.Bytes.size(), true);
	CHECK(info.has_value());
	if (info) {
		CHECK_EQ(info->ProcessId, (uint64_t)1234);
		CHECK(info->RuntimeCookie == cookie);
		CHECK_EQ(info->CommandLine, std::wstring(L"\"C:\\app\\app.exe\" --run"));
		CHECK_EQ(info->Architecture, std::wstring(L"x64"));
		CHECK_EQ(info->EntryAssembly, std::wstring(L"app"));
		CHECK_EQ(info->ClrVersion, std::wstring(L"8.0.10+81cabf2857"));
	}
	// ProcessInfo (v1) stops after the architecture
	auto v1 = DotNetInfo::ParseProcessInfo(w.Bytes.data(), w.Bytes.size(), false);
	CHECK(v1.has_value() && v1->EntryAssembly.empty() && v1->Os == L"Windows");
	// cut anywhere before its end: nothing, never a read past it
	for (size_t cut = 0; cut < w.Bytes.size(); cut += 3)
		CHECK(!DotNetInfo::ParseProcessInfo(w.Bytes.data(), cut, true).has_value());
	// a string's count past the payload
	PayloadWriter bad;
	bad.Put<uint64_t>(1);
	bad.Put(cookie);
	bad.Put<uint32_t>(0x7FFFFFFF);
	CHECK(!DotNetInfo::ParseProcessInfo(bad.Bytes.data(), bad.Bytes.size(), false).has_value());
}

TEST(DotNet_GcEvents) {
	DotNetGcStats stats;
	// a generation 1 collection: GCStart V2 (count, depth, reason, type, instance, sequence)
	PayloadWriter start;
	start.Put<uint32_t>(7);
	start.Put<uint32_t>(1);
	start.Put<uint32_t>(0);
	start.Put<uint32_t>(0);
	start.Put<uint16_t>(9);
	start.Put<uint64_t>(0);
	CHECK(DotNetGcMonitor::ApplyGcEvent(1, 2, start.Bytes.data(), start.Bytes.size(), 0, stats));
	CHECK_EQ(stats.Collections, 1u);
	CHECK_EQ(stats.ByGeneration[1], 1u);
	CHECK_EQ(stats.LastGeneration, 1u);
	// the pause: suspend at 10,000,000, restart 25 ms later
	CHECK(DotNetGcMonitor::ApplyGcEvent(9, 1, nullptr, 0, 10000000, stats));
	CHECK(DotNetGcMonitor::ApplyGcEvent(3, 1, nullptr, 0, 10250000, stats));
	CHECK_EQ(stats.LastPause, (uint64_t)250000);
	CHECK_EQ(stats.MaxPause, (uint64_t)250000);
	// a restart without its suspend counts nothing
	CHECK(!DotNetGcMonitor::ApplyGcEvent(3, 1, nullptr, 0, 20000000, stats));
	CHECK_EQ(stats.TotalPause, (uint64_t)250000);
	// GCHeapStats V2: the sizes (and the pinned object heap's)
	PayloadWriter heap;
	for (uint64_t g = 0; g < 4; g++) {
		heap.Put<uint64_t>((g + 1) * 1000);
		heap.Put<uint64_t>(0);
	}
	heap.Put<uint64_t>(0);
	heap.Put<uint64_t>(0);
	heap.Put<uint32_t>(5);	// pinned objects
	heap.Put<uint32_t>(0);
	heap.Put<uint32_t>(42);	// GC handles
	heap.Put<uint16_t>(9);
	heap.Put<uint64_t>(500);	// pinned object heap
	heap.Put<uint64_t>(0);
	CHECK(DotNetGcMonitor::ApplyGcEvent(4, 2, heap.Bytes.data(), heap.Bytes.size(), 0, stats));
	CHECK(stats.HasHeap);
	CHECK_EQ(stats.GenerationSize[0], (uint64_t)1000);
	CHECK_EQ(stats.LohSize, (uint64_t)4000);
	CHECK_EQ(stats.PohSize, (uint64_t)500);
	CHECK_EQ(stats.GcHandles, 42u);
	CHECK_EQ(stats.HeapSize(), (uint64_t)(1000 + 2000 + 3000 + 4000 + 500));
	// cut short: nothing changes
	auto before = stats.Collections;
	CHECK(!DotNetGcMonitor::ApplyGcEvent(1, 2, start.Bytes.data(), 6, 0, stats));
	CHECK(!DotNetGcMonitor::ApplyGcEvent(4, 2, heap.Bytes.data(), 40, 0, stats));
	CHECK_EQ(stats.Collections, before);
}

TEST(DotNet_LiveCoreProcessInfo) {
	// a .NET (Core) process of this user that has a diagnostics pipe: PowerShell 7, if one runs
	for (auto pid : DotNetInfo::GetCoreRuntimeProcesses()) {
		wil::unique_handle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
		if (!process)
			continue;
		auto info = DotNetInfo::QueryCoreProcessInfo(pid);
		if (!info)
			continue;	// another user's, or one that didn't answer
		CHECK_EQ(info->ProcessId, (uint64_t)pid);
		CHECK(!info->CommandLine.empty());
		CHECK(!info->Os.empty());
		return;
	}
}

TEST(DotNet_LiveGcMonitor) {
	// a PowerShell 7 of ours that forces a collection two seconds in (pwsh: a .NET process; skipped without it or
	// without elevation)
	WCHAR pwsh[MAX_PATH];
	if (!::SearchPath(nullptr, L"pwsh.exe", nullptr, _countof(pwsh), pwsh, nullptr))
		return;
	std::wstring cmd = L"\"" + std::wstring(pwsh) + L"\" -NoProfile -NonInteractive -Command \"Start-Sleep 2; [GC]::Collect(); Start-Sleep 2\"";
	STARTUPINFO si{ sizeof(si) };
	PROCESS_INFORMATION pi{};
	if (!::CreateProcess(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		return;
	wil::unique_handle process(pi.hProcess), thread(pi.hThread);
	DotNetGcMonitor monitor;
	if (!monitor.Start(pi.dwProcessId, L"WinSysLive Tests GC ")) {
		::TerminateProcess(pi.hProcess, 0);
		return;	// not elevated
	}
	::WaitForSingleObject(pi.hProcess, 15000);
	monitor.Flush();
	::Sleep(1500);
	auto stats = monitor.GetStats();
	monitor.Stop();
	CHECK(stats.Collections >= 1);
	CHECK(stats.ByGeneration[2] >= 1);	// GC.Collect() is a full collection
	CHECK(stats.HasHeap && stats.HeapSize() > 0);
	CHECK(stats.MaxPause > 0);
}
