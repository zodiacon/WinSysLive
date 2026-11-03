#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Com.h>
#include <WinSys/Ipc.h>
#include <WinSys/ProcessManager.h>
#include <WinSys/Processes.h>

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
	bool isDefault = false;
	auto sd = ComRegistry::GetPermission(L"", true, false, isDefault);
	printf("  default launch permission: %zu bytes\n", sd.size());
	CHECK(isDefault);
}
