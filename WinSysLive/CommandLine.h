#pragma once

#include "Snapshot.h"

//
// WinSysLive without its window, for scripts and support requests:
//
//   WinSysLive /snapshot <file.wslsnap>                       the system now, saved as File > Snapshots saves it
//   WinSysLive /compare <before.wslsnap> [<after.wslsnap>]    what changed (after left out: the system now)
//   WinSysLive /export <what> [/from <file.wslsnap>]          processes, modules, services, drivers or autoruns, now
//                                                             or from a saved snapshot
//   WinSysLive /help                                          this
//
// /compare and /export take /out <file>: CSV for a .csv file, else tab-separated text (UTF-8 with a BOM, as File >
// Export saves); without it the table goes to the console, tab separated. The exit code is 0 on success, 1 for a
// mistake in the arguments, 2 for a failure (a file that can't be read or written). Elevated, the snapshot has every
// process's modules and command line; else only those of the user's own processes.
//
// Parse and the tables are apart from running them (CommandLineRun.cpp, which captures through the app's process
// snapshot), so the tests take them.
//
struct CommandLineRequest {
	enum class Action { Help, Snapshot, Compare, Export };
	Action Command{ Action::Help };
	std::wstring File;		// /snapshot's
	std::wstring Before, After;	// /compare's; After empty: now
	std::wstring What;		// /export's, lowercase
	std::wstring From;		// /export's /from; empty: now
	std::wstring Out;		// /out; empty: the console
	std::wstring Error;		// a mistake in the arguments: said with the usage, exit code 1
};

struct CommandLine final {
	enum ExitCode { Success = 0, UsageError = 1, Failure = 2 };

	// the arguments after the program's name; nothing if they aren't a command (the window starts): the first must be
	// /snapshot, /compare, /export, /help or /? (- for /, any case)
	[[nodiscard]] static std::optional<CommandLineRequest> Parse(std::vector<std::wstring> const& args);
	// the same from a command line (CommandLineToArgvW; the program's name first)
	[[nodiscard]] static std::optional<CommandLineRequest> Parse(PCWSTR commandLine);
	[[nodiscard]] static std::wstring Usage();

	// a header row, then a row for each; empty for a kind that isn't one of ExportKinds
	[[nodiscard]] static std::vector<std::vector<std::wstring>> ExportTable(Snapshot const& snapshot, std::wstring const& what);
	static constexpr PCWSTR ExportKinds[] = { L"processes", L"modules", L"services", L"drivers", L"autoruns" };
	// Kind, Change, Name, Details, Path
	[[nodiscard]] static std::vector<std::vector<std::wstring>> CompareTable(std::vector<SnapshotRow> const& rows);
	// CSV for a path ending in .csv, else tab separated
	[[nodiscard]] static std::wstring Render(std::vector<std::vector<std::wstring>> const& rows, std::wstring const& path);
	// "2026-11-05T14:25:44Z"; empty for 0
	[[nodiscard]] static std::wstring IsoTime(int64_t time);

	// runs the request (CommandLineRun.cpp): the exit code
	static int Run(CommandLineRequest const& request);
};
