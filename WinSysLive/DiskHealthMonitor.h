#pragma once

#include <WinSys/DiskHealth.h>

//
// The physical disks' health (WinSys DiskHealthReader), read on a thread of its own: a disk's queries can take a while
// (a hard disk busy with I/O) and the Performance view samples on the UI thread. A disk is read when it's asked for (its
// drive page is shown), then every 30 seconds while it's still asked for; not asked for two minutes, it isn't read until
// it is again (temperatures and counters change slowly). A hard disk that has spun down isn't read (the SMART query would
// wake it): its last reading stays, marked asleep. A disk on a bus whose health isn't asked for (a RAID controller's,
// DiskHealth::NotRead) is read once, for its model, and never touched again.
//
struct DiskHealthMonitor final {
	struct Reading {
		std::optional<WinSys::DiskHealth> Health;	// nothing until the first read ends, or if the disk can't be opened
		bool Done{ false };		// a read was tried
		bool Asleep{ false };	// spun down at the last attempt (Health is the reading before, if any)
	};

	// the disk's last reading; asking keeps its reads going (the first call, or one after two minutes, reads at once)
	static Reading Get(uint32_t diskNumber);
};
