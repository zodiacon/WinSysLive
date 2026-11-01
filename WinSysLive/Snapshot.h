#pragma once

//
// A record of the system at one moment, to save, open later and compare (File > Snapshots, CSnapshotDlg): the
// processes (image, command line, user, counts) with the modules each has loaded, the Win32 services (state, start
// type, account, binary path), the loaded kernel modules and the autorun entries (WinSys Autoruns). Handles are left
// out (hundreds of thousands); each process keeps its count.
//
// The file is UTF-8 text, a record a line, tab separated, its first field the record's kind (P process, M a module of
// the process above it, S service, D driver, A autorun), after a header line and the computer's name, OS and time;
// tabs, line breaks and backslashes in fields are escaped (\t, \n, \r, \\).
//
// Compare matches what has no stable identity across time by what it is: processes by image path (instances
// counted, as PIDs change), modules by path (with the processes loading them), services by name, drivers by path,
// autoruns by category, location and name.
//
struct Snapshot {
	struct Process {
		uint32_t Pid{ 0 }, ParentPid{ 0 };
		std::wstring Name, Path, CommandLine, User;
		int64_t CreateTime{ 0 };
		uint32_t Threads{ 0 }, Handles{ 0 };
		uint64_t PrivateBytes{ 0 };
		std::vector<std::wstring> Modules;	// paths, the image itself left out; empty if the process couldn't be read
	};
	struct Service {
		std::wstring Name, DisplayName, State, StartType, Account, Path;
		uint32_t Pid{ 0 };
	};
	struct Driver {
		std::wstring Name, Path;
	};
	struct Autorun {
		std::wstring Category, Location, Name, Enabled, Command, ImagePath;
	};

	std::wstring Computer, Os;
	int64_t Time{ 0 };	// FILETIME (UTC)
	std::vector<Process> Processes;
	std::vector<Service> Services;
	std::vector<Driver> Drivers;
	std::vector<Autorun> Autoruns;

	// the system now: seconds (every process's modules, the autoruns), so a thread of its own; the caller initializes
	// COM there (the tasks)
	static Snapshot Capture();
	// false with the last error set
	bool Save(std::wstring const& path) const;
	// nothing if the file can't be read or isn't a snapshot (error says why)
	static std::optional<Snapshot> Load(std::wstring const& path, std::wstring& error);
};

// a row of a comparison, or of one snapshot's contents (Diff None)
struct SnapshotRow {
	enum class Kind { Process, Module, Service, Driver, Autorun };
	enum class Change { None, Added, Removed, Changed };

	Kind Type{ Kind::Process };
	Change Diff{ Change::None };
	std::wstring Name;
	std::wstring Details;	// what it is, or what changed ("State: Running -> Stopped")
	std::wstring Path;		// the file, when there's one (for Open File Location)

	static PCWSTR KindName(Kind kind);
	static PCWSTR ChangeName(Change change);
};

struct SnapshotCompare final {
	// what's in after and not before, what's gone, and what's different
	static std::vector<SnapshotRow> Compare(Snapshot const& before, Snapshot const& after);
	// everything in one snapshot
	static std::vector<SnapshotRow> Contents(Snapshot const& snapshot);
};
