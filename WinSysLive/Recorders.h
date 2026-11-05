#pragma once

#include "ProcessInfoEx.h"
#include "HistoryRecorder.h"
#include <WinSys/Ipc.h>
#include <WinSys/FileIo.h>
#include <WinSys/RegistryIo.h>
#include <WinSys/ProcessNetwork.h>
#include <WinSys/ProcessCreation.h>

//
// What records over time, shared by whoever needs it: the History tab's HistoryRecorder, the RPC/ALPC tab's IpcTracker
// the Disk tab's FileIoTracker and the Registry tab's RegistryIoTracker (ETW sessions named after the app's process, so a second one would take the first's
// session over). Each starts when first acquired and stops when its last owner lets it go. The tabs acquire theirs
// when first shown; with Options > Record from Startup the frame holds them all (and the network tracker,
// NetworkTracking) from its start (Holder), so a tab opened later shows what happened since. UI thread only (the
// network tracker, which the property sheets' threads share, has its own lock).
//
namespace Recorders {
	// nullptr if it can't start, with the error from GetLastError() (ERROR_ACCESS_DENIED when not elevated)
	std::shared_ptr<WinSys::IpcTracker> AcquireIpc();
	std::shared_ptr<WinSys::FileIoTracker> AcquireFileIo();
	std::shared_ptr<WinSys::RegistryIoTracker> AcquireRegistryIo();
	// who created each process (a spoofed parent: the Processes view's Tampered color, the Alerts' tampering rule)
	std::shared_ptr<WinSys::ProcessCreationTracker> AcquireProcessCreation();
	// takes its baseline from the snapshot when it starts
	std::shared_ptr<HistoryRecorder> AcquireHistory(ProcessManager& pm);

	// after each snapshot update, before the views refresh: the history takes the changes, the trackers forget the
	// processes that exited (their IDs may be reused)
	void OnRefresh(ProcessManager& pm);

	// the frame's references, while Record from Startup is on
	struct Holder {
		// acquires what isn't held yet; the trackers that can't start (not elevated) are left out
		void Start(ProcessManager& pm);
		void Stop();
		[[nodiscard]] bool IsHolding() const {
			return m_History != nullptr;
		}
		// the trackers that couldn't start: ERROR_ACCESS_DENIED when not elevated; 0 if they all did
		[[nodiscard]] DWORD GetTrackerError() const {
			return m_TrackerError;
		}

	private:
		std::shared_ptr<HistoryRecorder> m_History;
		std::shared_ptr<WinSys::IpcTracker> m_Ipc;
		std::shared_ptr<WinSys::FileIoTracker> m_FileIo;
		std::shared_ptr<WinSys::RegistryIoTracker> m_RegistryIo;
		std::shared_ptr<WinSys::ProcessNetworkTracker> m_Network;
		std::shared_ptr<WinSys::ProcessCreationTracker> m_Creation;
		DWORD m_TrackerError{ 0 };
	};
}
