#pragma once

#include <optional>

// posted to a window when addresses it looked up (SymbolEngine::Lookup) are resolved
constexpr UINT WM_SYMBOLS_RESOLVED = WM_APP + 0x100;
// posted to a window when a stack it asked for (SymbolEngine::CaptureStack) is captured: lParam is a StackCapture*
// the window owns
constexpr UINT WM_STACK_CAPTURED = WM_APP + 0x101;

struct StackFrame {
	uint64_t Address;
	std::wstring Symbol;	// "module!function+0x10", "module+0x1234", or empty
	std::wstring Source;	// "file(line)" when the symbols have line numbers
	bool Is32Bit{ false };	// a WOW64 thread's 32-bit frame
};

struct StackCapture {
	uint32_t ThreadId;
	std::vector<StackFrame> Frames;	// innermost first; a WOW64 thread's native frames, then its 32-bit frames
	std::wstring Error;				// why there are no frames
};

//
// Symbols for addresses in processes (and the kernel, when elevated), through DbgHelp. DbgHelp isn't thread-safe, so
// every DbgHelp call is made on the engine's own thread; the methods may be called from any thread, and the results
// come back as messages. dbghelp.dll and symsrv.dll are loaded from the exe's folder (a post-build step copies them
// from the Debugging Tools), so the symbol server works; without them, the system's dbghelp.dll is used, which can't
// download symbols.
//
// The search path is _NT_SYMBOL_PATH, with TotalPE's symbol cache added if it names no cache of its own; without it,
// the Microsoft symbol server, cached in TotalPE's symbol cache.
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
