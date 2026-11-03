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
		auto info = ComServers::Identify(commandLine, process->GetFullImageName(), process->IsWow64Process());
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
	}
	printf("  %d COM servers, %d named, in %llu ms\n", servers, named, ::GetTickCount64() - start);
	::CoUninitialize();
}
