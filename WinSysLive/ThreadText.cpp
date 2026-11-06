#include "pch.h"
#include "ThreadCommands.h"

// the names of the thread states and wait reasons, apart from the commands (ThreadCommands.cpp) so the tests and the
// command line's /export threads take them

PCWSTR ThreadCommands::StateText(uint32_t state) {
	static PCWSTR const names[] = {
		L"Initialized", L"Ready", L"Running", L"Standby", L"Terminated", L"Waiting", L"Transition",
		L"Deferred Ready", L"Gate Wait", L"Waiting for Swap",
	};
	return state < _countof(names) ? names[state] : L"";
}

PCWSTR ThreadCommands::WaitReasonText(uint32_t reason) {
	// KWAIT_REASON; 7 to 13 are the Wr variants of 0 to 6 (named as the kernel and Process Explorer have them, so the
	// two can be told apart)
	static PCWSTR const names[] = {
		L"Executive", L"Free Page", L"Page In", L"Pool Allocation", L"Delay Execution", L"Suspended", L"User Request",
		L"WrExecutive", L"WrFreePage", L"WrPageIn", L"WrPoolAllocation", L"WrDelayExecution", L"WrSuspended", L"WrUserRequest",
		L"Event Pair", L"Queue", L"LPC Receive", L"LPC Reply", L"Virtual Memory", L"Page Out", L"Rendezvous",
		L"Keyed Event", L"Terminated", L"Process In Swap", L"CPU Rate Control", L"Callout Stack", L"Kernel",
		L"Resource", L"Push Lock", L"Mutex", L"Quantum End", L"Dispatch Int", L"Preempted", L"Yield Execution",
		L"Fast Mutex", L"Guarded Mutex", L"Rundown", L"Alert By Thread ID", L"Deferred Preempt", L"Physical Fault",
		L"I/O Ring", L"Mdl Cache", L"RCU",
	};
	return reason < _countof(names) ? names[reason] : L"";
}

bool ThreadCommands::IsSuspended(uint32_t state, uint32_t waitReason) {
	return state == 5 && (waitReason == 5 || waitReason == 12);
}
