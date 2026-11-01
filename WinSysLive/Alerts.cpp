#include "pch.h"
#include "Alerts.h"
#include "resource.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/SystemInfo.h>
#include <WinSys/ServiceManager.h>
#include <WinSys/Helpers.h>
#include <ranges>

using namespace WinSys;

namespace {
	int64_t Now() {
		FILETIME ft;
		::GetSystemTimeAsFileTime(&ft);
		return (int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart;
	}
}

std::vector<CAlerts::ServiceChange> CAlerts::UpdateServices() {
	std::vector<ServiceChange> changes;
	auto services = ServiceManager::EnumServices(ServiceEnumType::AllServices);
	if (services.empty())
		return changes;	// the enumeration failed: what's known stays
	bool baseline = m_Services.empty();
	std::unordered_map<std::wstring, KnownService> current;
	current.reserve(services.size());
	for (auto& svc : services) {
		auto& status = svc.GetStatusProcess();
		auto state = status.CurrentState;
		auto it = m_Services.find(svc.GetName());
		KnownService known = it != m_Services.end() ? it->second : KnownService{ state, status.ProcessId };
		// pending states are waited out: a change when a service gets to running or stopped
		if (!baseline && it != m_Services.end() && state != known.State
			&& (state == WinSys::ServiceState::Running || state == WinSys::ServiceState::Stopped)) {
			changes.push_back({ svc.GetName(), svc.GetDisplayName(), state, state == WinSys::ServiceState::Running ? status.ProcessId : known.Pid,
				status.Win32ExitCode, status.ServiceSpecificExitCode });
		}
		if (state == WinSys::ServiceState::Running || state == WinSys::ServiceState::Stopped || state == WinSys::ServiceState::Paused)
			known.State = state;
		if (status.ProcessId)
			known.Pid = status.ProcessId;
		current[svc.GetName()] = known;
	}
	m_Services = std::move(current);
	return changes;
}

std::vector<AlertRule> CAlerts::Load() {
	return AlertRule::ParseRules(std::wstring(AppSettings::Get().AlertRules()));
}

void CAlerts::Save(std::vector<AlertRule> const& rules) {
	auto& settings = AppSettings::Get();
	settings.AlertRules(AlertRule::FormatRules(rules));
	settings.Save();
}

CAlerts::~CAlerts() {
	m_EventWatcher.Stop();
}

void CAlerts::OnEvent(EventLogEntry&& entry) {
	std::lock_guard lock(m_EventLock);
	if (m_Events.size() < MaxQueued)
		m_Events.push_back(std::move(entry));
}

void CAlerts::UpdateEventWatcher() {
	m_EventWatcher.Stop();
	{
		std::lock_guard lock(m_EventLock);
		m_Events.clear();
	}
	std::vector<std::pair<std::wstring, std::wstring>> queries;
	for (auto& log : AlertRule::EventLogs())
		if (auto query = AlertRule::EventQuery(m_Rules, log); !query.empty())
			queries.push_back({ log, query });
	// a log that can't be watched (Security without elevation) leaves the others
	if (!queries.empty())
		m_EventWatcher.Start(queries, *this);
}

void CAlerts::SetRules(std::vector<AlertRule> rules) {
	m_Rules = std::move(rules);
	UpdateEventWatcher();
	// the conditions start over (a rule edited is a new rule)
	m_States.assign(m_Rules.size(), {});
}

std::vector<AlertRule> const& CAlerts::GetRules() const {
	return m_Rules;
}

bool CAlerts::AnyEnabled() const {
	return std::ranges::any_of(m_Rules, &AlertRule::Enabled);
}

std::deque<AlertEvent> const& CAlerts::GetRecent() const {
	return m_Recent;
}

void CAlerts::ClearRecent() {
	m_Recent.clear();
}

std::vector<AlertEvent> CAlerts::Evaluate(WinSys::ProcessManager<ProcessInfoEx>& pm, std::optional<double> cpu) {
	std::vector<AlertEvent> events;
	bool baseline = !m_Baseline;
	m_Baseline = true;
	if (!AnyEnabled())
		return events;

	auto time = Now();
	auto tick = ::GetTickCount64();
	auto add = [&](std::wstring text, uint32_t pid = 0, bool running = false) {
		events.push_back({ time, std::move(text), pid, running });
	};

	// the system's values, read only if a rule needs them
	std::optional<double> commit, memory;
	bool needPerf = std::ranges::any_of(m_Rules, [](auto& r) {
		return r.Enabled && (r.Kind == AlertKind::CommitCharge || r.Kind == AlertKind::PhysicalMemory);
		});
	if (needPerf) {
		if (m_PageSize == 0)
			if (auto basic = SystemInfo::GetBasicInformation()) {
				m_PageSize = basic->PageSize;
				m_PhysicalTotal = (int64_t)basic->NumberOfPhysicalPages * basic->PageSize;
			}
		if (auto perf = SystemInfo::GetPerformanceInformation(); perf && m_PageSize) {
			if (perf->CommitLimit)
				commit = 100.0 * perf->CommittedPages / perf->CommitLimit;
			if (m_PhysicalTotal)
				memory = 100.0 * (m_PhysicalTotal - (int64_t)perf->AvailablePages * m_PageSize) / m_PhysicalTotal;
		}
	}

	// the services' changes, enumerated only while a service rule is on (off: the next read is a baseline again)
	std::vector<ServiceChange> serviceChanges;
	if (std::ranges::any_of(m_Rules, [](auto& r) { return r.Enabled && AlertRule::IsServiceKind(r.Kind); }))
		serviceChanges = UpdateServices();
	else
		m_Services.clear();

	// the events the watcher queued since the last update; an event fires its first matching rule only
	std::vector<EventLogEntry> logged;
	{
		std::lock_guard lock(m_EventLock);
		logged.swap(m_Events);
	}
	for (auto& e : logged) {
		auto rule = std::ranges::find_if(m_Rules, [&](auto& r) {
			return r.Enabled && r.MatchesEvent(e.Channel, e.Provider, e.EventId, e.IsAuditFailure());
			});
		if (rule == m_Rules.end())
			continue;
		auto line = e.Message.substr(0, e.Message.find_first_of(L"\r\n"));
		auto p = e.ProcessId ? pm.GetProcessById(e.ProcessId) : nullptr;
		bool running = p && p->Key.Created <= e.Time;
		add(rule->Kind == AlertKind::AuditFailure ? std::format(L"Security audit failure {} ({}): {}", e.EventId, e.Provider, line)
			: std::format(L"{} event {} from {}: {}", e.Channel, e.EventId, e.Provider, line), e.ProcessId, running);
	}

	auto processes = pm.GetProcesses();
	for (size_t i = 0; i < m_Rules.size(); i++) {
		auto& rule = m_Rules[i];
		auto& state = m_States[i];
		if (!rule.Enabled)
			continue;
		auto limit = AlertRule::FormatNumber(rule.Threshold) + (rule.Kind == AlertKind::ProcessPrivateBytes ? L" MB" : L"%");
		switch (rule.Kind) {
			case AlertKind::ProcessStarted:
				if (!baseline)
					for (auto& p : pm.GetNewProcesses())
						if (AlertRule::Matches(rule.Pattern, p->GetImageName()))
							add(std::format(L"{} ({}) started", p->GetImageName(), p->Id), p->Id, true);
				break;

			case AlertKind::ProcessExited:
				if (!baseline)
					for (auto& p : pm.GetTerminatedProcesses())
						if (AlertRule::Matches(rule.Pattern, p->GetImageName()))
							add(std::format(L"{} ({}) exited", p->GetImageName(), p->Id), p->Id, false);
				break;

			case AlertKind::SystemCpu:
			case AlertKind::CommitCharge:
			case AlertKind::PhysicalMemory:
			{
				auto value = rule.Kind == AlertKind::SystemCpu ? cpu : rule.Kind == AlertKind::CommitCharge ? commit : memory;
				if (!value)
					break;
				if (state.System.Update(*value > rule.Threshold, tick, rule.Seconds) && !baseline) {
					PCWSTR what = rule.Kind == AlertKind::SystemCpu ? L"CPU" : rule.Kind == AlertKind::CommitCharge ? L"Commit charge" : L"Physical memory";
					add(std::format(L"{} above {}{} (now {:.0f}%)", what, limit, AlertRule::ForSeconds(rule.Seconds), *value));
				}
				break;
			}

			case AlertKind::ProcessCpu:
			case AlertKind::ProcessPrivateBytes:
			{
				std::unordered_map<ProcessOrThreadKey, AlertCondition> current;
				for (auto& p : processes) {
					// Idle's CPU is the idle time
					if (p->Id == 0 || !AlertRule::Matches(rule.Pattern, p->GetImageName()))
						continue;
					double value = rule.Kind == AlertKind::ProcessCpu ? p->CPU / 10000.0 : p->NativeInfo->PrivatePageCount / 1048576.0;
					auto condition = state.Processes.contains(p->Key) ? state.Processes[p->Key] : AlertCondition{};
					if (condition.Update(value > rule.Threshold, tick, rule.Seconds) && !baseline) {
						auto now = rule.Kind == AlertKind::ProcessCpu ? std::format(L"{:.0f}%", value)
							: FormatHelper::FormatBytes((int64_t)p->NativeInfo->PrivatePageCount);
						add(std::format(L"{} ({}): {} above {}{} (now {})", p->GetImageName(), p->Id,
							rule.Kind == AlertKind::ProcessCpu ? L"CPU" : L"private bytes", limit, AlertRule::ForSeconds(rule.Seconds), now), p->Id, true);
					}
					// only the processes still running are kept
					if (condition.Since)
						current[p->Key] = condition;
				}
				state.Processes = std::move(current);
				break;
			}

			case AlertKind::ServiceStarted:
			case AlertKind::ServiceStopped:
			case AlertKind::ServiceFailed:
				for (auto& c : serviceChanges) {
					if (!AlertRule::Matches(rule.Pattern, c.Name) && !AlertRule::Matches(rule.Pattern, c.DisplayName))
						continue;
					bool failed = c.State == WinSys::ServiceState::Stopped && c.ExitCode != 0 && c.ExitCode != ERROR_SERVICE_NEVER_STARTED;
					auto label = std::format(L"{} ({})", c.DisplayName, c.Name);
					if (rule.Kind == AlertKind::ServiceStarted && c.State == WinSys::ServiceState::Running)
						add(std::format(L"Service {} started (PID {})", label, c.Pid), c.Pid, true);
					else if (rule.Kind == AlertKind::ServiceStopped && c.State == WinSys::ServiceState::Stopped)
						add(std::format(L"Service {} stopped{}", label, failed ? L" with an error" : L""), c.Pid, false);
					else if (rule.Kind == AlertKind::ServiceFailed && failed)
						add(std::format(L"Service {} failed: {}", label, c.ExitCode == ERROR_SERVICE_SPECIFIC_ERROR
							? std::format(L"service error {} (0x{:X})", c.ServiceExitCode, c.ServiceExitCode)
							: Helpers::GetErrorText(c.ExitCode)), c.Pid, false);
				}
				break;
		}
	}

	for (auto& e : events)
		m_Recent.push_back(e);
	while (m_Recent.size() > RecentLimit)
		m_Recent.pop_front();
	return events;
}

CAlertIcon::~CAlertIcon() {
	Remove();
}

void CAlertIcon::Init(HWND hWnd, UINT callbackMessage) {
	m_hWnd = hWnd;
	m_Message = callbackMessage;
}

void CAlertIcon::Show(bool show) {
	m_Wanted = show;
	if (show && !m_Shown)
		Add();
	else if (!show)
		Remove();
}

void CAlertIcon::AddAgain() {
	m_Shown = false;
	if (m_Wanted)
		Add();
}

bool CAlertIcon::Add() {
	NOTIFYICONDATA nid{ sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = Id;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = m_Message;
	nid.hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	wcscpy_s(nid.szTip, L"WinSysLive alerts");
	m_Shown = ::Shell_NotifyIcon(NIM_ADD, &nid);
	return m_Shown;
}

void CAlertIcon::Remove() {
	if (!m_Shown)
		return;
	NOTIFYICONDATA nid{ sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = Id;
	::Shell_NotifyIcon(NIM_DELETE, &nid);
	m_Shown = false;
}

void CAlertIcon::Notify(std::wstring const& title, std::wstring const& text) {
	if (!m_Shown && !Add())
		return;
	NOTIFYICONDATA nid{ sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = Id;
	nid.uFlags = NIF_INFO;
	nid.dwInfoFlags = NIIF_WARNING;
	wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
	wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
	::Shell_NotifyIcon(NIM_MODIFY, &nid);
}
