#include "pch.h"
#include "NetworkTracking.h"
#include <mutex>

namespace {
	std::mutex s_Lock;
	std::weak_ptr<WinSys::ProcessNetworkTracker> s_Current;
}

std::shared_ptr<WinSys::ProcessNetworkTracker> NetworkTracking::Current() {
	std::lock_guard guard(s_Lock);
	return s_Current.lock();
}

std::shared_ptr<WinSys::ProcessNetworkTracker> NetworkTracking::Acquire() {
	auto& lock = s_Lock;
	auto& current = s_Current;

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
