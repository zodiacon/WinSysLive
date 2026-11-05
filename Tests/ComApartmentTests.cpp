#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Thread.h>
#include <thread>
#include <atomic>
#include <TlHelp32.h>

using namespace WinSys;

// a thread's COM apartment (WinSys Thread::GetComFlags, ComApartmentText): the wording, this process's threads in an
// STA, the MTA and none, and a 32-bit (WOW64) process's UI thread, whose COM data is in its 32-bit TEB

TEST(ComApartment_Text) {
	CHECK_EQ(Thread::ComApartmentText(ComFlags::Error, true), std::wstring());
	CHECK_EQ(Thread::ComApartmentText(ComFlags::None, false), std::wstring());
	CHECK_EQ(Thread::ComApartmentText(ComFlags::None, true), std::wstring(L"Implicit MTA"));
	// COM's data without an apartment (the allocator used, say): not initialized
	CHECK_EQ(Thread::ComApartmentText(ComFlags::LocalTid | ComFlags::UuidInitialized, true), std::wstring(L"Implicit MTA"));
	CHECK_EQ(Thread::ComApartmentText(ComFlags::STA | ComFlags::LocalTid, true), std::wstring(L"STA"));
	CHECK_EQ(Thread::ComApartmentText(ComFlags::STA | ComFlags::ASTA, false), std::wstring(L"ASTA"));
	CHECK_EQ(Thread::ComApartmentText(ComFlags::MTA, false), std::wstring(L"MTA"));
	CHECK_EQ(Thread::ComApartmentText(ComFlags::MTA | ComFlags::WinRTInitialize, false), std::wstring(L"MTA (WinRT)"));
	CHECK_EQ(Thread::ComApartmentText(ComFlags::STA | ComFlags::InNeutralApartment, false), std::wstring(L"STA, in the NTA"));
	CHECK_EQ(Thread::ComApartmentText(ComFlags::STA | ComFlags::ThreadUninitializing, false), std::wstring(L"STA, uninitializing"));
}

namespace {
	// a thread that initialized COM (or didn't, with -1) and waits until told to go
	struct ComThread {
		HANDLE Ready{ ::CreateEvent(nullptr, TRUE, FALSE, nullptr) };
		HANDLE Done{ ::CreateEvent(nullptr, TRUE, FALSE, nullptr) };
		std::atomic<DWORD> Tid{ 0 };
		std::thread Worker;
		explicit ComThread(int model) : Worker([this, model] {
			if (model >= 0)
				::CoInitializeEx(nullptr, (DWORD)model);
			Tid = ::GetCurrentThreadId();
			::SetEvent(Ready);
			::WaitForSingleObject(Done, INFINITE);
			if (model >= 0)
				::CoUninitialize();
		}) {
			::WaitForSingleObject(Ready, 5000);
		}
		~ComThread() {
			::SetEvent(Done);
			Worker.join();
			::CloseHandle(Ready);
			::CloseHandle(Done);
		}
		ComFlags Flags() const {
			wil::unique_handle hThread(::OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, Tid));
			return Thread::GetComFlags(::GetCurrentProcess(), Thread::GetTebAddress(hThread.get()), false);
		}
	};

	bool Has(ComFlags flags, ComFlags flag) {
		return flags != ComFlags::Error && (flags & flag) == flag;
	}
}

TEST(ComApartment_LiveOwnThreads) {
	ComThread sta(COINIT_APARTMENTTHREADED), mta(COINIT_MULTITHREADED), none(-1);
	auto s = sta.Flags(), m = mta.Flags(), n = none.Flags();
	CHECK(Has(s, ComFlags::STA));
	CHECK(!Has(s, ComFlags::MTA));
	CHECK(Has(m, ComFlags::MTA));
	CHECK(!Has(m, ComFlags::STA));
	CHECK(n == ComFlags::None);
	CHECK_EQ(Thread::ComApartmentText(s, true), std::wstring(L"STA"));
	CHECK_EQ(Thread::ComApartmentText(m, true), std::wstring(L"MTA"));
	CHECK_EQ(Thread::ComApartmentText(n, true), std::wstring(L"Implicit MTA"));
	// the member, which opens the process itself, agrees
	auto thread = Thread::OpenById(sta.Tid, ThreadAccessMask::QueryLimitedInformation);
	CHECK(thread != nullptr);
	if (thread)
		CHECK(thread->GetComFlags() == s);
	CHECK(!Thread::IsWow64(::GetCurrentProcess()));
}

namespace {
	// 32-bit Windows PowerShell's threads' apartments, once it's running a command: its main thread is an MTA, and it
	// runs commands on an STA thread unless told -Mta (not notepad: Windows 11's SysWOW64\notepad.exe starts the 64-bit
	// Store app). Empty if it can't be started
	std::vector<std::wstring> PowerShellApartments(PCWSTR switches) {
		std::vector<std::wstring> apartments;
		WCHAR dir[MAX_PATH];
		if (!::GetSystemWow64Directory(dir, _countof(dir)))
			return apartments;	// a 32-bit Windows
		auto exe = std::wstring(dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
		STARTUPINFO si{ sizeof(si) };
		PROCESS_INFORMATION pi{};
		std::wstring line = L"\"" + exe + L"\" " + switches + L" -NoLogo -NoProfile -NonInteractive -Command Start-Sleep 30";
		if (!::CreateProcess(exe.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
			return apartments;
		CHECK(Thread::IsWow64(pi.hProcess));
		// the runtime and the runspace start first: until the threads' apartments stop changing, up to 15 seconds
		for (int i = 0; i < 15; i++) {
			::Sleep(1000);
			std::vector<std::wstring> now;
			wil::unique_handle snap(::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
			THREADENTRY32 te{ sizeof(te) };
			for (BOOL ok = ::Thread32First(snap.get(), &te); ok; ok = ::Thread32Next(snap.get(), &te)) {
				if (te.th32OwnerProcessID != pi.dwProcessId)
					continue;
				wil::unique_handle hThread(::OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID));
				auto flags = Thread::GetComFlags(pi.hProcess, Thread::GetTebAddress(hThread.get()), true);
				CHECK(flags != ComFlags::Error);
				now.push_back(Thread::ComApartmentText(flags, true));
			}
			bool stable = now.size() > 5 && now == apartments;
			apartments = std::move(now);
			if (stable)
				break;
		}
		::TerminateProcess(pi.hProcess, 0);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		return apartments;
	}
}

TEST(ComApartment_LiveWow64Process) {
	auto sta = PowerShellApartments(L"-Sta");
	if (sta.empty())
		return;
	CHECK(std::ranges::count(sta, std::wstring(L"STA")) == 1);
	CHECK(std::ranges::count(sta, std::wstring(L"MTA")) > 0);
	CHECK(std::ranges::count(sta, std::wstring(L"Implicit MTA")) > 0);
	auto mta = PowerShellApartments(L"-Mta");
	CHECK(!mta.empty());
	CHECK(std::ranges::count(mta, std::wstring(L"STA")) == 0);
	CHECK(std::ranges::count(mta, std::wstring(L"MTA")) > 0);
}
