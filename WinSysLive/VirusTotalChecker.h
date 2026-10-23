#pragma once

#include <WinSys/VirusTotal.h>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>

//
// VirusTotal checks for the VirusTotal columns (Processes, the Modules pages, Kernel Modules) and the rows' VirusTotal
// commands: one worker thread for the whole app (the free API allows 4 requests a minute, whoever asks), shared by the UI
// thread and the property sheets' threads, so every member is thread-safe. A file is hashed (SHA-256) and only the hash
// is looked up; a file is uploaded only through Submit, which the commands call after a confirmation. Results are kept
// for the session by path (case-insensitive) and by hash (the same file elsewhere isn't looked up again). Query queues a
// file the first time it's asked for, at the front (the rows shown last come first), and the requests are paced by the
// RequestsPerMinute given. After an upload the hash is looked up again every minute until the analysis is done (for up
// to 15 minutes). Windows registered with AddListener are posted WM_VIRUSTOTAL as results come. The API key comes from the
// UI thread (SetApiKey); without one nothing is queued. At exit the worker is left to the process's end (a request can't
// be canceled), and the checker is never destroyed.
//
class VirusTotalChecker final {
public:
	enum class State {
		NoKey,		// no API key set
		Queued,		// waiting for its turn
		Hashing,
		Checking,	// the request is out
		Found,		// Report has the counts
		NotFound,	// VirusTotal doesn't know the file
		Uploading,
		Analyzing,	// uploaded; looked up again until analyzed
		Error,		// Report's error, a bad key, the quota...
	};

	struct Result {
		State State{ State::Queued };
		std::wstring Sha256;
		WinSys::VirusTotalReport Report;
		uint32_t FileError{ 0 };	// hashing or reading the file failed
	};

	// posted to listeners, wParam and lParam 0
	static constexpr UINT WM_VIRUSTOTAL = WM_APP + 0x180;

	static VirusTotalChecker& Get();

	void SetApiKey(std::wstring key);
	[[nodiscard]] bool HasApiKey() const;
	void SetRequestsPerMinute(int count);
	void AddListener(HWND hWnd);
	void RemoveListener(HWND hWnd);

	// the file's result so far, queuing it the first time (or moving it to the front while it waits)
	[[nodiscard]] Result Query(std::wstring const& path);
	// the result if there is one, without queuing
	[[nodiscard]] std::optional<Result> Peek(std::wstring const& path) const;
	// forgets the file's result and looks it up again
	void Recheck(std::wstring const& path);
	// uploads the file (the caller asked the user), then follows its analysis
	void Submit(std::wstring const& path);

	// the column's text: "0/72", "Not found", "Queued"...
	[[nodiscard]] static std::wstring Text(Result const& result);
	// for the column's sorting: detections first, then the known files, then the rest
	[[nodiscard]] static int64_t SortKey(Result const& result);
	// Found with detections: the row may be colored
	[[nodiscard]] static bool IsDetected(Result const& result);

	// DPAPI (CryptProtectData) for the current user, as hex, for the VirusTotalKey setting; empty on failure
	[[nodiscard]] static std::wstring Protect(std::wstring const& key);
	[[nodiscard]] static std::wstring Unprotect(std::wstring const& stored);

private:
	VirusTotalChecker() = default;

	enum class Job { Lookup, Upload };
	struct Pending {
		std::wstring Path;	// as given
		Job Job;
		uint64_t Due{ 0 };	// GetTickCount64; 0: now
		int Attempts{ 0 };	// lookups after an upload
	};

	void Run();
	void StartWorker();
	void Notify();
	// waits until a request may go out (the pacing), the lock released meanwhile
	void Pace(std::unique_lock<std::mutex>& lock);
	static std::wstring Key(std::wstring const& path);

	mutable std::mutex m_Lock;
	std::condition_variable m_Wake;
	std::wstring m_ApiKey;
	int m_PerMinute{ 4 };
	uint64_t m_LastRequest{ 0 };
	std::deque<Pending> m_Pending;
	std::unordered_map<std::wstring, Result> m_Results;	// by lowercase path
	std::unordered_map<std::wstring, WinSys::VirusTotalReport> m_ByHash;	// found reports
	std::unordered_set<HWND> m_Listeners;
	bool m_Started{ false };
};
