#include "pch.h"
#include "Symbols.h"
#include <WinSys/Helpers.h>
#define _NO_CVCONST_H
#include <DbgHelp.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <variant>

// delay-loaded (see the project's linker settings): the engine loads the exe folder's copy first
#pragma comment(lib, "dbghelp")

namespace {
	// a stack copy is at most this much of the stack, from the stack pointer up
	const size_t MaxStackCopy = 1 << 20;
	const int MaxFrames = 256;
	// processes whose DbgHelp sessions are kept open (the least recently used is closed)
	const size_t MaxSessions = 16;
	// a process's module list is read again when an address is in none of its modules, at most this often
	const ULONGLONG ModuleRefreshInterval = 2000;
	// stands for the kernel in DbgHelp, which takes any value as a process handle when it doesn't invade the process
	HANDLE const KernelSession = (HANDLE)(ULONG_PTR)0x4B524E4C;

	bool IsKernelAddress(uint64_t address) {
		return (int64_t)address < 0;
	}

	std::wstring ExeFolder() {
		WCHAR path[MAX_PATH];
		::GetModuleFileName(nullptr, path, _countof(path));
		*wcsrchr(path, L'\\') = 0;
		return path;
	}

	std::wstring ExpandEnvironment(std::wstring const& text) {
		WCHAR expanded[MAX_PATH * 2];
		auto len = ::ExpandEnvironmentStrings(text.c_str(), expanded, _countof(expanded));
		return len > 0 && len <= _countof(expanded) ? expanded : text;
	}

	std::wstring Trim(std::wstring const& text, PCWSTR chars = L" \t\r\n;") {
		auto first = text.find_first_not_of(chars);
		return first == std::wstring::npos ? L"" : text.substr(first, text.find_last_not_of(chars) - first + 1);
	}

	// whether a symbol path has a symbol server (srv*..., symsrv*...), and whether it keeps downloaded files locally
	// (cache*dir, or srv*dir*server)
	struct PathParts {
		bool Server{ false }, Cache{ false };
	};
	PathParts Analyze(std::wstring const& path) {
		PathParts parts;
		size_t start = 0;
		while (start <= path.size()) {
			auto end = path.find(L';', start);
			auto element = path.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
			auto lower = element;
			::CharLowerBuff(lower.data(), (DWORD)lower.size());
			if (lower.starts_with(L"cache*"))
				parts.Cache = true;
			if (lower.starts_with(L"srv*") || lower.starts_with(L"symsrv*")) {
				parts.Server = true;
				// srv*server has no cache; srv*cache*server and symsrv*symsrv.dll*cache*server have
				auto stars = std::ranges::count(element, L'*');
				if (stars >= (lower.starts_with(L"srv*") ? 2 : 3))
					parts.Cache = true;
			}
			if (end == std::wstring::npos)
				break;
			start = end + 1;
		}
		return parts;
	}

	// "\SystemRoot\system32\ntoskrnl.exe", "\??\C:\..." to a Win32 path
	std::wstring KernelModulePath(char const* name) {
		std::wstring path(name, name + strlen(name));
		if (::_wcsnicmp(path.c_str(), L"\\SystemRoot\\", 12) == 0)
			return ExpandEnvironment(L"%SystemRoot%\\") + path.substr(12);
		if (path.starts_with(L"\\??\\"))
			return path.substr(4);
		return path;
	}

	// the stack copies the walk reads from instead of the process (the engine's thread walks one stack at a time)
	struct StackCopy {
		uint64_t Base;
		std::vector<BYTE> Bytes;
	};
	thread_local std::vector<StackCopy> const* t_Stacks;

	BOOL CALLBACK ReadMemory(HANDLE hProcess, DWORD64 address, PVOID buffer, DWORD size, LPDWORD read) {
		if (t_Stacks)
			for (auto& copy : *t_Stacks)
				if (address >= copy.Base && address + size <= copy.Base + copy.Bytes.size()) {
					memcpy(buffer, copy.Bytes.data() + (address - copy.Base), size);
					*read = size;
					return TRUE;
				}
		SIZE_T bytes = 0;
		auto ok = ::ReadProcessMemory(hProcess, (void const*)address, buffer, size, &bytes);
		*read = (DWORD)bytes;
		return ok || bytes > 0;
	}
}

std::wstring FormatAddress(uint64_t address) {
	return std::format(L"0x{:X}", address);
}

std::wstring TotalPESymbolCache() {
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, L"Software\\ScorpioSoftware\\TotalPE", KEY_READ) == ERROR_SUCCESS) {
		WCHAR value[MAX_PATH];
		ULONG chars = _countof(value);
		if (key.QueryStringValue(L"SymbolCache", value, &chars) == ERROR_SUCCESS && value[0])
			return ExpandEnvironment(value);
	}
	return ExpandEnvironment(L"%LOCALAPPDATA%\\TotalPE\\Symbols");
}

std::wstring GetEnvironmentSymbolPath() {
	WCHAR env[4096];
	auto len = ::GetEnvironmentVariable(L"_NT_SYMBOL_PATH", env, _countof(env));
	return len > 0 && len < _countof(env) ? Trim(env) : L"";
}

std::wstring BuildSymbolSearchPath(SymbolPathOptions const& options) {
	std::wstring path;
	auto add = [&](std::wstring const& part) {
		if (part.empty())
			return;
		if (!path.empty())
			path += L';';
		path += part;
	};
	auto cache = Trim(options.Cache, L" \t\r\n");
	cache = cache.empty() ? TotalPESymbolCache() : ExpandEnvironment(cache);

	// local locations first, so the servers are only asked for what they don't have
	add(Trim(options.ExtraPaths));
	PathParts env;
	if (options.UseEnvironment) {
		auto envPath = GetEnvironmentSymbolPath();
		env = Analyze(envPath);
		// its server's downloads go to the cache, unless it names one
		if (env.Server && !env.Cache)
			add(L"cache*" + cache);
		add(envPath);
	}
	auto server = Trim(options.ServerUrl, L" \t\r\n");
	if (options.UseServer && !env.Server && !server.empty())
		add(std::format(L"srv*{}*{}", cache, server));
	return path;
}

struct SymbolEngine::Impl {
	struct ProcessKey {
		uint32_t Pid;
		int64_t Created;
		auto operator<=>(ProcessKey const&) const = default;
	};
	struct AddressKey {
		ProcessKey Process;
		uint64_t Address;
		auto operator<=>(AddressKey const&) const = default;
	};
	struct ResolveWork {
		AddressKey Key;
	};
	struct StackWork {
		ProcessKey Process;
		uint32_t Tid;
		HWND Notify;
		bool Resolve;	// the frames' symbols too
	};
	struct DumpWork {
		ProcessKey Process;
		std::wstring Path;
		bool Full;
		HWND Notify;
	};
	using Work = std::variant<ResolveWork, StackWork, DumpWork>;

	// a process opened for DbgHelp; DbgHelp's session is keyed by the handle
	struct Session {
		wil::unique_handle Process;
		bool Wow64{ false };
		ULONGLONG LastRefresh{ 0 };
		ULONGLONG LastUse{ 0 };
	};

	// guarded by Lock
	std::mutex Lock;
	std::wstring Path;			// the search path
	bool PathChanged{ false };	// the sessions are to be closed, so new ones use the new path
	std::condition_variable Wake;
	std::deque<Work> Queue;
	std::map<AddressKey, std::wstring> Cache;
	std::map<AddressKey, std::set<HWND>> Pending;	// queued addresses, and the windows to tell
	bool Stopping{ false };
	std::thread Thread;

	// the engine's thread only
	std::map<ProcessKey, Session> Sessions;
	bool Initialized{ false };
	bool KernelLoaded{ false };
	ULONGLONG KernelRefresh{ 0 };

	Impl() {
		Thread = std::thread([this] { Run(); });
	}

	void Post(Work work) {
		std::lock_guard lock(Lock);
		if (Stopping)
			return;
		Queue.push_back(std::move(work));
		Wake.notify_one();
	}

	void Run() {
		std::set<HWND> notify;
		for (;;) {
			Work work;
			{
				std::unique_lock lock(Lock);
				// the windows waiting for resolved addresses are told once the queue empties, not once per address
				if (Queue.empty() && !notify.empty()) {
					for (auto hWnd : notify)
						::PostMessage(hWnd, WM_SYMBOLS_RESOLVED, 0, 0);
					notify.clear();
				}
				Wake.wait(lock, [&] { return Stopping || !Queue.empty(); });
				if (Stopping)
					break;
				work = std::move(Queue.front());
				Queue.pop_front();
			}
			if (!Initialized)
				Initialize();
			bool pathChanged;
			{
				std::lock_guard lock(Lock);
				pathChanged = std::exchange(PathChanged, false);
			}
			if (pathChanged)
				CloseSessions();

			if (auto resolve = std::get_if<ResolveWork>(&work)) {
				auto text = Resolve(resolve->Key);
				std::lock_guard lock(Lock);
				Cache[resolve->Key] = std::move(text);
				if (auto it = Pending.find(resolve->Key); it != Pending.end()) {
					notify.insert(it->second.begin(), it->second.end());
					Pending.erase(it);
				}
			}
			else if (auto dump = std::get_if<DumpWork>(&work)) {
				auto result = std::make_unique<DumpResult>(WriteDump(*dump));
				if (::PostMessage(dump->Notify, WM_DUMP_WRITTEN, 0, (LPARAM)result.get()))
					result.release();
			}
			else {
				auto& stack = std::get<StackWork>(work);
				auto capture = std::make_unique<StackCapture>(Capture(stack.Process, stack.Tid, stack.Resolve));
				if (::PostMessage(stack.Notify, WM_STACK_CAPTURED, 0, (LPARAM)capture.get()))
					capture.release();
			}
		}
		CloseSessions();
	}

	void Initialize() {
		Initialized = true;
		// the exe folder's dbghelp.dll, which finds symsrv.dll beside it; the delay-load helper then binds to it
		::LoadLibraryEx((ExeFolder() + L"\\dbghelp.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS |
			SYMOPT_LOAD_LINES | SYMOPT_INCLUDE_32BIT_MODULES | SYMOPT_OMAP_FIND_NEAREST);
	}

	std::wstring SearchPath() {
		std::lock_guard lock(Lock);
		return Path;
	}

	// after a path change: modules already loaded keep what they found with the old path
	void CloseSessions() {
		for (auto& [key, session] : Sessions)
			::SymCleanup(session.Process.get());
		Sessions.clear();
		if (KernelLoaded) {
			::SymCleanup(KernelSession);
			KernelLoaded = false;
		}
	}

	// the process's session, opened if needed; nullptr (with the last error set) if it can't be opened, or the ID is
	// another process's now
	Session* GetSession(ProcessKey const& key) {
		auto now = ::GetTickCount64();
		if (auto it = Sessions.find(key); it != Sessions.end()) {
			it->second.LastUse = now;
			return &it->second;
		}

		wil::unique_handle hProcess(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, key.Pid));
		if (!hProcess)
			return nullptr;
		FILETIME created, exited, kernel, user;
		if (key.Created && ::GetProcessTimes(hProcess.get(), &created, &exited, &kernel, &user)
			&& (int64_t)ULARGE_INTEGER{ created.dwLowDateTime, created.dwHighDateTime }.QuadPart != key.Created) {
			::SetLastError(ERROR_INVALID_PARAMETER);
			return nullptr;
		}
		// invading reads the module list; with deferred loads, symbols are loaded only when they're needed
		if (!::SymInitializeW(hProcess.get(), SearchPath().c_str(), TRUE))
			return nullptr;

		if (Sessions.size() >= MaxSessions) {
			auto oldest = std::ranges::min_element(Sessions, {}, [](auto& s) { return s.second.LastUse; });
			::SymCleanup(oldest->second.Process.get());
			Sessions.erase(oldest);
		}
		Session session;
		BOOL wow64 = FALSE;
		::IsWow64Process(hProcess.get(), &wow64);
		session.Wow64 = wow64;
		session.Process = std::move(hProcess);
		session.LastUse = session.LastRefresh = now;
		return &Sessions.insert({ key, std::move(session) }).first->second;
	}

	// the kernel's modules, at their bases (which only an elevated process gets)
	bool LoadKernel() {
		auto now = ::GetTickCount64();
		if (KernelLoaded && now - KernelRefresh < ModuleRefreshInterval)
			return true;
		KernelRefresh = now;
		ULONG size = 1 << 16;
		std::unique_ptr<BYTE[]> buffer;
		NTSTATUS status;
		do {
			buffer = std::make_unique<BYTE[]>(size);
			status = ::NtQuerySystemInformation(SystemModuleInformation, buffer.get(), size, &size);
		} while (status == STATUS_INFO_LENGTH_MISMATCH);
		if (!NT_SUCCESS(status))
			return KernelLoaded;
		auto modules = reinterpret_cast<RTL_PROCESS_MODULES const*>(buffer.get());
		if (modules->NumberOfModules == 0 || !modules->Modules[0].ImageBase)
			return KernelLoaded;
		if (!KernelLoaded) {
			if (!::SymInitializeW(KernelSession, SearchPath().c_str(), FALSE))
				return false;
			KernelLoaded = true;
		}
		// a module already loaded fails, which is fine: drivers loaded since are added
		for (ULONG i = 0; i < modules->NumberOfModules; i++) {
			auto& m = modules->Modules[i];
			auto path = KernelModulePath((char const*)m.FullPathName);
			::SymLoadModuleExW(KernelSession, nullptr, path.c_str(), nullptr, (DWORD64)m.ImageBase, m.ImageSize, nullptr, 0);
		}
		return true;
	}

	// "module!function+0x10", "module+0x1234", or empty if the address is in no known module
	std::wstring Format(HANDLE hSession, uint64_t address, std::wstring* source = nullptr) {
		IMAGEHLP_MODULEW64 module{ sizeof(module) };
		if (!::SymGetModuleInfoW64(hSession, address, &module))
			return L"";
		auto slash = wcsrchr(module.ImageName, L'\\');
		std::wstring name = slash ? slash + 1 : module.ImageName;
		if (name.empty())
			name = module.ModuleName;

		BYTE buffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR)];
		auto symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
		symbol->MaxNameLen = MAX_SYM_NAME;
		DWORD64 displacement = 0;
		std::wstring text;
		if (::SymFromAddrW(hSession, address, &displacement, symbol))
			text = displacement ? std::format(L"{}!{}+0x{:X}", name, symbol->Name, displacement) : std::format(L"{}!{}", name, symbol->Name);
		else
			text = std::format(L"{}+0x{:X}", name, address - module.BaseOfImage);

		if (source) {
			IMAGEHLP_LINEW64 line{ sizeof(line) };
			DWORD lineDisplacement;
			if (::SymGetLineFromAddrW64(hSession, address, &lineDisplacement, &line))
				*source = std::format(L"{}({})", line.FileName, line.LineNumber);
		}
		return text;
	}

	// a process's address; its module list is read again (now and then) for a module loaded since
	std::wstring FormatInProcess(Session& session, uint64_t address, std::wstring* source = nullptr) {
		auto text = Format(session.Process.get(), address, source);
		if (text.empty() && ::GetTickCount64() - session.LastRefresh >= ModuleRefreshInterval) {
			session.LastRefresh = ::GetTickCount64();
			::SymRefreshModuleList(session.Process.get());
			text = Format(session.Process.get(), address, source);
		}
		return text;
	}

	std::wstring Resolve(AddressKey const& key) {
		if (IsKernelAddress(key.Address))
			return LoadKernel() ? Format(KernelSession, key.Address) : L"";
		auto session = GetSession(key.Process);
		return session ? FormatInProcess(*session, key.Address) : L"";
	}

	DumpResult WriteDump(DumpWork const& work) {
		DumpResult result{ work.Path };
		auto fail = [&](PCWSTR what) {
			result.Error = std::format(L"{}: {}", what, WinSys::Helpers::GetErrorText(::GetLastError()));
			return result;
		};
		// the handles' data needs PROCESS_DUP_HANDLE
		wil::unique_handle hProcess(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, work.Process.Pid));
		if (!hProcess)
			return fail(L"Can't open the process");
		FILETIME created, exited, kernel, user;
		if (::GetProcessTimes(hProcess.get(), &created, &exited, &kernel, &user)
			&& (int64_t)ULARGE_INTEGER{ created.dwLowDateTime, created.dwHighDateTime }.QuadPart != work.Process.Created) {
			::SetLastError(ERROR_INVALID_PARAMETER);
			return fail(L"The process is gone");
		}
		wil::unique_hfile file(::CreateFile(work.Path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!file)
			return fail(L"Can't create the file");
		auto type = MiniDumpWithHandleData | MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData
			| MiniDumpWithFullMemoryInfo;
		if (work.Full)
			type |= MiniDumpWithFullMemory | MiniDumpWithTokenInformation | MiniDumpIgnoreInaccessibleMemory;
		if (!::MiniDumpWriteDump(hProcess.get(), work.Process.Pid, file.get(), (MINIDUMP_TYPE)type, nullptr, nullptr, nullptr)) {
			auto error = ::GetLastError();
			file.reset();
			::DeleteFile(work.Path.c_str());
			// MiniDumpWriteDump returns an HRESULT as the last error
			::SetLastError(HRESULT_FACILITY(error) == FACILITY_WIN32 ? HRESULT_CODE(error) : error);
			return fail(L"Writing the dump failed");
		}
		LARGE_INTEGER size;
		if (::GetFileSizeEx(file.get(), &size))
			result.Size = size.QuadPart;
		return result;
	}

	StackCapture Capture(ProcessKey const& key, uint32_t tid, bool resolve) {
		StackCapture capture{ tid };
		auto fail = [&](PCWSTR what) {
			capture.Error = std::format(L"{}: {}", what, WinSys::Helpers::GetErrorText(::GetLastError()));
			return std::move(capture);
		};
		if (key.Pid == ::GetCurrentProcessId()) {
			// suspending one of the app's own threads could deadlock with this one or the UI
			capture.Error = L"The stacks of WinSysLive's own threads aren't captured";
			return capture;
		}
		if (key.Pid <= 4) {
			capture.Error = L"The thread has no user-mode stack";
			return capture;
		}
		auto session = GetSession(key);
		if (!session)
			return fail(L"Can't open the process");
		auto hProcess = session->Process.get();
		wil::unique_handle hThread(::OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid));
		if (!hThread)
			return fail(L"Can't open the thread");

		// the stack's top is in the TEB (NT_TIB); a WOW64 thread's 32-bit TEB is two pages after the 64-bit one
		THREAD_BASIC_INFORMATION tbi;
		NT_TIB tib{};
		if (NT_SUCCESS(::NtQueryInformationThread(hThread.get(), ThreadBasicInformation, &tbi, sizeof(tbi), nullptr)))
			::ReadProcessMemory(hProcess, tbi.TebBaseAddress, &tib, sizeof(tib), nullptr);
		NT_TIB32 tib32{};
		if (session->Wow64 && tbi.TebBaseAddress)
			::ReadProcessMemory(hProcess, (BYTE const*)tbi.TebBaseAddress + 0x2000, &tib32, sizeof(tib32), nullptr);

		// suspended only for the context and a copy of the stack: the walk (which may load symbols) runs on the copy
		std::vector<StackCopy> stacks;
		auto copyStack = [&](uint64_t sp, uint64_t base) {
			if (!sp || base <= sp)
				return;
			StackCopy copy{ sp };
			copy.Bytes.resize((size_t)(std::min)(base - sp, (uint64_t)MaxStackCopy));
			SIZE_T read = 0;
			::ReadProcessMemory(hProcess, (void const*)sp, copy.Bytes.data(), copy.Bytes.size(), &read);
			copy.Bytes.resize(read);
			stacks.push_back(std::move(copy));
		};
		CONTEXT context{};
		context.ContextFlags = CONTEXT_FULL;
#ifdef _M_X64
		WOW64_CONTEXT context32{};
		context32.ContextFlags = WOW64_CONTEXT_FULL;
		bool have32 = false;
#endif
		if (::SuspendThread(hThread.get()) == (DWORD)-1)
			return fail(L"Can't suspend the thread");
		bool ok = ::GetThreadContext(hThread.get(), &context);
		auto error = ::GetLastError();
		if (ok) {
#if defined(_M_X64)
			copyStack(context.Rsp, (uint64_t)tib.StackBase);
			if (session->Wow64 && ::Wow64GetThreadContext(hThread.get(), &context32)) {
				have32 = true;
				copyStack(context32.Esp, tib32.StackBase);
			}
#elif defined(_M_ARM64)
			copyStack(context.Sp, (uint64_t)tib.StackBase);
#else
			copyStack(context.Esp, (uint64_t)tib.StackBase);
#endif
		}
		::ResumeThread(hThread.get());
		if (!ok) {
			::SetLastError(error);
			return fail(L"Can't get the thread's context");
		}

		t_Stacks = &stacks;
		auto walk = [&](DWORD machine, void* contextRecord, uint64_t pc, uint64_t frame, uint64_t sp, bool is32) {
			STACKFRAME_EX sf{};
			sf.StackFrameSize = sizeof(sf);
			sf.AddrPC.Offset = pc;
			sf.AddrPC.Mode = AddrModeFlat;
			sf.AddrFrame.Offset = frame;
			sf.AddrFrame.Mode = AddrModeFlat;
			sf.AddrStack.Offset = sp;
			sf.AddrStack.Mode = AddrModeFlat;
			uint64_t previous = 0;
			for (int i = 0; i < MaxFrames; i++) {
				if (!::StackWalkEx(machine, hProcess, hThread.get(), &sf, contextRecord, ReadMemory, ::SymFunctionTableAccess64,
					::SymGetModuleBase64, nullptr, SYM_STKWALK_DEFAULT))
					break;
				if (sf.AddrPC.Offset == 0 || (i > 0 && sf.AddrPC.Offset == previous && sf.AddrStack.Offset == sp))
					break;
				previous = sf.AddrPC.Offset;
				sp = sf.AddrStack.Offset;
				capture.Frames.push_back({ sf.AddrPC.Offset, L"", L"", is32 });
			}
		};
#if defined(_M_X64)
		walk(IMAGE_FILE_MACHINE_AMD64, &context, context.Rip, context.Rsp, context.Rsp, false);
		if (have32)
			walk(IMAGE_FILE_MACHINE_I386, &context32, context32.Eip, context32.Ebp, context32.Esp, true);
#elif defined(_M_ARM64)
		walk(IMAGE_FILE_MACHINE_ARM64, &context, context.Pc, context.Fp, context.Sp, false);
#else
		walk(IMAGE_FILE_MACHINE_I386, &context, context.Eip, context.Ebp, context.Esp, false);
#endif
		t_Stacks = nullptr;

		for (auto& f : capture.Frames)
			if (resolve)
				f.Symbol = IsKernelAddress(f.Address) ? (LoadKernel() ? Format(KernelSession, f.Address) : L"")
				: FormatInProcess(*session, f.Address, &f.Source);
		if (capture.Frames.empty())
			capture.Error = L"No frames found";
		return capture;
	}
};

SymbolEngine& SymbolEngine::Get() {
	static SymbolEngine engine;
	return engine;
}

SymbolEngine::~SymbolEngine() {
	// Shutdown should have stopped the thread; one still running (or waiting) is left to the process's exit, with its
	// state, which it may still use
	if (m_Impl && m_Impl->Thread.joinable()) {
		m_Impl->Thread.detach();
		(void)m_Impl.release();
	}
}

SymbolEngine::Impl& SymbolEngine::GetImpl() {
	static std::once_flag once;
	std::call_once(once, [this] { m_Impl = std::make_unique<Impl>(); });
	return *m_Impl;
}

void SymbolEngine::Enable(bool enable) {
	m_Enabled = enable;
}

bool SymbolEngine::IsEnabled() const {
	return m_Enabled;
}

std::optional<std::wstring> SymbolEngine::Lookup(uint32_t pid, int64_t created, uint64_t address, HWND notify) {
	if (!m_Enabled || !address)
		return std::nullopt;
	auto& impl = GetImpl();
	// the kernel's addresses are the same in every process
	Impl::AddressKey key{ IsKernelAddress(address) ? Impl::ProcessKey{} : Impl::ProcessKey{ pid, created }, address };
	{
		std::lock_guard lock(impl.Lock);
		if (auto it = impl.Cache.find(key); it != impl.Cache.end())
			return it->second;
		auto [it, added] = impl.Pending.try_emplace(key);
		it->second.insert(notify);
		if (!added)
			return std::nullopt;
	}
	impl.Post(Impl::ResolveWork{ key });
	return std::nullopt;
}

void SymbolEngine::CaptureStack(uint32_t pid, int64_t created, uint32_t tid, HWND notify) {
	GetImpl().Post(Impl::StackWork{ { pid, created }, tid, notify, m_Enabled });
}

void SymbolEngine::WriteDump(uint32_t pid, int64_t created, std::wstring path, bool full, HWND notify) {
	GetImpl().Post(Impl::DumpWork{ { pid, created }, std::move(path), full, notify });
}

void SymbolEngine::SetSearchPath(std::wstring path) {
	auto& impl = GetImpl();
	std::lock_guard lock(impl.Lock);
	if (path == impl.Path)
		return;
	impl.Path = std::move(path);
	impl.PathChanged = true;
	// resolved again with the new path, as they're shown
	impl.Cache.clear();
}

void SymbolEngine::Shutdown() {
	if (!m_Impl)
		return;
	{
		std::lock_guard lock(m_Impl->Lock);
		m_Impl->Stopping = true;
		m_Impl->Wake.notify_one();
	}
	// a symbol download in progress isn't waited for: the process is exiting anyway
	auto handle = m_Impl->Thread.native_handle();
	if (::WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0)
		m_Impl->Thread.join();
	else {
		// its state goes with the process, as the thread may still use it
		m_Impl->Thread.detach();
		(void)m_Impl.release();
	}
}
