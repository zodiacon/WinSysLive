#include "pch.h"
#include "Symbols.h"
#include <WinSys/Helpers.h>
#include <WinSys/SymbolHandler.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <variant>

// dbghelp.dll is delay-loaded (see the project's linker settings): the engine loads the exe folder's copy first

namespace {
	// processes whose DbgHelp sessions are kept open (the least recently used is closed)
	const size_t MaxSessions = 16;

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

	// a process's DbgHelp session (WinSys SymbolHandler)
	struct Session {
		std::unique_ptr<WinSys::SymbolHandler> Handler;
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
	std::unique_ptr<WinSys::SymbolHandler> Kernel;
	bool KernelTried{ false };	// its modules' bases need elevation: not tried again until the path changes
	bool Initialized{ false };

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
		WinSys::SymbolHandler::SetOptions();
	}

	std::wstring SearchPath() {
		std::lock_guard lock(Lock);
		return Path;
	}

	// after a path change: modules already loaded keep what they found with the old path
	void CloseSessions() {
		Sessions.clear();
		Kernel.reset();
		KernelTried = false;
	}

	// the process's session, opened if needed; nullptr (with the last error set) if it can't be opened, or the ID is
	// another process's now
	WinSys::SymbolHandler* GetSession(ProcessKey const& key) {
		auto now = ::GetTickCount64();
		if (auto it = Sessions.find(key); it != Sessions.end()) {
			it->second.LastUse = now;
			return it->second.Handler.get();
		}
		auto handler = WinSys::SymbolHandler::ForProcess(key.Pid, key.Created, SearchPath());
		if (!handler)
			return nullptr;
		if (Sessions.size() >= MaxSessions)
			Sessions.erase(std::ranges::min_element(Sessions, {}, [](auto& s) { return s.second.LastUse; }));
		return Sessions.insert({ key, Session{ std::move(handler), now } }).first->second.Handler.get();
	}

	// the kernel's modules' session; null if their bases aren't known (not elevated)
	WinSys::SymbolHandler* GetKernel() {
		if (!Kernel && !KernelTried) {
			KernelTried = true;
			Kernel = WinSys::SymbolHandler::ForKernel(SearchPath());
		}
		return Kernel.get();
	}

	std::wstring Symbol(WinSys::SymbolHandler* process, uint64_t address, std::wstring* source = nullptr) {
		if (WinSys::SymbolHandler::IsKernelAddress(address)) {
			auto kernel = GetKernel();
			return kernel ? kernel->GetSymbol(address) : L"";
		}
		return process ? process->GetSymbol(address, source) : L"";
	}

	std::wstring Resolve(AddressKey const& key) {
		return Symbol(WinSys::SymbolHandler::IsKernelAddress(key.Address) ? nullptr : GetSession(key.Process), key.Address);
	}

	DumpResult WriteDump(DumpWork const& work) {
		DumpResult result{ work.Path };
		if (!WinSys::MiniDump::Write(work.Process.Pid, work.Process.Created, work.Path, work.Full, result.Size)) {
			auto error = ::GetLastError();
			result.Error = error == ERROR_INVALID_PARAMETER ? L"The process is gone"
				: std::format(L"Writing the dump failed: {}", WinSys::Helpers::GetErrorText(error));
		}
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
		std::vector<WinSys::StackFrameAddress> frames;
		if (!session->CaptureStack(tid, frames))
			return fail(L"Can't capture the thread's stack");
		for (auto& f : frames) {
			StackFrame frame{ f.Address, L"", L"", f.Is32Bit };
			if (resolve)
				frame.Symbol = Symbol(session, f.Address, &frame.Source);
			// after resolving, which loads the module's symbols (unresolved, they're shown as not loaded)
			if (auto module = session->GetModuleSymbols(f.Address)) {
				frame.Module = std::move(module->ImagePath);
				frame.SymbolFile = std::move(module->SymbolFile);
				frame.Symbols = module->Kind;
			}
			capture.Frames.push_back(std::move(frame));
		}
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
	Impl::AddressKey key{ WinSys::SymbolHandler::IsKernelAddress(address) ? Impl::ProcessKey{} : Impl::ProcessKey{ pid, created }, address };
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
