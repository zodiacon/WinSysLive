#pragma once

//
// The process runs at high priority (the main thread above normal, so the UI answers on a busy machine), and its other
// threads two levels lower: THREAD_PRIORITY_LOWEST, 11 in the High class instead of 13. A TLS callback lowers each new
// thread before its start routine runs, so this covers every thread: the app's workers, WinSys's ETW consumers and
// the threads WinHTTP, COM, RPC and the thread pool make. A thread may raise itself again (a property sheet's UI thread
// does). A worker that hangs, or a driver it waits on, then holds less of the machine.
//
struct ThreadPriority final {
	// from now on, threads created in the process start two levels below normal, and the ones already there at normal
	// are lowered too; the calling thread (the main one) is left as it is. false: new threads start at normal again
	// (for the tests; the lowered ones stay)
	static void LowerNewThreads(bool lower = true);
};
