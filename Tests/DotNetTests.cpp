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
