#pragma once

#include <optional>
#include <WinSys/SymbolHandler.h>

// posted to a window when addresses it looked up (SymbolEngine::Lookup) are resolved
constexpr UINT WM_SYMBOLS_RESOLVED = WM_APP + 0x100;
// posted to a window when a stack it asked for (SymbolEngine::CaptureStack) is captured: lParam is a StackCapture*
// the window owns
constexpr UINT WM_STACK_CAPTURED = WM_APP + 0x101;

// posted to a window when a dump it asked for (SymbolEngine::WriteDump) is written, or failed: lParam is a DumpResult*
// the window owns
constexpr UINT WM_DUMP_WRITTEN = WM_APP + 0x102;

struct DumpResult {
	std::wstring Path;
	std::wstring Error;	// empty if the dump was written
	uint64_t Size{ 0 };
};

struct StackFrame {
	uint64_t Address;
	std::wstring Symbol;	// "module!function+0x10", "module+0x1234", or empty
	std::wstring Source;	// "file(line)" when the symbols have line numbers
	bool Is32Bit{ false };	// a WOW64 thread's 32-bit frame
	std::wstring Module;	// the module's file; empty if the address is in no known module
	std::wstring SymbolFile;	// the PDB the module's symbols came from
	WinSys::ModuleSymbolsKind Symbols{ WinSys::ModuleSymbolsKind::NotLoaded };
};

struct StackCapture {
	uint32_t ThreadId;
	std::vector<StackFrame> Frames;	// innermost first; a WOW64 thread's native frames, then its 32-bit frames
	std::wstring Error;				// why there are no frames
};

//
// Symbols for addresses in processes (and the kernel, when elevated), through DbgHelp. DbgHelp isn't thread-safe, so
// every DbgHelp call is made on the engine's own thread; the methods may be called from any thread, and the results
// come back as messages. dbghelp.dll and symsrv.dll are loaded from the exe's folder (the Pavel Tools installer puts
// the Debugging Tools' copies there), so the symbol server works; without them, the system's dbghelp.dll is used, which can't
// download symbols.
//
// The search path comes from the UI thread (SetSearchPath, built by BuildSymbolSearchPath from the Symbol Settings); a
// new path closes the sessions and forgets the resolved addresses, so they're resolved again with it.
//
class SymbolEngine final {
public:
	static SymbolEngine& Get();

	// the Resolve Symbols option: while disabled, Lookup resolves nothing
	void Enable(bool enable);
	bool IsEnabled() const;

	// the address's symbol, if it's known: empty if it can't be resolved (show the address). nullopt if it isn't known
	// yet (or the engine is disabled): it's resolved on the engine's thread, and notify then gets WM_SYMBOLS_RESOLVED.
	// created: the process's creation time, which tells it from a later process with the same ID. Kernel addresses
	// are resolved from the kernel's modules, whatever the process
	std::optional<std::wstring> Lookup(uint32_t pid, int64_t created, uint64_t address, HWND notify);

	// captures the thread's user-mode stack and resolves its frames' symbols (while enabled); notify gets
	// WM_STACK_CAPTURED
	void CaptureStack(uint32_t pid, int64_t created, uint32_t tid, HWND notify);
	// the window is going: its captures aren't posted (they'd leak), and those still queued aren't made
	void CancelStacks(HWND notify);

	// writes a dump file of the process (MiniDumpWriteDump is DbgHelp's, so on the engine's thread); full: all its
	// memory, else a minidump (threads, modules, handles); notify gets WM_DUMP_WRITTEN
	void WriteDump(uint32_t pid, int64_t created, std::wstring path, bool full, HWND notify);

	// the DbgHelp search path; until it's set, symbols are looked up beside the images only
	void SetSearchPath(std::wstring path);

	// stops the engine's thread, before the app exits
	void Shutdown();

private:
	SymbolEngine() = default;
	~SymbolEngine();
	SymbolEngine(SymbolEngine const&) = delete;
	SymbolEngine& operator=(SymbolEngine const&) = delete;

	struct Impl;
	Impl& GetImpl();

	std::unique_ptr<Impl> m_Impl;
	std::atomic<bool> m_Enabled{ true };
};

// "0x7FF812345678", the way the lists show addresses
std::wstring FormatAddress(uint64_t address);

// where symbols are looked for (the Symbol Settings)
struct SymbolPathOptions {
	bool UseEnvironment{ true };	// _NT_SYMBOL_PATH, if it's set
	bool UseServer{ true };			// a symbol server, unless _NT_SYMBOL_PATH (in use) names one
	std::wstring ServerUrl{ DefaultSymbolServer };
	std::wstring Cache;				// downloads; empty: TotalPE's symbol cache
	std::wstring ExtraPaths;		// searched first; separated by semicolons

	static constexpr PCWSTR DefaultSymbolServer = L"https://msdl.microsoft.com/download/symbols";
};

// the DbgHelp search path: the extra paths, then _NT_SYMBOL_PATH (with "cache*" the cache added if it has a server
// but no cache of its own), then "srv*" the cache "*" the server, if _NT_SYMBOL_PATH has none
std::wstring BuildSymbolSearchPath(SymbolPathOptions const& options);
// TotalPE's symbol cache (its SymbolCache setting, else its default), expanded
std::wstring TotalPESymbolCache();
// _NT_SYMBOL_PATH, or empty if it isn't set
std::wstring GetEnvironmentSymbolPath();
