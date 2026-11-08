#pragma once

#include <WinSys/DiskHealth.h>

//
// The physical disks' health (WinSys DiskHealthReader), read on a thread of its own: a disk's queries can take a while
// (a hard disk busy with I/O) and the Performance view samples on the UI thread. A disk is read from the first time it's
// asked for, then every 30 seconds, as long as the app runs (temperatures and counters change slowly). A hard disk that
// has spun down isn't read (the SMART query would wake it): its last reading stays, marked asleep.
//
struct DiskHealthMonitor final {
	struct Reading {
		std::optional<WinSys::DiskHealth> Health;	// nothing until the first read ends, or if the disk can't be opened
		bool Done{ false };		// a read was tried
		bool Asleep{ false };	// spun down at the last attempt (Health is the reading before, if any)
	};

	// the disk's last reading; the first call starts its reads
	static Reading Get(uint32_t diskNumber);
};
