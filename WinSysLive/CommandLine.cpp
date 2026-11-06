#include "pch.h"
#include "CommandLine.h"
#include "ListExport.h"
#include "ThreadCommands.h"

namespace {
	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	// "/name" or "-name", lowercase without the prefix; empty for a value
	std::wstring Switch(std::wstring const& arg) {
		if (arg.size() < 2 || (arg[0] != L'/' && arg[0] != L'-'))
			return L"";
		return Lowercase(arg.substr(1));
	}
}

std::optional<CommandLineRequest> CommandLine::Parse(std::vector<std::wstring> const& args) {
	if (args.empty())
		return std::nullopt;
	auto command = Switch(args[0]);
	CommandLineRequest request;
	if (command == L"help" || command == L"?") {
		request.Command = CommandLineRequest::Action::Help;
		return request;
	}
	if (command == L"snapshot")
		request.Command = CommandLineRequest::Action::Snapshot;
	else if (command == L"compare")
		request.Command = CommandLineRequest::Action::Compare;
	else if (command == L"export")
		request.Command = CommandLineRequest::Action::Export;
	else
		return std::nullopt;	// the window's own (/minimized, /locks, /restart...) or none

	// the values in order, and the options with theirs
	std::vector<std::wstring> values;
	for (size_t i = 1; i < args.size(); i++) {
		auto name = Switch(args[i]);
		if (name.empty()) {
			values.push_back(args[i]);
			continue;
		}
		bool takesValue = name == L"out" || (name == L"from" && request.Command == CommandLineRequest::Action::Export);
		if (!takesValue) {
			request.Error = L"Unknown option: " + args[i];
			return request;
		}
		if (i + 1 >= args.size() || !Switch(args[i + 1]).empty()) {
			request.Error = args[i] + L" needs a file";
			return request;
		}
		(name == L"out" ? request.Out : request.From) = args[++i];
	}
	switch (request.Command) {
		case CommandLineRequest::Action::Snapshot:
			if (values.size() != 1 || !request.Out.empty())
				request.Error = L"/snapshot takes the file to save";
			else
				request.File = values[0];
			break;
		case CommandLineRequest::Action::Compare:
			if (values.empty() || values.size() > 2)
				request.Error = L"/compare takes a snapshot to compare with now, or two snapshots";
			else {
				request.Before = values[0];
				if (values.size() == 2)
					request.After = values[1];
			}
			break;
		case CommandLineRequest::Action::Export:
			if (values.size() != 1)
				request.Error = L"/export takes what to export: processes, modules, threads, services, drivers or autoruns";
			else {
				request.What = Lowercase(values[0]);
				auto is = [&](PCWSTR kind) { return request.What == kind; };
				bool live = std::ranges::any_of(LiveExportKinds, is);
				if (!live && std::ranges::none_of(ExportKinds, is))
					request.Error = L"Nothing called \"" + values[0] + L"\" to export: processes, modules, threads, services, drivers or autoruns";
				else if (live && !request.From.empty())
					request.Error = L"Snapshots don't keep the " + request.What + L": /export " + request.What + L" takes the system now, without /from";
			}
			break;
	}
	return request;
}

std::optional<CommandLineRequest> CommandLine::Parse(PCWSTR commandLine) {
	int count = 0;
	auto argv = ::CommandLineToArgvW(commandLine ? commandLine : L"", &count);
	if (!argv)
		return std::nullopt;
	std::vector<std::wstring> args;
	for (int i = 1; i < count; i++)
		args.push_back(argv[i]);
	::LocalFree(argv);
	return Parse(args);
}

std::wstring CommandLine::Usage() {
	return
		L"WinSysLive without its window:\r\n"
		L"\r\n"
		L"  WinSysLive /snapshot <file.wslsnap>\r\n"
		L"      Saves a snapshot of the system now (processes and their modules, services, drivers, autoruns),\r\n"
		L"      as File > Snapshots does.\r\n"
		L"  WinSysLive /compare <before.wslsnap> [<after.wslsnap>] [/out <file>]\r\n"
		L"      What changed between two snapshots, or since one (without the second).\r\n"
		L"  WinSysLive /export processes|modules|services|drivers|autoruns [/from <file.wslsnap>] [/out <file>]\r\n"
		L"      A table of the system now, or of a saved snapshot.\r\n"
		L"  WinSysLive /export threads [/out <file>]\r\n"
		L"      Every process's threads now (snapshots don't keep them).\r\n"
		L"\r\n"
		L"/out writes CSV for a .csv file, else tab-separated text; without it the table goes to the console.\r\n"
		L"Run elevated for every process's modules and command line.\r\n"
		L"Exit code: 0 success, 1 bad arguments, 2 failure.\r\n"
		L"From cmd.exe, \"start /wait WinSysLive ...\" waits for it; PowerShell waits when the output is piped or redirected.\r\n";
}

std::wstring CommandLine::IsoTime(int64_t time) {
	if (time == 0)
		return L"";
	FILETIME ft{ (DWORD)time, (DWORD)(time >> 32) };
	SYSTEMTIME st;
	if (!::FileTimeToSystemTime(&ft, &st))
		return L"";
	return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::vector<std::vector<std::wstring>> CommandLine::ExportTable(Snapshot const& s, std::wstring const& what) {
	std::vector<std::vector<std::wstring>> rows;
	if (what == L"processes") {
		rows.push_back({ L"PID", L"Parent PID", L"Name", L"Path", L"Command Line", L"User", L"Created", L"Threads", L"Handles", L"Private Bytes", L"Modules" });
		for (auto& p : s.Processes)
			rows.push_back({ std::to_wstring(p.Pid), std::to_wstring(p.ParentPid), p.Name, p.Path, p.CommandLine, p.User, IsoTime(p.CreateTime),
				std::to_wstring(p.Threads), std::to_wstring(p.Handles), std::to_wstring(p.PrivateBytes), std::to_wstring(p.Modules.size()) });
	}
	else if (what == L"modules") {
		// a row per module of each process
		rows.push_back({ L"PID", L"Process", L"Module" });
		for (auto& p : s.Processes)
			for (auto& m : p.Modules)
				rows.push_back({ std::to_wstring(p.Pid), p.Name, m });
	}
	else if (what == L"services") {
		rows.push_back({ L"Name", L"Display Name", L"State", L"Start Type", L"Account", L"PID", L"Path" });
		for (auto& v : s.Services)
			rows.push_back({ v.Name, v.DisplayName, v.State, v.StartType, v.Account, v.Pid ? std::to_wstring(v.Pid) : L"", v.Path });
	}
	else if (what == L"drivers") {
		rows.push_back({ L"Name", L"Path" });
		for (auto& d : s.Drivers)
			rows.push_back({ d.Name, d.Path });
	}
	else if (what == L"autoruns") {
		rows.push_back({ L"Category", L"Location", L"Name", L"Enabled", L"Command", L"Image Path" });
		for (auto& a : s.Autoruns)
			rows.push_back({ a.Category, a.Location, a.Name, a.Enabled, a.Command, a.ImagePath });
	}
	return rows;
}

std::vector<std::vector<std::wstring>> CommandLine::ThreadsTable(std::vector<CommandLineThread> const& threads) {
	// "1:02:03.456", hours as many as there are
	auto span = [](int64_t ticks) {
		auto ms = ticks / 10000;
		return std::format(L"{}:{:02}:{:02}.{:03}", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
	};
	std::vector<std::vector<std::wstring>> rows;
	rows.push_back({ L"PID", L"Process", L"TID", L"State", L"Wait Reason", L"Priority", L"Base Priority", L"CPU Time",
		L"Kernel Time", L"User Time", L"Context Switches", L"Start Address", L"Created" });
	for (auto& t : threads)
		rows.push_back({ std::to_wstring(t.Pid), t.Process, std::to_wstring(t.Tid), ThreadCommands::StateText(t.State),
			t.State == 5 ? ThreadCommands::WaitReasonText(t.WaitReason) : L"",	// Waiting
			std::to_wstring(t.Priority), std::to_wstring(t.BasePriority), span(t.KernelTime + t.UserTime), span(t.KernelTime),
			span(t.UserTime), std::to_wstring(t.ContextSwitches), t.StartAddress ? std::format(L"0x{:X}", t.StartAddress) : L"",
			// the System process's threads made at boot have a time near 0, before the clock was set: none
			t.Created > 116444736000000000 ? IsoTime(t.Created) : L"" });	// 1970
	return rows;
}

std::vector<std::vector<std::wstring>> CommandLine::CompareTable(std::vector<SnapshotRow> const& rows) {
	std::vector<std::vector<std::wstring>> table;
	table.push_back({ L"Kind", L"Change", L"Name", L"Details", L"Path" });
	for (auto& r : rows)
		table.push_back({ SnapshotRow::KindName(r.Type), SnapshotRow::ChangeName(r.Diff), r.Name, r.Details, r.Path });
	return table;
}

std::wstring CommandLine::Render(std::vector<std::vector<std::wstring>> const& rows, std::wstring const& path) {
	bool csv = path.size() >= 4 && ::_wcsicmp(path.c_str() + path.size() - 4, L".csv") == 0;
	return csv ? ListExport::ToCsv(rows) : ListExport::ToText(rows);
}
