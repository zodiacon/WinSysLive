#pragma once

#include <WinSys/ProcessIntegrity.h>
#include <WinSys/Keys.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>

//
// The Processes view's image checks and executable memory counts (WinSys ProcessIntegrity) on a worker thread, as
// each reads the process and its file, and a walk of a large address space takes a while. A process (by its key: the
// same ID later is another) has its image checked once; its executable memory is counted again when asked for after
// MemoryMaxAge. The window given to Init is posted its message as each is done. Used from one thread (the owner's);
// the worker shares only its state, and the owner going doesn't wait for it (it finishes the process at hand and exits).
//
class IntegrityChecker final {
public:
	// a count older than this is taken again when asked for
	static constexpr ULONGLONG MemoryMaxAge = 30000;

	IntegrityChecker() = default;
	~IntegrityChecker();
	IntegrityChecker(IntegrityChecker const&) = delete;
	IntegrityChecker& operator=(IntegrityChecker const&) = delete;

	void Init(HWND notify, UINT message);
	// queued once per process
	void CheckImage(WinSys::ProcessOrThreadKey const& key);
	// queued if there's no count, or it's old
	void CountMemory(WinSys::ProcessOrThreadKey const& key);
	// nothing until it's done
	[[nodiscard]] std::optional<WinSys::ImageIntegrity> GetImage(WinSys::ProcessOrThreadKey const& key) const;
	[[nodiscard]] std::optional<WinSys::ExecutableMemory> GetMemory(WinSys::ProcessOrThreadKey const& key) const;
	// the process exited: its results go
	void Forget(WinSys::ProcessOrThreadKey const& key);
	// replaced, another file's or rewritten (not a file that's gone, nor one that couldn't be checked)
	[[nodiscard]] static bool IsTampered(WinSys::ImageIntegrity const& image);

private:
	struct Request {
		WinSys::ProcessOrThreadKey Key;
		bool Image;	// else the memory
	};
	struct Result {
		std::optional<WinSys::ImageIntegrity> Image;
		std::optional<WinSys::ExecutableMemory> Memory;
		ULONGLONG MemoryTime{ 0 };
	};
	struct State {
		std::mutex Lock;
		std::condition_variable_any Wake;
		std::deque<Request> Pending;
		std::unordered_map<WinSys::ProcessOrThreadKey, Result> Results;
		std::unordered_set<WinSys::ProcessOrThreadKey> Forgotten;	// exited while queued: not checked
		HWND Notify{ nullptr };
		UINT Message{ 0 };
	};

	static void Run(std::stop_token stop, std::shared_ptr<State> state);
	void Queue(Request request);

	std::shared_ptr<State> m_State{ std::make_shared<State>() };
	std::unordered_set<WinSys::ProcessOrThreadKey> m_ImageQueued;
	std::unordered_map<WinSys::ProcessOrThreadKey, ULONGLONG> m_MemoryQueued;	// when it was last queued
	std::jthread m_Worker;
};
