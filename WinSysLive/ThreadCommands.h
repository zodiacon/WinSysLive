#pragma once

#include <WinSys/Keys.h>
#include <WinSys/Thread.h>

//
// What the lists of threads share: the lower pane's and the property sheet's Threads page (CThreadsPage, one process's
// threads) and the Threads tab (CThreadsView, every process's). The states' and wait reasons' names, the state icons, and
// a thread row's context menu and its commands: Stack, Wait Chain, Suspend or Resume, Terminate (after asking), Set
// Priority and Affinity. The commands reopen the thread by its ID and refuse one created after the row's (its ID reused).
//
namespace ThreadCommands {
	// a thread a command acts on, with its process (for titles and stacks, and to leave the app's own alone)
	struct Target {
		WinSys::ProcessOrThreadKey Thread{};
		uint32_t Pid{ 0 };
		int64_t ProcessCreated{ 0 };
		std::wstring ProcessName;
		bool Suspended{ false };	// the menu offers Resume instead of Suspend
	};

	[[nodiscard]] PCWSTR StateText(uint32_t state);			// KTHREAD_STATE
	[[nodiscard]] PCWSTR WaitReasonText(uint32_t reason);	// KWAIT_REASON
	// waiting because it's suspended (Suspended or WrSuspended)
	[[nodiscard]] bool IsSuspended(uint32_t state, uint32_t waitReason);
	// the state's icon (IDI_*): running, ready, waiting, terminated, its stack paged out...
	[[nodiscard]] UINT StateIcon(uint32_t state);
	// not the app's own threads (suspending one could hang the app), nor the Idle and System processes'
	[[nodiscard]] bool CanControl(uint32_t pid);

	// a row's menu: the commands, then Copy (copyId: the caller's command); current: the thread's priority, checked
	void BuildMenu(CMenu& menu, Target const& target, std::optional<WinSys::ThreadPriorityLevel> current, UINT copyId, int iconSize);
	// an ID_THREAD_* command on the target; false for another command
	bool Execute(HWND owner, UINT id, Target const& target);
}
