#pragma once

#include <WinSys/ProcessNetwork.h>

//
// The app's per-process network tracker (an ETW session, which needs elevation), shared by the Processes view and the
// process property sheets, which run on threads of their own: its session is named after the app's process, so a
// second tracker would take it over. It starts when first acquired and stops when its last owner lets it go.
//
namespace NetworkTracking {
	// nullptr if it can't start, with the error from GetLastError() (ERROR_ACCESS_DENIED when not elevated)
	std::shared_ptr<WinSys::ProcessNetworkTracker> Acquire();
}
