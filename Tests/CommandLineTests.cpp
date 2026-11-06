#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/CommandLine.h"

// the command line without the window (WinSysLive\CommandLine.h): which arguments are a command, their mistakes, and the
// tables /export and /compare write

namespace {
	std::optional<CommandLineRequest> Parse(std::vector<std::wstring> args) {
		return CommandLine::Parse(args);
	}
}

TEST(CommandLine_NotACommand) {
	// the window starts: no arguments, or its own
	CHECK(!Parse({}).has_value());
	CHECK(!Parse({ L"/minimized" }).has_value());
	CHECK(!Parse({ L"/locks", L"C:\\Temp" }).has_value());
	CHECK(!Parse({ L"/restart" }).has_value());
	CHECK(!Parse({ L"C:\\Windows\\System32\\taskmgr.exe" }).has_value());
	CHECK(!Parse({ L"snapshot", L"x.wslsnap" }).has_value());	// a value, not a switch
}

TEST(CommandLine_Parse) {
	auto r = Parse({ L"/snapshot", L"C:\\Temp\\now.wslsnap" });
	CHECK(r && r->Command == CommandLineRequest::Action::Snapshot && r->Error.empty());
	CHECK(r && r->File == L"C:\\Temp\\now.wslsnap");
	// any case, - for /
	r = Parse({ L"-SNAPSHOT", L"a.wslsnap" });
	CHECK(r && r->Command == CommandLineRequest::Action::Snapshot && r->File == L"a.wslsnap");

	r = Parse({ L"/compare", L"a.wslsnap" });
	CHECK(r && r->Command == CommandLineRequest::Action::Compare && r->Before == L"a.wslsnap" && r->After.empty() && r->Error.empty());
	r = Parse({ L"/compare", L"a.wslsnap", L"b.wslsnap", L"/out", L"diff.csv" });
	CHECK(r && r->Before == L"a.wslsnap" && r->After == L"b.wslsnap" && r->Out == L"diff.csv" && r->Error.empty());
	// /out anywhere
	r = Parse({ L"/compare", L"/out", L"diff.txt", L"a.wslsnap" });
	CHECK(r && r->Before == L"a.wslsnap" && r->Out == L"diff.txt" && r->Error.empty());

	r = Parse({ L"/export", L"Processes", L"/out", L"p.csv" });
	CHECK(r && r->Command == CommandLineRequest::Action::Export && r->What == L"processes" && r->Out == L"p.csv" && r->Error.empty());
	r = Parse({ L"/export", L"services", L"/from", L"a.wslsnap" });
	CHECK(r && r->What == L"services" && r->From == L"a.wslsnap" && r->Out.empty() && r->Error.empty());

	// checked before they're used: a failure is reported, not a crash
	auto help = [](std::wstring arg) {
		auto r = Parse({ arg });
		return r.has_value() && r->Command == CommandLineRequest::Action::Help;
	};
	CHECK(help(L"/help"));
	CHECK(help(L"/?"));
	CHECK(help(L"-?"));

	// from a whole command line: the program's name first, quotes
	r = CommandLine::Parse(L"\"C:\\Program Files\\WinSysLive.exe\" /export drivers /out \"C:\\My Files\\d.csv\"");
	CHECK(r && r->What == L"drivers" && r->Out == L"C:\\My Files\\d.csv" && r->Error.empty());
	CHECK(!CommandLine::Parse(L"\"C:\\Program Files\\WinSysLive.exe\" /minimized").has_value());
}

TEST(CommandLine_Mistakes) {
	// a command with a mistake is still a command (the usage is shown, exit code 1), not the window
	auto mistake = [](std::vector<std::wstring> args) {
		auto r = Parse(args);
		return r.has_value() && !r->Error.empty();
	};
	CHECK(mistake({ L"/snapshot" }));
	CHECK(mistake({ L"/snapshot", L"a", L"b" }));
	CHECK(mistake({ L"/snapshot", L"a", L"/out", L"b" }));
	CHECK(mistake({ L"/compare" }));
	CHECK(mistake({ L"/compare", L"a", L"b", L"c" }));
	CHECK(mistake({ L"/compare", L"a", L"/out" }));
	CHECK(mistake({ L"/compare", L"a", L"/out", L"/b" }));
	CHECK(mistake({ L"/compare", L"a", L"/from", L"b" }));	// only /export's
	CHECK(mistake({ L"/export" }));
	CHECK(mistake({ L"/export", L"handles" }));
	CHECK(mistake({ L"/export", L"processes", L"services" }));
	CHECK(mistake({ L"/export", L"processes", L"/verbose" }));
	auto r = Parse({ L"/export", L"handles" });
	CHECK(r && r->Error.find(L"handles") != std::wstring::npos);
}

TEST(CommandLine_Tables) {
	Snapshot s;
	Snapshot::Process p;
	p.Pid = 1234;
	p.ParentPid = 4;
	p.Name = L"app.exe";
	p.Path = L"C:\\App\\app.exe";
	p.CommandLine = L"app.exe /x \"a, b\"";
	p.CreateTime = 0x01DD6D5A00000000;
	p.Modules = { L"C:\\Windows\\System32\\ntdll.dll", L"C:\\Windows\\System32\\kernel32.dll" };
	s.Processes.push_back(p);
	s.Services.push_back({ L"Spooler", L"Print Spooler", L"Running", L"Automatic", L"LocalSystem", L"C:\\Windows\\System32\\spoolsv.exe", 999 });
	s.Drivers.push_back({ L"ntfs.sys", L"C:\\Windows\\System32\\drivers\\ntfs.sys" });

	auto processes = CommandLine::ExportTable(s, L"processes");
	CHECK_EQ(processes.size(), (size_t)2);
	CHECK_EQ(processes[0][0], std::wstring(L"PID"));
	CHECK_EQ(processes[1][0], std::wstring(L"1234"));
	CHECK_EQ(processes[1][4], p.CommandLine);
	CHECK_EQ(processes[1].back(), std::wstring(L"2"));	// the modules counted
	CHECK_EQ(processes[0].size(), processes[1].size());
	// a row per module
	auto modules = CommandLine::ExportTable(s, L"modules");
	CHECK_EQ(modules.size(), (size_t)3);
	CHECK_EQ(modules[2][2], std::wstring(L"C:\\Windows\\System32\\kernel32.dll"));
	auto services = CommandLine::ExportTable(s, L"services");
	CHECK_EQ(services.size(), (size_t)2);
	CHECK_EQ(services[1][5], std::wstring(L"999"));
	CHECK_EQ(CommandLine::ExportTable(s, L"drivers").size(), (size_t)2);
	CHECK_EQ(CommandLine::ExportTable(s, L"autoruns").size(), (size_t)1);	// the header alone
	CHECK(CommandLine::ExportTable(s, L"handles").empty());
	for (auto kind : CommandLine::ExportKinds)
		CHECK(!CommandLine::ExportTable(s, kind).empty());

	// CSV quotes what needs it; text is tab separated
	auto csv = CommandLine::Render(processes, L"out.CSV");
	CHECK(csv.find(L"\"app.exe /x \"\"a, b\"\"\"") != std::wstring::npos);
	auto text = CommandLine::Render(processes, L"out.txt");
	CHECK(text.find(L"1234\t4\tapp.exe\t") != std::wstring::npos);
	CHECK_EQ(CommandLine::Render(processes, L""), text);

	SnapshotRow row{ SnapshotRow::Kind::Service, SnapshotRow::Change::Changed, L"Spooler", L"State: Running -> Stopped", L"" };
	auto compare = CommandLine::CompareTable({ row });
	CHECK_EQ(compare.size(), (size_t)2);
	CHECK_EQ(compare[0][1], std::wstring(L"Change"));
	CHECK_EQ(compare[1][1], std::wstring(SnapshotRow::ChangeName(SnapshotRow::Change::Changed)));
	CHECK_EQ(compare[1][3], std::wstring(L"State: Running -> Stopped"));

	// 2026-11-05 00:00:00 UTC
	SYSTEMTIME st{ 2026, 11, 0, 5, 0, 0, 0, 0 };
	FILETIME ft;
	::SystemTimeToFileTime(&st, &ft);
	CHECK_EQ(CommandLine::IsoTime((int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart), std::wstring(L"2026-11-05T00:00:00Z"));
	CHECK_EQ(CommandLine::IsoTime(0), std::wstring());
}
