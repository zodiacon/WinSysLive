#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Autoruns.h>
#include <WinSys/Devices.h>
#include <WinSys/EventLog.h>
#include <WinSys/PEFile.h>
#include <WinSys/FileVersion.h>
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

TEST(EventLog_ParseEventData) {
	// as EvtRender gives it: single quotes, one line; named, empty and entity-encoded values
	std::wstring xml = L"<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='Microsoft-Windows-Security-Auditing'/>"
		L"<EventID>4625</EventID><Data>not this</Data></System><EventData><Data Name='SubjectUserSid'>S-1-5-18</Data>"
		L"<Data Name='TargetUserName'>bob</Data><Data Name='TargetDomainName'>ZODIAC</Data><Data Name='Status'>0xc000006d</Data>"
		L"<Data Name='Empty'/><Data Name=\"Quoted\">a &amp; b &lt;c&gt; &#65;&#x42;</Data><Data Name='IpAddress'>10.0.0.5</Data>"
		L"</EventData></Event>";
	auto data = EventLog::ParseData(xml);
	CHECK_EQ(data.size(), (size_t)7);
	if (data.size() == 7) {
		CHECK_EQ(data[0].Name, std::wstring(L"SubjectUserSid"));
		CHECK_EQ(data[1].Value, std::wstring(L"bob"));
		CHECK_EQ(data[4].Name, std::wstring(L"Empty"));
		CHECK(data[4].Value.empty());
		CHECK_EQ(data[5].Name, std::wstring(L"Quoted"));
		CHECK_EQ(data[5].Value, std::wstring(L"a & b <c> AB"));
		CHECK_EQ(data[6].Value, std::wstring(L"10.0.0.5"));
	}
	// the classic logs' unnamed items
	data = EventLog::ParseData(L"<Event><System/><EventData><Data>first</Data><Data>second</Data><Binary>00</Binary></EventData></Event>");
	CHECK_EQ(data.size(), (size_t)2);
	if (data.size() == 2) {
		CHECK(data[0].Name.empty());
		CHECK_EQ(data[1].Value, std::wstring(L"second"));
	}
	// UserData: the elements under its first child (1102, the log cleared)
	data = EventLog::ParseData(L"<Event><System/><UserData><LogFileCleared xmlns='http://manifests.microsoft.com/win/2004/08/windows/eventlog'>"
		L"<SubjectUserSid>S-1-5-21-1</SubjectUserSid><SubjectUserName>zodia</SubjectUserName><SubjectDomainName>ZODIAC</SubjectDomainName>"
		L"<SubjectLogonId>0x57d1a2</SubjectLogonId></LogFileCleared></UserData></Event>");
	CHECK_EQ(data.size(), (size_t)4);
	if (data.size() == 4) {
		CHECK_EQ(data[1].Name, std::wstring(L"SubjectUserName"));
		CHECK_EQ(data[1].Value, std::wstring(L"zodia"));
	}
	// none, and broken XML: nothing, no crash
	CHECK(EventLog::ParseData(L"<Event><System/><EventData/></Event>").empty());
	CHECK(EventLog::ParseData(L"<Event><System/></Event>").empty());
	CHECK(EventLog::ParseData(L"<Event><EventData><Data Name='x'>unterminated").empty());
	CHECK(EventLog::ParseData(L"").empty());
}

TEST(EventLog_LiveEventData) {
	// the newest System events' data, read again by record ID: most events have some fields
	auto events = EventLog::Query(L"System", 4, 24 * 7, 20);
	CHECK(events.has_value());
	if (!events || events->empty())
		return;
	int withData = 0;
	for (auto& e : *events) {
		auto data = EventLog::GetData(L"System", e.RecordId);
		CHECK(data.has_value());
		if (data && !data->empty())
			withData++;
	}
	CHECK(withData > 0);
	// read with the query: the same fields as found again by record ID; none asked for, none read
	auto read = EventLog::Query(L"System", 4, 24 * 7, 20, EventDataRead::All);
	CHECK(read.has_value());
	if (read)
		for (auto& e : *read) {
			auto again = EventLog::GetData(L"System", e.RecordId);
			if (!again)
				continue;	// gone since (the log wrapped)
			CHECK_EQ(e.Data.size(), again->size());
			if (e.Data.size() == again->size())
				for (size_t i = 0; i < e.Data.size(); i++)
					CHECK(e.Data[i].Name == (*again)[i].Name && e.Data[i].Value == (*again)[i].Value);
		}
	auto none = EventLog::Query(L"System", 4, 24 * 7, 20);
	if (none)
		for (auto& e : *none)
			CHECK(e.Data.empty());
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

namespace {
	std::wstring OwnPath() {
		WCHAR path[MAX_PATH];
		::GetModuleFileName(nullptr, path, _countof(path));
		return path;
	}
}

TEST(PEFile_OwnImage) {
	// the test program: x64 PE32+, an executable with .text, importing KERNEL32.dll (CreateFileW, by name)
	auto pe = PEFile::Load(OwnPath());
	CHECK(pe.has_value());
	if (!pe)
		return;
	auto& h = pe->GetHeaders();
	CHECK(h.Is64);
	CHECK_EQ(h.Machine, (uint16_t)IMAGE_FILE_MACHINE_AMD64);
	CHECK(h.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE);
	CHECK(!(h.Characteristics & IMAGE_FILE_DLL));
	CHECK_EQ(h.Subsystem, (uint16_t)IMAGE_SUBSYSTEM_WINDOWS_CUI);
	CHECK_EQ(h.Directories.size(), (size_t)16);
	CHECK(h.EntryPoint != 0);
	CHECK(std::ranges::any_of(pe->GetSections(), [](auto& s) { return s.Name == ".text" && (s.Characteristics & IMAGE_SCN_CNT_CODE); }));
	auto imports = pe->GetImports();
	auto kernel32 = std::ranges::find_if(imports, [](auto& l) { return ::_stricmp(l.Name.c_str(), "KERNEL32.dll") == 0; });
	CHECK(kernel32 != imports.end());
	if (kernel32 != imports.end()) {
		CHECK(!kernel32->Delayed);
		CHECK(std::ranges::any_of(kernel32->Functions, [](auto& f) { return f.Name == "CreateFileW"; }));
	}
	CHECK(!pe->GetExports().has_value());
	// the entry point is in a section's raw data
	CHECK(pe->RvaToOffset(h.EntryPoint).has_value());
	// built with /DEBUG: a CodeView entry naming Tests.pdb, with a GUID and an age
	auto cv = pe->GetCodeView();
	CHECK(cv.has_value());
	if (cv) {
		CHECK(cv->PdbPath.size() > 9 && ::_stricmp(cv->PdbPath.c_str() + cv->PdbPath.size() - 9, "Tests.pdb") == 0);
		CHECK(cv->Age >= 1);
		CHECK(cv->Signature != GUID{});
	}
	CHECK(std::ranges::any_of(pe->GetDebugEntries(), [](auto& e) { return e.Type == IMAGE_DEBUG_TYPE_CODEVIEW; }));
}

TEST(PEFile_LiveKernel32Debug) {
	// a system DLL's CodeView names its PDB (as the symbol server knows it)
	WCHAR path[MAX_PATH];
	::GetSystemDirectory(path, _countof(path));
	auto pe = PEFile::Load(std::wstring(path) + L"\\kernel32.dll");
	if (!pe)
		return;
	auto cv = pe->GetCodeView();
	CHECK(cv.has_value());
	if (cv)
		CHECK(::_stricmp(cv->PdbPath.c_str(), "kernel32.pdb") == 0);
}

TEST(PEFile_LiveKernel32) {
	// a DLL with exports, by name and ordinal, and forwarders ("NTDLL.Rtl...")
	WCHAR path[MAX_PATH];
	::GetSystemDirectory(path, _countof(path));
	auto pe = PEFile::Load(std::wstring(path) + L"\\kernel32.dll");
	CHECK(pe.has_value());
	if (!pe)
		return;
	CHECK(pe->GetHeaders().Characteristics & IMAGE_FILE_DLL);
	auto exports = pe->GetExports();
	CHECK(exports.has_value());
	if (!exports)
		return;
	CHECK(::_stricmp(exports->Name.c_str(), "KERNEL32.dll") == 0);
	CHECK(exports->Functions.size() > 1000);
	auto create = std::ranges::find_if(exports->Functions, [](auto& f) { return f.Name == "CreateFileW"; });
	CHECK(create != exports->Functions.end());
	if (create != exports->Functions.end())
		CHECK(create->Forwarder.empty() && create->Rva != 0);
	auto forwarded = std::ranges::find_if(exports->Functions, [](auto& f) { return f.Name == "AcquireSRWLockExclusive"; });
	CHECK(forwarded != exports->Functions.end());
	if (forwarded != exports->Functions.end())
		CHECK(::_strnicmp(forwarded->Forwarder.c_str(), "NTDLL.", 6) == 0);
	// ordinals in order, from the base
	for (size_t i = 1; i < exports->Functions.size(); i++)
		CHECK(exports->Functions[i - 1].Ordinal < exports->Functions[i].Ordinal);
}

TEST(PEFile_Malformed) {
	// not PE images, and images whose tables point outside the file: nothing, or what fits; never a crash
	CHECK(!PEFile::Parse({}).has_value());
	CHECK(!PEFile::Parse(std::vector<uint8_t>(64, 0)).has_value());
	auto own = PEFile::Load(OwnPath());
	if (!own)
		return;
	// the real file, cut at every few hundred bytes: either no headers or a parse that stays inside
	std::vector<uint8_t> bytes;
	{
		auto file = ::CreateFile(OwnPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		LARGE_INTEGER size;
		::GetFileSizeEx(file, &size);
		bytes.resize((size_t)size.QuadPart);
		DWORD read;
		::ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
		::CloseHandle(file);
	}
	for (size_t cut = 0; cut < bytes.size(); cut += bytes.size() / 97 + 1) {
		auto pe = PEFile::Parse(std::vector<uint8_t>(bytes.begin(), bytes.begin() + cut));
		if (pe) {
			(void)pe->GetImports();
			(void)pe->GetExports();
		}
	}
	// e_lfanew past the end, a bad magic, a huge section count and directory sizes
	auto broken = bytes;
	reinterpret_cast<IMAGE_DOS_HEADER*>(broken.data())->e_lfanew = 0x7FFFFFF0;
	CHECK(!PEFile::Parse(broken).has_value());
	broken = bytes;
	auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(broken.data() + reinterpret_cast<IMAGE_DOS_HEADER*>(broken.data())->e_lfanew);
	nt->OptionalHeader.Magic = 0x1234;
	CHECK(!PEFile::Parse(broken).has_value());
	broken = bytes;
	nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(broken.data() + reinterpret_cast<IMAGE_DOS_HEADER*>(broken.data())->e_lfanew);
	nt->FileHeader.NumberOfSections = 0xFFFF;
	nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0xFFFFFFF0;
	nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x10;
	nt->OptionalHeader.NumberOfRvaAndSizes = 0xFFFFFFFF;
	auto pe = PEFile::Parse(broken);
	CHECK(pe.has_value());
	if (pe) {
		CHECK(pe->GetSections().size() <= 96);
		CHECK(pe->GetHeaders().Directories.size() <= 16);
		(void)pe->GetImports();
		(void)pe->GetExports();
	}
}

TEST(PEFile_Names) {
	CHECK_EQ(std::wstring(PEFile::MachineName(IMAGE_FILE_MACHINE_ARM64)), std::wstring(L"ARM64"));
	CHECK_EQ(std::wstring(PEFile::SubsystemName(IMAGE_SUBSYSTEM_NATIVE)), std::wstring(L"Native"));
	CHECK_EQ(std::wstring(PEFile::DirectoryName(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)), std::wstring(L"Delay Import"));
	CHECK_EQ(PEFile::DllCharacteristicsText(IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT),
		std::wstring(L"ASLR, DEP"));
	CHECK_EQ(PEFile::SectionCharacteristicsText(IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE), std::wstring(L"RX: Code"));
}

TEST(PEFile_AppDelayImports) {
	// the app beside the tests (the solution builds both into the same folder) delay-loads dbghelp.dll
	auto path = OwnPath();
	path = path.substr(0, path.rfind(L'\\') + 1) + L"WinSysLive.exe";
	if (::GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	auto pe = PEFile::Load(path);
	CHECK(pe.has_value());
	if (!pe)
		return;
	auto imports = pe->GetImports();
	auto dbghelp = std::ranges::find_if(imports, [](auto& l) { return ::_stricmp(l.Name.c_str(), "dbghelp.dll") == 0; });
	CHECK(dbghelp != imports.end());
	if (dbghelp != imports.end()) {
		CHECK(dbghelp->Delayed);
		CHECK(!dbghelp->Functions.empty());
		CHECK(std::ranges::all_of(dbghelp->Functions, [](auto& f) { return f.ByOrdinal || !f.Name.empty(); }));
	}
}

TEST(PEFile_AppResources) {
	// the app beside the tests: an icon group, a version and a manifest (whose text is XML)
	auto path = OwnPath();
	path = path.substr(0, path.rfind(L'\\') + 1) + L"WinSysLive.exe";
	if (::GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	auto pe = PEFile::Load(path);
	CHECK(pe.has_value());
	if (!pe)
		return;
	auto resources = pe->GetResources();
	auto has = [&](uint16_t type) { return std::ranges::any_of(resources, [&](auto& r) { return r.TypeId == type; }); };
	CHECK(has(14));	// icon group
	CHECK(has(16));	// version
	auto manifest = std::ranges::find_if(resources, [](auto& r) { return r.TypeId == 24; });
	CHECK(manifest != resources.end());
	if (manifest != resources.end()) {
		auto data = pe->GetResourceData(*manifest);
		CHECK(data.size() == manifest->Size);
		std::string text(data.begin(), data.end());
		CHECK(text.find("assembly") != std::string::npos);
	}
	CHECK(std::wstring(PEFile::ResourceTypeName(24)) == L"Manifest");
	// each icon group's icons are there, as RT_ICON resources of the sizes the group says
	for (auto& group : resources) {
		if (group.TypeId != 14)
			continue;
		auto icons = pe->GetIconGroup(group);
		CHECK(!icons.empty());
		for (auto& icon : icons) {
			CHECK(icon.Width >= 16 && icon.Width <= 256);
			auto found = pe->FindIcon(resources, icon.IconId, group.Language);
			CHECK(found.has_value());
			if (found)
				CHECK_EQ(found->Size, icon.Size);
		}
	}
	CHECK(!pe->GetDotNet().has_value());
}

TEST(PEFile_LiveChecksumAndVersion) {
	// a system DLL's header checksum is the computed one; a byte changed changes it
	WCHAR path[MAX_PATH];
	::GetSystemDirectory(path, _countof(path));
	std::wstring kernel32 = std::wstring(path) + L"\\kernel32.dll";
	auto pe = PEFile::Load(kernel32);
	CHECK(pe.has_value());
	if (!pe)
		return;
	auto checksum = pe->ComputeChecksum();
	CHECK(pe->GetHeaders().CheckSum != 0);
	CHECK_EQ(checksum, pe->GetHeaders().CheckSum);
	// the same bytes with one changed (past the headers)
	auto file = ::CreateFile(kernel32.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	LARGE_INTEGER size;
	::GetFileSizeEx(file, &size);
	std::vector<uint8_t> bytes((size_t)size.QuadPart);
	DWORD read;
	::ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
	::CloseHandle(file);
	bytes[bytes.size() / 2] ^= 0x5A;
	auto changed = PEFile::Parse(std::move(bytes));
	CHECK(changed.has_value());
	if (changed)
		CHECK(changed->ComputeChecksum() != checksum);
	// its version resource in full
	auto version = FileVersion::ReadDetails(kernel32);
	CHECK(version.has_value());
	if (version) {
		CHECK(version->FileVersion != 0);
		auto company = std::ranges::find_if(version->Strings, [](auto& s) { return s.first == L"CompanyName"; });
		CHECK(company != version->Strings.end());
		if (company != version->Strings.end())
			CHECK(company->second.find(L"Microsoft") != std::wstring::npos);
		// 10.0, or 6.2 for this unmanifested test program (GetFileVersionInfo's version lie for system DLLs; the app's
		// manifest declares Windows 10 and sees the truth)
		auto text = FileVersion::VersionText(version->FileVersion);
		CHECK(text.starts_with(L"10.0.") || text.starts_with(L"6.2."));
		CHECK(std::ranges::count(text, L'.') == 3);
	}
}

TEST(PEFile_LiveDotNet) {
	// the .NET Framework's System.dll: managed, IL only, metadata for v4.0.30319
	WCHAR windows[MAX_PATH];
	::GetWindowsDirectory(windows, _countof(windows));
	auto path = std::wstring(windows) + L"\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
	if (::GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	auto pe = PEFile::Load(path);
	CHECK(pe.has_value());
	if (!pe)
		return;
	auto net = pe->GetDotNet();
	CHECK(net.has_value());
	if (net) {
		CHECK(net->IsILOnly());
		CHECK_EQ(net->MetadataVersion, std::string("v4.0.30319"));
		CHECK(net->RuntimeMajor == 2);
		CHECK(PEFile::DotNetFlagsText(net->Flags).find(L"IL only") != std::wstring::npos);
	}
}
