#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Com.h>
#include <WinSys/Ipc.h>
#include <WinSys/ProcessManager.h>
#include <WinSys/Processes.h>
#include <WinSys/EventLog.h>

using namespace WinSys;

// ComServers: the command lines of COM server processes (as Windows 11's dllhost, RuntimeBroker, explorer's factory,
// Word's automation and packaged apps' WinRT servers have them), LocalServer32 values, and the running processes'
// identities (live)

TEST(Com_ParseCommandLine) {
	std::wstring id;
	CHECK(ComServers::ParseCommandLine(L"C:\\WINDOWS\\system32\\DllHost.exe /Processid:{3EB3C877-1F16-487C-9050-104DBCD66683}", id) == ComServerKind::Surrogate);
	CHECK_EQ(id, std::wstring(L"{3EB3C877-1F16-487C-9050-104DBCD66683}"));
	// quoted, the ID in lower case: upper-cased
	CHECK(ComServers::ParseCommandLine(L"\"C:\\WINDOWS\\system32\\DllHost.exe\" /processid:{dfb65c4c-b34f-435d-afe9-a86218684aa8}", id) == ComServerKind::Surrogate);
	CHECK_EQ(id, std::wstring(L"{DFB65C4C-B34F-435D-AFE9-A86218684AA8}"));
	CHECK(ComServers::ParseCommandLine(L"C:\\Windows\\System32\\RuntimeBroker.exe -Embedding", id) == ComServerKind::LocalServer);
	CHECK(id.empty());
	CHECK(ComServers::ParseCommandLine(L"C:\\WINDOWS\\system32\\SearchIndexer.exe /Embedding", id) == ComServerKind::LocalServer);
	CHECK(ComServers::ParseCommandLine(L"\"C:\\Program Files\\Microsoft Office\\Root\\Office16\\WINWORD.EXE\" /Automation -Embedding", id) == ComServerKind::LocalServer);
	CHECK(ComServers::ParseCommandLine(L"C:\\WINDOWS\\explorer.exe /factory,{ceff45ee-c862-41de-aee2-a022c81eda92} -Embedding", id) == ComServerKind::LocalServer);
	CHECK_EQ(id, std::wstring(L"{CEFF45EE-C862-41DE-AEE2-A022C81EDA92}"));
	CHECK(ComServers::ParseCommandLine(L"\"C:\\Windows\\SystemApps\\MicrosoftWindows.Client.CBS_cw5n1h2txyewy\\SearchHost.exe\" -ServerName:CortanaUI.AppXstmwaab17q5s3y22tp6apqz7a45vwv65.mca", id) == ComServerKind::WinRtServer);
	CHECK_EQ(id, std::wstring(L"CortanaUI.AppXstmwaab17q5s3y22tp6apqz7a45vwv65.mca"));
	// not switches: a word inside a path or an argument, a broken ID, nothing
	CHECK(ComServers::ParseCommandLine(L"C:\\Tools\\embedding.exe", id) == ComServerKind::None);
	CHECK(ComServers::ParseCommandLine(L"notepad.exe C:\\docs\\-embeddings.txt", id) == ComServerKind::None);
	CHECK(ComServers::ParseCommandLine(L"notepad.exe C:\\docs\\word-embedding", id) == ComServerKind::None);
	CHECK(ComServers::ParseCommandLine(L"viewer.exe C:\\pics\\a/processid:{3EB3C877-1F16-487C-9050-104DBCD66683}", id) == ComServerKind::None);
	CHECK(ComServers::ParseCommandLine(L"dllhost.exe /Processid:{3EB3C877}", id) == ComServerKind::None);
	CHECK(ComServers::ParseCommandLine(L"", id) == ComServerKind::None);
}

TEST(Com_ServerPathAndDescribe) {
	CHECK_EQ(ComServers::ServerPath(L"\"C:\\Program Files\\Oracle\\VirtualBox\\VBoxSVC.exe\""), std::wstring(L"C:\\Program Files\\Oracle\\VirtualBox\\VBoxSVC.exe"));
	CHECK_EQ(ComServers::ServerPath(L"C:\\Windows\\System32\\RuntimeBroker.exe -Embedding"), std::wstring(L"C:\\Windows\\System32\\RuntimeBroker.exe"));
	CHECK_EQ(ComServers::ServerPath(L"  \"C:\\a b\\x.exe\" /Automation"), std::wstring(L"C:\\a b\\x.exe"));
	WCHAR windir[MAX_PATH];
	::GetEnvironmentVariable(L"SystemRoot", windir, _countof(windir));
	CHECK_EQ(ComServers::ServerPath(L"%SystemRoot%\\system32\\SearchIndexer.exe /Embedding"), std::wstring(windir) + L"\\system32\\SearchIndexer.exe");

	ComServerInfo info;
	CHECK(info.Describe().empty());
	info.Kind = ComServerKind::Surrogate;
	info.Id = L"{02D4B3F1-FD88-11D1-960D-00805FC79235}";
	CHECK_EQ(info.Describe(), std::wstring(L"COM Surrogate: {02D4B3F1-FD88-11D1-960D-00805FC79235}"));
	info.Registration = ComRegistration::ComPlus;
	info.Name = L"System Application";
	CHECK_EQ(info.Describe(), std::wstring(L"COM+ Application: System Application"));
	info = {};
	info.Kind = ComServerKind::LocalServer;
	CHECK_EQ(info.Describe(), std::wstring(L"COM Server"));
	info.Classes = 13;
	CHECK_EQ(info.Describe(), std::wstring(L"COM Server (13 classes)"));
	info.Name = L"RuntimeBroker";
	info.Classes = 32;
	CHECK_EQ(info.Describe(), std::wstring(L"COM Server: RuntimeBroker (32 classes)"));
}

TEST(Com_IsComCall) {
	// an apartment's endpoint (as IEventSource's to VBoxSVC was), COM's activation in rpcss, and plain RPC
	IpcLink link;
	link.Kind = IpcKind::Rpc;
	link.Protocol = RpcProtocol::Lrpc;
	link.Endpoint = L"OLEFC24E6148898EF870B52570025C2";
	CHECK(IpcTracker::IsComCall(link));
	link.Endpoint = L"OLEFC24E6148898EF870B52570025CZ";	// not hex
	CHECK(!IpcTracker::IsComCall(link));
	link.Endpoint = L"OLE";
	CHECK(!IpcTracker::IsComCall(link));
	link.Endpoint = L"epmapper";
	CHECK(!IpcTracker::IsComCall(link));
	link.Interface = { 0x00000136, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };	// ISCMLocalActivator
	CHECK(IpcTracker::IsComCall(link));
	link.Interface = { 0x367abb81, 0x9844, 0x35f1, { 0xad, 0x32, 0x98, 0xf0, 0x38, 0x00, 0x10, 0x03 } };	// svcctl
	link.Endpoint = L"ntsvcs";
	CHECK(!IpcTracker::IsComCall(link));
	// an OLE endpoint over a named pipe isn't an apartment's; ALPC links never are
	link.Endpoint = L"OLEFC24E6148898EF870B52570025C2";
	link.Protocol = RpcProtocol::NamedPipe;
	CHECK(!IpcTracker::IsComCall(link));
	link.Protocol = RpcProtocol::Lrpc;
	link.Kind = IpcKind::Alpc;
	CHECK(!IpcTracker::IsComCall(link));
}

TEST(Com_InterpreterOf) {
	CHECK_EQ(ComRegistry::InterpreterOf(L"C:\\Windows\\System32\\rundll32.exe"), std::wstring(L"rundll32.exe"));
	CHECK_EQ(ComRegistry::InterpreterOf(L"C:\\WINDOWS\\System32\\WindowsPowerShell\\v1.0\\PowerShell.exe"), std::wstring(L"powershell.exe"));
	CHECK_EQ(ComRegistry::InterpreterOf(L"mshta.exe"), std::wstring(L"mshta.exe"));
	CHECK(ComRegistry::InterpreterOf(L"C:\\Program Files\\Oracle\\VirtualBox\\VBoxSVC.exe").empty());
	CHECK(ComRegistry::InterpreterOf(L"C:\\Tools\\notrundll32.exe").empty());
	CHECK(ComRegistry::InterpreterOf(L"").empty());
}

TEST(Com_IsWritableByUsersLive) {
	// the user's own temp folder: yes (a file that isn't there can be created); System32: no, even elevated (the
	// check is a standard user's, without Administrators); ProgramData: users may create folders there
	WCHAR temp[MAX_PATH];
	::GetTempPath(_countof(temp), temp);
	CHECK(ComRegistry::IsWritableByUsers(std::wstring(temp) + L"WinSysLiveNoSuch.dll"));
	CHECK(ComRegistry::IsWritableByUsers(std::wstring(temp) + L"WinSysLiveNoSuch\\Deeper\\x.dll"));
	WCHAR system[MAX_PATH];
	::GetSystemDirectory(system, _countof(system));
	CHECK(!ComRegistry::IsWritableByUsers(std::wstring(system) + L"\\kernel32.dll"));
	CHECK(!ComRegistry::IsWritableByUsers(std::wstring(system) + L"\\WinSysLiveNoSuch.dll"));
	CHECK(!ComRegistry::IsWritableByUsers(std::wstring(system) + L"\\WinSysLiveNoSuch\\x.dll"));
	WCHAR programData[MAX_PATH];
	::GetEnvironmentVariable(L"ProgramData", programData, _countof(programData));
	CHECK(ComRegistry::IsWritableByUsers(std::wstring(programData) + L"\\WinSysLiveNoSuch\\x.dll"));
	// a file the user made: they can write it
	auto file = std::wstring(temp) + L"WinSysLiveWritable.tmp";
	wil::unique_hfile h(::CreateFile(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
	h.reset();
	CHECK(ComRegistry::IsWritableByUsers(file));
	::DeleteFile(file.c_str());
}

TEST(Com_LiveServers) {
	::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	auto start = ::GetTickCount64();
	auto classes = ComServers::GetIndexedClasses();
	printf("  %zu classes indexed in %llu ms\n", classes, ::GetTickCount64() - start);
	CHECK(classes > 100);
	ProcessManager<> pm;
	pm.Update();
	int servers = 0, named = 0;
	start = ::GetTickCount64();
	for (auto& p : pm.GetProcesses()) {
		auto process = Process::OpenById(p->Id);
		if (!process)
			continue;
		auto commandLine = process->GetCommandLine();
		auto info = ComServers::Identify(commandLine, process->GetFullImageName(), process->IsWow64Process(), process->GetPackageFullName());
		if (info.Kind == ComServerKind::None)
			continue;
		servers++;
		if (!info.Name.empty())
			named++;
		printf("  %6u %-28ls %ls%ls%ls\n", p->Id, p->GetImageName().c_str(), info.Describe().c_str(),
			info.ServerDll.empty() ? L"" : L" -> ", info.ServerDll.c_str());
		// every dllhost has a surrogate's command line
		if (_wcsicmp(p->GetImageName().c_str(), L"dllhost.exe") == 0)
			CHECK(info.Kind == ComServerKind::Surrogate);
		// a packaged app registered as a server (CrossDeviceService, WidgetService, PowerToys' CmdPal extensions) is
		// found in its package's servers
		if (commandLine.find(L"-RegisterProcessAsComServer") != std::wstring::npos && !process->GetPackageFullName().empty())
			CHECK(info.Registration == ComRegistration::Packaged && !info.Name.empty() && info.Classes > 0);
	}
	printf("  %d COM servers, %d named, in %llu ms\n", servers, named, ::GetTickCount64() - start);
	::CoUninitialize();
}

TEST(Com_LiveRegistry) {
	for (bool wow64 : { false, true }) {
		auto start = ::GetTickCount64();
		auto classes = ComRegistry::EnumClasses(wow64);
		auto elapsed = ::GetTickCount64() - start;
		int types[6]{}, missing = 0, perUser = 0, overrides = 0, packaged = 0, named = 0, writable = 0, scriptlets = 0, interpreters = 0;
		for (auto& c : classes) {
			types[(int)c.Type]++;
			missing += c.ServerMissing;
			perUser += c.PerUser;
			overrides += c.OverridesMachine;
			packaged += !c.Package.empty();
			named += !c.Name.empty();
			// a few keys aren't GUIDs (malformed registrations): listed as they are
			CHECK(c.Clsid[0] == L'{');
			if (c.Clsid.size() != 38)
				printf("    not a GUID: %ls (%ls)\n", c.Clsid.c_str(), c.Name.c_str());
			CHECK(c.Wow64 == wow64);
			// only the user's can override the machine's, and the machine's it hides is there too
			if (c.OverridesMachine) {
				CHECK(c.PerUser);
				CHECK(std::ranges::any_of(classes, [&](auto& m) { return !m.PerUser && m.OverriddenByUser && m.Clsid == c.Clsid; }));
			}
			if (c.OverriddenByUser)
				CHECK(!c.PerUser);
			// only machine-wide classes are checked
			if (c.ServerWritable)
				CHECK(!c.PerUser && c.Package.empty());
			writable += c.ServerWritable;
			scriptlets += !c.ScriptletUrl.empty();
			interpreters += !c.Interpreter.empty() && !c.InterpreterRunsSystemDll;
			if (c.InterpreterRunsSystemDll)
				CHECK_EQ(c.Interpreter, std::wstring(L"rundll32.exe"));
		}
		printf("  %d machine-wide servers writable by users, %d scriptlets, %d interpreters\n", writable, scriptlets, interpreters);
		int shown = 0;
		for (auto& c : classes)
			if ((c.ServerWritable || !c.ScriptletUrl.empty() || (!c.Interpreter.empty() && !c.InterpreterRunsSystemDll)) && shown++ < 8)
				printf("    %ls %ls: %ls%ls%ls%ls\n", c.Clsid.c_str(), c.Name.c_str(), c.Server.c_str(),
					c.ServerWritable ? L" [writable]" : L"", c.ScriptletUrl.empty() ? L"" : (L" [scriptlet " + c.ScriptletUrl + L"]").c_str(),
					c.Interpreter.empty() ? L"" : L" [interpreter]");
		printf("  %s: %zu classes in %llu ms (%d named): %d in-process, %d handlers, %d local servers, %d surrogates, %d services, %d other; %d per user (%d over the machine's), %d packaged, %d missing servers\n",
			wow64 ? "32-bit" : "64-bit", classes.size(), elapsed, named, types[1], types[2], types[3], types[4], types[5], types[0],
			perUser, overrides, packaged, missing);
		CHECK(classes.size() > 500);
		// the keys' times, TreatAs, elevation (auto-approved only if enabled), the user's ProgIDs pointing elsewhere
		int treatAs = 0, elevation = 0, approved = 0, redirected = 0, timed = 0;
		for (auto& c : classes) {
			treatAs += !c.TreatAs.empty();
			elevation += c.ElevationEnabled;
			approved += c.AutoApproved;
			redirected += !c.RedirectedProgIds.empty();
			if (c.Package.empty()) {
				timed += c.LastWrite != 0;
				// not in the future, nor before 1995 (FILETIME 125,000,000,000,000,000 is 1997)
				CHECK(c.LastWrite == 0 || (c.LastWrite > 120'000'000'000'000'000LL && c.LastWrite < 140'000'000'000'000'000LL));
			}
			if (c.AutoApproved)
				CHECK(c.ElevationEnabled && !c.PerUser);
			if (!c.TreatAs.empty())
				CHECK(c.TreatAs[0] == L'{');
		}
		printf("  %d TreatAs, %d elevation enabled (%d without a prompt), %d with the user's ProgIDs pointing at them, %d with a time\n",
			treatAs, elevation, approved, redirected, timed);
		CHECK(timed > (int)classes.size() / 2);
		shown = 0;
		for (auto& c : classes)
			if (!c.RedirectedProgIds.empty() && shown++ < 5)
				printf("    redirected: %ls %ls: %ls\n", c.Clsid.c_str(), c.Name.c_str(), c.RedirectedProgIds.c_str());
		shown = 0;
		for (auto& c : classes)
			if (c.ServerMissing && shown++ < 5)
				printf("    missing: %ls %ls %ls\n", c.Clsid.c_str(), c.Name.c_str(), c.ServerFile.c_str());
	}
	// a well-known one: the shell's
	auto classes = ComRegistry::EnumClasses(false);
	auto find = [&](PCWSTR clsid) { return std::ranges::find(classes, std::wstring(clsid), &ComClassRegistration::Clsid); };
	auto shell = find(L"{13709620-C279-11CE-A49E-444553540000}");	// Shell.Application
	CHECK(shell != classes.end());
	if (shell != classes.end()) {
		CHECK(shell->Type == ComServerType::InProc || shell->Type == ComServerType::LocalServer);
		CHECK_EQ(shell->ProgId, std::wstring(L"Shell.Application.1"));
		CHECK(!shell->ServerMissing);
	}
	// a ProgID the user remaps (as a hijack would): a rarely used one of WMI's, pointed at Shell.Application's class
	// for the moment of the read, if the user has no key of that name (and the machine has the ProgID)
	{
		PCWSTR progId = L"WbemScripting.SWbemDateTime";
		auto userKey = std::wstring(L"Software\\Classes\\") + progId;
		HKEY key;
		bool machine = ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, (userKey + L"\\CLSID").c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS;
		if (machine)
			::RegCloseKey(key);
		bool userHas = ::RegOpenKeyEx(HKEY_CURRENT_USER, userKey.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS;
		if (userHas)
			::RegCloseKey(key);
		if (!machine || userHas)
			printf("  ProgID redirect: skipped\n");
		else {
			PCWSTR target = L"{13709620-C279-11CE-A49E-444553540000}";
			if (::RegCreateKeyEx(HKEY_CURRENT_USER, (userKey + L"\\CLSID").c_str(), 0, nullptr, REG_OPTION_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
				::RegSetValueEx(key, nullptr, 0, REG_SZ, (BYTE const*)target, DWORD((wcslen(target) + 1) * sizeof(WCHAR)));
				::RegCloseKey(key);
				auto redirected = ComRegistry::EnumClasses(false);
				::RegDeleteTree(HKEY_CURRENT_USER, userKey.c_str());
				::RegDeleteKey(HKEY_CURRENT_USER, userKey.c_str());
				auto it = std::ranges::find_if(redirected, [&](auto& c) { return c.Clsid == target && !c.OverriddenByUser; });
				CHECK(it != redirected.end() && it->RedirectedProgIds.find(progId) != std::wstring::npos);
				if (it != redirected.end())
					printf("  ProgID redirect: %ls\n", it->RedirectedProgIds.c_str());
			}
		}
	}
	bool isDefault = false;
	auto sd = ComRegistry::GetPermission(L"", true, false, isDefault);
	printf("  default launch permission: %zu bytes\n", sd.size());
	CHECK(isDefault);
}

// the DCOM permissions' entries and rights (WinSys ComSecurity), on a security descriptor made from SDDL: COM writes its
// rights in the DS rights' bits (CC 1 = COM_RIGHTS_EXECUTE, DC 2 = local, LC 4 = remote, SW 8 = local activation,
// RP 16 = remote activation)
TEST(Dcom_ParsePermission) {
	PSECURITY_DESCRIPTOR sd = nullptr;
	CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptor(L"O:BAG:BAD:(A;;CCDCLCSWRP;;;SY)(A;;CCDCSW;;;WD)(D;;CCLC;;;AN)",
		SDDL_REVISION_1, &sd, nullptr));
	if (!sd)
		return;
	auto entries = ComSecurity::ParsePermission(sd);
	::LocalFree(sd);
	CHECK_EQ(entries.size(), (size_t)3);
	if (entries.size() != 3)
		return;
	CHECK_EQ(entries[0].Sid, std::wstring(L"S-1-5-18"));
	CHECK(entries[0].Allow);
	CHECK_EQ(entries[0].Rights, (uint32_t)0x1F);
	CHECK(ComSecurity::GrantsRemote(entries[0]));
	CHECK_EQ(entries[1].Sid, std::wstring(L"S-1-1-0"));
	CHECK(!entries[1].Account.empty());
	CHECK_EQ(ComSecurity::RightsText(entries[1].Rights, true), std::wstring(L"Local Launch, Local Activation"));
	CHECK(!ComSecurity::GrantsRemote(entries[1]));
	CHECK_EQ(entries[2].Sid, std::wstring(L"S-1-5-7"));
	CHECK(!entries[2].Allow);
	CHECK(!ComSecurity::GrantsRemote(entries[2]));	// a deny grants nothing
	CHECK_EQ(ComSecurity::RightsText(entries[2].Rights, false), std::wstring(L"Remote Access"));
	// an old-style entry (COM_RIGHTS_EXECUTE alone) has every right
	CHECK_EQ(ComSecurity::RightsText(1, true), std::wstring(L"Local Launch, Remote Launch, Local Activation, Remote Activation"));
	CHECK_EQ(ComSecurity::RightsText(1, false), std::wstring(L"Local Access, Remote Access"));
	CHECK(ComSecurity::GrantsRemote({ L"", L"S-1-1-0", true, 1 }));
	CHECK_EQ(ComSecurity::RightsText(0, true), std::wstring(L"None"));
	// not a security descriptor
	uint8_t junk[16]{};
	CHECK(ComSecurity::ParsePermission(junk).empty());
	CHECK(ComSecurity::ParsePermission(nullptr).empty());
	CHECK(ComSecurity::IsBroadAccount(L"S-1-1-0"));
	CHECK(ComSecurity::IsBroadAccount(L"s-1-5-32-545"));
	CHECK(!ComSecurity::IsBroadAccount(L"S-1-5-32-544"));
	CHECK(!ComSecurity::IsBroadAccount(L"S-1-5-4"));	// Interactive: never in a remote caller's token
	CHECK_EQ(ComSecurity::AuthenticationLevelText(1), std::wstring(L"None (1)"));
	CHECK_EQ(ComSecurity::AuthenticationLevelText(6), std::wstring(L"Packet Privacy (6)"));
	CHECK_EQ(ComSecurity::AuthenticationLevelText(9), std::wstring(L"9"));
	CHECK_EQ(ComSecurity::ImpersonationLevelText(2), std::wstring(L"Identify (2)"));
}

// the machine's DCOM settings, the app IDs, the Running Object Table and the DCOM events as this machine has them
TEST(Dcom_Live) {
	auto settings = ComSecurity::GetMachineSettings();
	CHECK(settings.size() >= 10);
	if (!settings.empty())
		CHECK_EQ(settings[0].Name, std::wstring(L"EnableDCOM"));
	int permissions = 0;
	for (auto& s : settings) {
		CHECK(!s.Value.empty());
		CHECK(!s.Description.empty());
		if (s.IsPermission) {
			permissions++;
			CHECK_EQ(s.Present, !s.SecurityDescriptor.empty());
		}
	}
	CHECK_EQ(permissions, 4);
	// Windows sets the limits
	auto limit = std::ranges::find(settings, std::wstring(L"MachineLaunchRestriction"), &DcomMachineSetting::Name);
	CHECK(limit != settings.end() && limit->Present);
	if (limit != settings.end() && limit->Present)
		CHECK(!ComSecurity::ParsePermission(limit->SecurityDescriptor.data()).empty());

	auto ids = ComRegistry::EnumAppIds();
	CHECK(ids.size() > 100);
	bool service = false, executable = false, launch = false;
	for (auto& a : ids) {
		CHECK(a.AppId.size() == 38 && a.AppId.front() == L'{');
		service |= !a.ServiceName.empty();
		executable |= !a.Executables.empty();
		launch |= !a.LaunchPermission.empty();
	}
	CHECK(service);
	CHECK(executable);
	CHECK(launch);

	auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	auto rot = ComSecurity::EnumRunningObjects();
	CHECK(rot.has_value());
	if (SUCCEEDED(com))
		::CoUninitialize();

	// only the provider's events (there may be none in the last month)
	auto events = EventLog::Query(L"System", 4, 24 * 30, 50, EventDataRead::None, L"Microsoft-Windows-DistributedCOM");
	CHECK(events.has_value());
	if (events)
		for (auto& e : *events)
			CHECK(e.Provider.find(L"DistributedCOM") != std::wstring::npos || e.Provider == L"DCOM");
}
