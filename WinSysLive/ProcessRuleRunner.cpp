#include "pch.h"
#include "ProcessRuleRunner.h"
#include "AppSettings.h"

std::vector<ProcessRule> CProcessRules::Load() {
	return ProcessRule::ParseRules(std::wstring(AppSettings::Get().ProcessRules()));
}

void CProcessRules::Save(std::vector<ProcessRule> const& rules) {
	auto& settings = AppSettings::Get();
	settings.ProcessRules(ProcessRule::FormatRules(rules));
	settings.Save();
}

void CProcessRules::Update(ProcessManager& pm) {
	bool first = m_First;
	m_First = false;
	if (std::ranges::none_of(m_Rules, &ProcessRule::Enabled))
		return;
	auto applyAll = [&](auto& processes) {
		// in the list's order: a later rule's settings win over an earlier one's
		for (auto& p : processes)
			for (auto& rule : m_Rules)
				if (rule.Enabled)
					Apply(rule, *p);
		};
	if (first)
		applyAll(pm.GetProcesses());
	else
		applyAll(pm.GetNewProcesses());
}

void CProcessRules::ApplyToRunning(ProcessManager& pm, ProcessRule const& rule) {
	if (!rule.Enabled)
		return;
	for (auto& p : pm.GetProcesses())
		Apply(rule, *p);
}

void CProcessRules::Apply(ProcessRule const& rule, ProcessInfoEx& p) {
	// Idle and System are the system's, with no priority of their own to set
	if (p.Id <= 4 || !rule.Matches(p.GetImageName()))
		return;
	std::vector<std::wstring> failures;
	if (auto process = WinSys::Process::OpenById(p.Id, p.Key.Created, WinSys::ProcessAccessMask::SetInformation))
		failures = rule.Apply(*process);
	else {
		// a process that exited since the snapshot (its ID maybe reused) isn't a failure of the rule's
		if (auto error = ::GetLastError(); error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND)
			return;
		failures.push_back(L"opening the process: " + ProcessRule::LastErrorText());
	}

	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);
	m_Recent.push_back({ (int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart,
		rule.ResultText(p.GetImageName(), p.Id, failures) });
	if (m_Recent.size() > MaxRecent)
		m_Recent.pop_front();
}
