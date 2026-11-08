#include "pch.h"
#include "LeakWatcher.h"

void LeakWatcher::Update(ProcessManager& pm) {
	auto now = ::GetTickCount64();
	if (m_Last && now - m_Last < Interval)
		return;
	m_Last = now;
	m_Samples++;

	std::unordered_map<WinSys::ProcessOrThreadKey, Entry> current;
	current.reserve(pm.GetProcessCount());
	for (auto& p : pm.GetProcesses()) {
		// Idle and System have no user-mode objects to leak, and Idle's handle count is the system's
		if (p->Id <= 4)
			continue;
		auto it = m_Processes.find(p->Key);
		auto entry = it != m_Processes.end() ? std::move(it->second) : Entry{};
		auto info = p->NativeInfo;
		entry.Series.Add(now, { (int64_t)info->HandleCount, (int64_t)info->PrivatePageCount, p->GetGdiObjects(), p->GetUserObjects() });
		entry.Findings = entry.Series.Analyze();
		current.emplace(p->Key, std::move(entry));
	}
	// only the running processes are kept
	m_Processes = std::move(current);
}

void LeakWatcher::Clear() {
	m_Processes.clear();
	m_Last = 0;
	m_Samples = 0;
}

std::vector<LeakFinding> const& LeakWatcher::GetFindings(WinSys::ProcessOrThreadKey const& key) const {
	static const std::vector<LeakFinding> none;
	auto it = m_Processes.find(key);
	return it == m_Processes.end() ? none : it->second.Findings;
}
