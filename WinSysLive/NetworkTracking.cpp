#include "pch.h"
#include "NetworkTracking.h"
#include <mutex>

std::shared_ptr<WinSys::ProcessNetworkTracker> NetworkTracking::Acquire() {
	static std::mutex lock;
	static std::weak_ptr<WinSys::ProcessNetworkTracker> current;

	std::lock_guard guard(lock);
	if (auto tracker = current.lock())
		return tracker;
	auto tracker = std::make_shared<WinSys::ProcessNetworkTracker>();
	if (!tracker->Start(L"WinSysLive Network")) {
		// the tracker's destructor stops what did start
		auto error = ::GetLastError();
		tracker.reset();
		::SetLastError(error);
		return nullptr;
	}
	current = tracker;
	return tracker;
}
