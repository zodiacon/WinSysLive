#pragma once

#include <WinSys/FileSignature.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_set>

//
// Verifies files' signatures (WinSys FileSignature: WinVerifyTrust, no revocation checks) on a worker thread, as a
// file can take seconds through the system catalogs. The worker starts with the first file queued; the window given to
// Init is posted its message as each file is done. The results are kept by path (case-insensitive). Used from one
// thread (the owner's). The worker shares only its state with the owner, which may go while it's verifying a file:
// it isn't waited for, but finishes that file and exits without posting.
//
class SignatureVerifier final {
public:
	SignatureVerifier() = default;
	~SignatureVerifier();
	SignatureVerifier(SignatureVerifier const&) = delete;
	SignatureVerifier& operator=(SignatureVerifier const&) = delete;

	void Init(HWND notify, UINT message);
	// queues the file, unless it was queued before
	void Verify(std::wstring const& path);
	// nothing until it's verified
	[[nodiscard]] std::optional<WinSys::SignatureInfo> GetResult(std::wstring const& path) const;
	// a file was queued: the worker runs
	[[nodiscard]] bool IsStarted() const {
		return m_Worker.joinable();
	}

private:
	struct State {
		std::mutex Lock;
		std::condition_variable_any Wake;
		std::deque<std::wstring> Pending;	// paths
		std::unordered_map<std::wstring, WinSys::SignatureInfo> Results;	// by lowercase path
		HWND Notify{ nullptr };
		UINT Message{ 0 };
	};

	static void Run(std::stop_token stop, std::shared_ptr<State> state);
	static std::wstring Lowercase(std::wstring text);

	std::shared_ptr<State> m_State{ std::make_shared<State>() };
	std::unordered_set<std::wstring> m_Queued;	// lowercase paths
	std::jthread m_Worker;
};
