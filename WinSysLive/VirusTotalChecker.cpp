#include "pch.h"
#include "VirusTotalChecker.h"
#include <WinSys/Helpers.h>
#include <dpapi.h>
#include <thread>

#pragma comment(lib, "crypt32")

namespace {
	constexpr uint64_t AnalysisInterval = 60000;	// ms between lookups after an upload
	constexpr int AnalysisAttempts = 15;
}

VirusTotalChecker& VirusTotalChecker::Get() {
	// never destroyed: the worker may still be in a request at exit
	static auto checker = new VirusTotalChecker;
	return *checker;
}

std::wstring VirusTotalChecker::Key(std::wstring const& path) {
	auto key = path;
	::CharLowerBuff(key.data(), (DWORD)key.size());
	return key;
}

void VirusTotalChecker::SetApiKey(std::wstring key) {
	{
		std::lock_guard lock(m_Lock);
		if (key == m_ApiKey)
			return;
		m_ApiKey = std::move(key);
		// another key may see more (or the first one was wrong): the failures are tried again
		std::erase_if(m_Results, [](auto& r) { return r.second.State == State::Error || r.second.State == State::NoKey; });
	}
	m_Wake.notify_all();
	Notify();
}

bool VirusTotalChecker::HasApiKey() const {
	std::lock_guard lock(m_Lock);
	return !m_ApiKey.empty();
}

void VirusTotalChecker::SetRequestsPerMinute(int count) {
	std::lock_guard lock(m_Lock);
	m_PerMinute = std::clamp(count, 1, 1000);
}

void VirusTotalChecker::AddListener(HWND hWnd) {
	std::lock_guard lock(m_Lock);
	m_Listeners.insert(hWnd);
}

void VirusTotalChecker::RemoveListener(HWND hWnd) {
	std::lock_guard lock(m_Lock);
	m_Listeners.erase(hWnd);
}

void VirusTotalChecker::Notify() {
	std::vector<HWND> listeners;
	{
		std::lock_guard lock(m_Lock);
		listeners.assign(m_Listeners.begin(), m_Listeners.end());
	}
	for (auto hWnd : listeners)
		::PostMessage(hWnd, WM_VIRUSTOTAL, 0, 0);
}

void VirusTotalChecker::StartWorker() {
	// with the lock held
	if (m_Started)
		return;
	m_Started = true;
	std::thread([this] { Run(); }).detach();
}

VirusTotalChecker::Result VirusTotalChecker::Query(std::wstring const& path) {
	if (path.empty())
		return { State::Error };
	std::lock_guard lock(m_Lock);
	if (m_ApiKey.empty())
		return { State::NoKey };
	auto key = Key(path);
	if (auto it = m_Results.find(key); it != m_Results.end()) {
		if (it->second.State == State::Queued) {
			// asked for again (shown again): to the front
			auto pending = std::ranges::find_if(m_Pending, [&](auto& p) { return p.Job == Job::Lookup && p.Due == 0 && Key(p.Path) == key; });
			if (pending != m_Pending.end() && pending != m_Pending.begin()) {
				auto job = std::move(*pending);
				m_Pending.erase(pending);
				m_Pending.push_front(std::move(job));
			}
		}
		return it->second;
	}
	m_Results[key] = { State::Queued };
	m_Pending.push_front({ path, Job::Lookup });
	StartWorker();
	m_Wake.notify_all();
	return { State::Queued };
}

std::optional<VirusTotalChecker::Result> VirusTotalChecker::Peek(std::wstring const& path) const {
	std::lock_guard lock(m_Lock);
	auto it = m_Results.find(Key(path));
	if (it == m_Results.end())
		return std::nullopt;
	return it->second;
}

void VirusTotalChecker::Recheck(std::wstring const& path) {
	{
		std::lock_guard lock(m_Lock);
		auto key = Key(path);
		// the hash's report goes too, or it would be reused
		if (auto it = m_Results.find(key); it != m_Results.end() && !it->second.Sha256.empty())
			m_ByHash.erase(it->second.Sha256);
		m_Results.erase(key);
		std::erase_if(m_Pending, [&](auto& p) { return Key(p.Path) == key; });
	}
	(void)Query(path);
	Notify();
}

void VirusTotalChecker::Submit(std::wstring const& path) {
	{
		std::lock_guard lock(m_Lock);
		auto key = Key(path);
		std::erase_if(m_Pending, [&](auto& p) { return Key(p.Path) == key; });
		auto& result = m_Results[key];
		result.State = State::Uploading;
		m_Pending.push_front({ path, Job::Upload });
		StartWorker();
	}
	m_Wake.notify_all();
	Notify();
}

void VirusTotalChecker::Pace(std::unique_lock<std::mutex>& lock) {
	// the free API: 4 requests a minute
	for (;;) {
		auto interval = 60000ull / m_PerMinute;
		auto now = ::GetTickCount64();
		if (m_LastRequest == 0 || now - m_LastRequest >= interval)
			break;
		m_Wake.wait_for(lock, std::chrono::milliseconds(interval - (now - m_LastRequest)));
	}
	m_LastRequest = ::GetTickCount64();
}

void VirusTotalChecker::Run() {
	std::unique_lock lock(m_Lock);
	for (;;) {
		// the first job that's due, else wait for one (or the next due)
		auto now = ::GetTickCount64();
		auto job = std::ranges::find_if(m_Pending, [&](auto& p) { return p.Due <= now; });
		if (job == m_Pending.end() || m_ApiKey.empty()) {
			uint64_t next = UINT64_MAX;
			for (auto& p : m_Pending)
				next = (std::min)(next, p.Due);
			if (next == UINT64_MAX || m_ApiKey.empty())
				m_Wake.wait(lock);
			else
				m_Wake.wait_for(lock, std::chrono::milliseconds(next - now));
			continue;
		}
		auto pending = std::move(*job);
		m_Pending.erase(job);
		auto key = Key(pending.Path);
		auto apiKey = m_ApiKey;

		// the hash, once per file
		auto& known = m_Results[key];
		std::wstring sha256 = known.Sha256;
		if (sha256.empty()) {
			known.State = State::Hashing;
			lock.unlock();
			auto hash = WinSys::FileHash::Sha256(pending.Path);
			auto error = hash ? 0 : ::GetLastError();
			lock.lock();
			auto& result = m_Results[key];
			if (!hash) {
				result.State = State::Error;
				result.FileError = error;
				lock.unlock();
				Notify();
				lock.lock();
				continue;
			}
			result.Sha256 = sha256 = *hash;
		}

		if (pending.Job == Job::Lookup) {
			// the same file under another path was looked up already
			if (auto it = m_ByHash.find(sha256); it != m_ByHash.end() && pending.Attempts == 0) {
				auto& result = m_Results[key];
				result.Report = it->second;
				result.State = State::Found;
				lock.unlock();
				Notify();
				lock.lock();
				continue;
			}
			Pace(lock);
			m_Results[key].State = pending.Attempts ? State::Analyzing : State::Checking;
			lock.unlock();
			Notify();
			auto report = WinSys::VirusTotal::LookupHash(apiKey, sha256);
			lock.lock();
			auto& result = m_Results[key];
			result.Report = report;
			if (report.Status == WinSys::VirusTotalStatus::Found && report.LastAnalysis == 0 && pending.Attempts) {
				// known, but its analysis isn't done
				report.Status = WinSys::VirusTotalStatus::NotFound;
			}
			switch (report.Status) {
				case WinSys::VirusTotalStatus::Found:
					result.State = State::Found;
					m_ByHash[sha256] = report;
					break;
				case WinSys::VirusTotalStatus::NotFound:
					if (pending.Attempts && pending.Attempts < AnalysisAttempts) {
						// an upload being analyzed: again in a minute
						result.State = State::Analyzing;
						m_Pending.push_back({ pending.Path, Job::Lookup, ::GetTickCount64() + AnalysisInterval, pending.Attempts + 1 });
					}
					else
						result.State = State::NotFound;
					break;
				default:
					result.State = State::Error;
					break;
			}
		}
		else {
			Pace(lock);
			m_Results[key].State = State::Uploading;
			lock.unlock();
			Notify();
			auto report = WinSys::VirusTotal::UploadFile(apiKey, pending.Path);
			lock.lock();
			auto& result = m_Results[key];
			result.Report = report;
			if (report.Status == WinSys::VirusTotalStatus::Uploaded) {
				result.State = State::Analyzing;
				m_ByHash.erase(sha256);
				m_Pending.push_back({ pending.Path, Job::Lookup, ::GetTickCount64() + AnalysisInterval, 1 });
			}
			else
				result.State = State::Error;
		}
		lock.unlock();
		Notify();
		lock.lock();
	}
}

std::wstring VirusTotalChecker::Text(Result const& result) {
	switch (result.State) {
		case State::NoKey: return L"No API key";
		case State::Queued: return L"Queued";
		case State::Hashing: return L"Hashing...";
		case State::Checking: return L"Checking...";
		case State::Found:
			return std::format(L"{}/{}", result.Report.Malicious, result.Report.Engines);
		case State::NotFound: return L"Unknown";
		case State::Uploading: return L"Uploading...";
		case State::Analyzing: return L"Analyzing...";
		case State::Error:
			if (result.FileError)
				return L"Can't read: " + WinSys::Helpers::GetErrorText(result.FileError);
			switch (result.Report.Status) {
				case WinSys::VirusTotalStatus::BadKey: return L"Invalid API key";
				case WinSys::VirusTotalStatus::QuotaExceeded: return L"Quota exceeded";
				case WinSys::VirusTotalStatus::TooLarge: return L"Too large to upload";
			}
			if (result.Report.Error)
				return L"Error: " + WinSys::Helpers::GetErrorText(result.Report.Error);
			return std::format(L"Error (HTTP {})", result.Report.HttpStatus);
	}
	return L"";
}

int64_t VirusTotalChecker::SortKey(Result const& result) {
	if (result.State == State::Found)
		return (int64_t)result.Report.Malicious * 1000 + result.Report.Suspicious + 1000000;
	return result.State == State::NotFound ? 1 : 0;
}

bool VirusTotalChecker::IsDetected(Result const& result) {
	return result.State == State::Found && result.Report.Malicious > 0;
}

std::wstring VirusTotalChecker::Protect(std::wstring const& key) {
	if (key.empty())
		return {};
	DATA_BLOB in{ (DWORD)(key.size() * sizeof(wchar_t)), (BYTE*)key.data() }, out{};
	if (!::CryptProtectData(&in, L"VirusTotal API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};
	static const wchar_t hex[] = L"0123456789ABCDEF";
	std::wstring text;
	for (DWORD i = 0; i < out.cbData; i++) {
		text += hex[out.pbData[i] >> 4];
		text += hex[out.pbData[i] & 15];
	}
	::LocalFree(out.pbData);
	return text;
}

std::wstring VirusTotalChecker::Unprotect(std::wstring const& stored) {
	if (stored.empty() || stored.size() % 2)
		return {};
	std::vector<BYTE> data(stored.size() / 2);
	for (size_t i = 0; i < data.size(); i++)
		data[i] = (BYTE)wcstoul(stored.substr(i * 2, 2).c_str(), nullptr, 16);
	DATA_BLOB in{ (DWORD)data.size(), data.data() }, out{};
	if (!::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};
	std::wstring key((wchar_t const*)out.pbData, out.cbData / sizeof(wchar_t));
	::SecureZeroMemory(out.pbData, out.cbData);
	::LocalFree(out.pbData);
	return key;
}
