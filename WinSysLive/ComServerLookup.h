#pragma once

#include <WinSys/Com.h>
#include <atomic>
#include <memory>

//
// What COM server processes serve (WinSys ComServers::Identify), looked up on a thread of its own (COM initialized,
// MTA): the classes' registry index takes about 0.4 s the first time and COM+'s catalog more, too long for the UI
// thread. A request's result is filled in a moment later; the asker keeps it (ProcessInfoEx, per process) and checks
// Done on each use, so the views show it from the refresh after it arrives.
//
struct ComServerLookup final {
	struct Result {
		std::atomic<bool> Done{ false };	// Info is set (released after it's written)
		WinSys::ComServerInfo Info;
	};

	static std::shared_ptr<Result> Request(std::wstring commandLine, std::wstring imagePath, bool wow64);
};
