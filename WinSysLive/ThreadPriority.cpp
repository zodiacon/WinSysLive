#include "pch.h"
#include "ThreadPriority.h"
#include <atomic>
#include <TlHelp32.h>
#include <wil/resource.h>

namespace {
	// the thread left alone (the main one); 0: new threads aren't lowered
	std::atomic<DWORD> s_MainThread{ 0 };

	// called by the loader on each new thread before its start routine, under the loader lock (SetThreadPriority is a
	// system call, safe there)
	void NTAPI OnThreadEvent(PVOID /*module*/, DWORD reason, PVOID /*reserved*/) {
		if (reason != DLL_THREAD_ATTACH)
			return;
		auto main = s_MainThread.load(std::memory_order_relaxed);
		if (main && ::GetCurrentThreadId() != main)
			::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_LOWEST);
	}
}

// the callback in the image's TLS directory: _tls_used (the CRT's) is linked in, and the pointer goes in .CRT$XL*,
// between the CRT's markers, kept by /INCLUDE as nothing refers to it (x64 and ARM64: no leading underscore)
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:WinSysLive_ThreadPriorityCallback")
#pragma const_seg(".CRT$XLW")
extern "C" const PIMAGE_TLS_CALLBACK WinSysLive_ThreadPriorityCallback = OnThreadEvent;
#pragma const_seg()

void ThreadPriority::LowerNewThreads(bool lower) {
	auto self = ::GetCurrentThreadId();
	s_MainThread = lower ? self : 0;
	if (!lower)
		return;
	// the threads made before (the loader's parallel-load workers, which the default thread pool goes on with): the
	// callback never sees them
	wil::unique_handle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
	if (!snapshot)
		return;
	THREADENTRY32 te{ sizeof(te) };
	auto pid = ::GetCurrentProcessId();
	for (auto ok = ::Thread32First(snapshot.get(), &te); ok; ok = ::Thread32Next(snapshot.get(), &te)) {
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
			continue;
		wil::unique_handle thread(::OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION, FALSE, te.th32ThreadID));
		// one raised on purpose (above normal) is left alone
		if (thread && ::GetThreadPriority(thread.get()) == THREAD_PRIORITY_NORMAL)
			::SetThreadPriority(thread.get(), THREAD_PRIORITY_LOWEST);
	}
}
