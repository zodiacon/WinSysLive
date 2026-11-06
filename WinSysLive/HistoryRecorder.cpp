#include "pch.h"
#include "HistoryRecorder.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/Helpers.h>

using namespace WinSys;

namespace {
	std::wstring Duration(int64_t ticks) {
		// 100 ns units, as h:mm:ss
		auto seconds = ticks / 10000000;
		return std::format(L"{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
	}
}

HistoryRecorder::HistoryRecorder(::ProcessManager& pm, uint32_t events) : m_Events(events), m_Since(Now()) {
	for (auto& p : pm.GetProcesses()) {
		m_BaselineProcesses.insert(p->Key);
		AddLife(pm, *p, true);
		if (p->Id > 4)
			m_Processes[p->Key] = Process::OpenById(p->Id, p->Key.Created, ProcessAccessMask::QueryLimitedInformation | ProcessAccessMask::Synchronize);
	}
	TakeServices();
}

int64_t HistoryRecorder::Now() {
	int64_t now;
	::GetSystemTimeAsFileTime((FILETIME*)&now);
	return now;
}

bool HistoryRecorder::IsEnabled(Event kind) const {
	return (m_Events & (1u << (int)kind)) != 0;
}

PCWSTR HistoryRecorder::EventName(Event kind) {
	switch (kind) {
		case Event::ProcessStarted: return L"Process Started";
		case Event::ProcessExited: return L"Process Exited";
		case Event::ServiceStarted: return L"Service Started";
		case Event::ServiceStopped: return L"Service Stopped";
		case Event::ServicePaused: return L"Service Paused or Resumed";
		case Event::ServiceInstalled: return L"Service Installed or Deleted";
	}
	return L"";
}

void HistoryRecorder::TakeServices() {
	auto services = ServiceManager::EnumServices(ServiceEnumType::AllServices);
	if (services.empty())
		return;	// enumeration failed: keep what we have
	m_Services.clear();
	for (auto& svc : services) {
		auto& status = svc.GetStatusProcess();
		m_Services[svc.GetName()] = { status.CurrentState, status.ProcessId, svc.GetDisplayName() };
	}
}

void HistoryRecorder::SetEvents(uint32_t events) {
	// the services' baseline went stale while their kinds were off
	if (events & ~m_Events)
		TakeServices();
	m_Events = events;
}

void HistoryRecorder::Clear() {
	m_FirstSequence += m_Lines.size();
	m_Lines.clear();
	m_Since = Now();
	// the running processes stay, as there when recording started
	std::erase_if(m_Lives, [](auto const& e) { return e.second.End != 0; });
	for (auto& [key, life] : m_Lives)
		life.Baseline = true;
}

void HistoryRecorder::AddLife(::ProcessManager& pm, ProcessInfoEx& p, bool baseline) {
	ProcessLife life;
	life.Key = p.Key;
	life.Start = p.Key.Created;
	life.Baseline = baseline;
	life.Name = p.GetImageName();
	if (p.Id > 4) {
		life.Path = p.GetImagePath();
		life.CommandLine = p.GetCommandLine();
		life.User = p.GetUser();
	}
	// the parent, if it's the one running with that ID (created before the child: the ID isn't reused)
	if (auto parent = pm.GetProcessById(p.ParentId); parent && parent->Key.Created <= p.Key.Created && p.Id != 0)
		life.Parent = parent->Key;
	else
		life.Parent = { 0, p.ParentId };
	m_Lives.insert_or_assign(p.Key, std::move(life));
}

void HistoryRecorder::MergeExited(ProcessCreationTracker& creations) {
	for (auto& c : creations.TakeExited()) {
		if (auto it = m_Lives.find(c.Key()); it != m_Lives.end()) {
			// seen by a snapshot: the exit's exact time and code
			it->second.End = c.ExitTime;
			if (c.ExitCode)
				it->second.ExitCode = c.ExitCode;
			continue;
		}
		// before recording started (or was cleared): not this recording's
		if (c.CreateTime < m_Since)
			continue;
		ProcessLife life;
		life.Key = c.Key();
		life.Start = c.CreateTime;
		life.End = c.ExitTime;
		life.ExitCode = c.ExitCode;
		life.Traced = true;
		life.Path = c.ImagePath.starts_with(L"\\") ? Helpers::GetDosNameFromNtName(c.ImagePath.c_str()) : c.ImagePath;
		if (life.Path.empty())
			life.Path = c.ImagePath;
		auto slash = life.Path.rfind(L'\\');
		life.Name = slash == std::wstring::npos ? life.Path : life.Path.substr(slash + 1);
		// the parent: the latest process with its ID created before it
		life.Parent = { 0, c.ParentPid };
		for (auto& [key, other] : m_Lives)
			if (key.Id == c.ParentPid && other.Start <= c.CreateTime && other.Start > life.Parent.Created && (!other.End || other.End >= c.CreateTime))
				life.Parent = key;
		m_Lives.insert_or_assign(life.Key, std::move(life));
	}
}

void HistoryRecorder::TrimLives() {
	if (m_Lives.size() <= LifeLimit)
		return;
	std::vector<std::pair<int64_t, WinSys::ProcessOrThreadKey>> exited;
	for (auto& [key, life] : m_Lives)
		if (life.End)
			exited.push_back({ life.End, key });
	std::ranges::sort(exited, [](auto& a, auto& b) { return a.first < b.first; });
	for (size_t i = 0; i < exited.size() && m_Lives.size() > LifeLimit; i++)
		m_Lives.erase(exited[i].second);
}

std::wstring HistoryRecorder::Format(Line const& line) {
	FILETIME local;
	SYSTEMTIME st{};
	if (::FileTimeToLocalFileTime((FILETIME const*)&line.Time, &local))
		::FileTimeToSystemTime(&local, &st);
	return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}  {:<18} {}", st.wYear, st.wMonth, st.wDay, st.wHour,
		st.wMinute, st.wSecond, st.wMilliseconds, line.Label, line.Text);
}

void HistoryRecorder::CollectProcesses(::ProcessManager& pm, std::vector<Line>& lines) {
	for (auto& p : pm.GetNewProcesses()) {
		// there when recording started: not a start
		if (m_BaselineProcesses.erase(p->Key))
			continue;
		AddLife(pm, *p, false);
		if (p->Id > 4 && !m_Processes.contains(p->Key))
			m_Processes[p->Key] = Process::OpenById(p->Id, p->Key.Created, ProcessAccessMask::QueryLimitedInformation | ProcessAccessMask::Synchronize);
		if (!IsEnabled(Event::ProcessStarted))
			continue;
		auto parent = pm.GetProcessNameById(p->ParentId);
		auto text = std::format(L"{} ({})  parent {} ({})", p->GetImageName(), p->Id, parent.empty() ? L"?" : parent, p->ParentId);
		if (auto& user = p->GetUser(); !user.empty())
			text += L"  " + user;
		if (auto& cmd = p->GetCommandLine(); !cmd.empty())
			text += L"  " + cmd;
		lines.push_back({ p->Key.Created, Event::ProcessStarted, false, p->Id, std::move(text), L"Process started" });
	}
	for (auto& p : pm.GetTerminatedProcesses()) {
		std::optional<uint32_t> code;
		if (auto it = m_Processes.find(p->Key); it != m_Processes.end()) {
			if (it->second)
				code = it->second->GetExitCode();
			m_Processes.erase(it);
		}
		m_BaselineProcesses.erase(p->Key);
		auto now = Now();
		// the trace's exact exit may have come already
		if (auto life = m_Lives.find(p->Key); life != m_Lives.end() && !life->second.End) {
			life->second.End = now;
			life->second.ExitCode = code;
		}
		if (!IsEnabled(Event::ProcessExited))
			continue;
		auto text = std::format(L"{} ({})  ran {}", p->GetImageName(), p->Id, Duration(now - p->Key.Created));
		bool warning = false;
		if (code) {
			text += std::format(L"  exit code {} (0x{:X})", (int32_t)*code, *code);
			warning = *code != 0;
		}
		lines.push_back({ now, Event::ProcessExited, warning, p->Id, std::move(text), L"Process exited" });
	}
}

void HistoryRecorder::CollectServices(std::vector<Line>& lines) {
	auto services = ServiceManager::EnumServices(ServiceEnumType::AllServices);
	if (services.empty())
		return;	// enumeration failed: keep what we have
	auto now = Now();
	std::unordered_set<std::wstring> current;
	current.reserve(services.size());
	for (auto& svc : services) {
		auto& name = svc.GetName();
		current.insert(name);
		auto& status = svc.GetStatusProcess();
		auto label = std::format(L"{} ({})", name, svc.GetDisplayName());
		auto it = m_Services.find(name);
		if (it == m_Services.end()) {
			if (IsEnabled(Event::ServiceInstalled))
				lines.push_back({ now, Event::ServiceInstalled, false, 0, label, L"Service installed" });
			m_Services[name] = { status.CurrentState, status.ProcessId, svc.GetDisplayName() };
			continue;
		}
		auto& known = it->second;
		auto state = status.CurrentState;
		// pending states are waited out: an event when a service gets to running, stopped or paused
		if (state == known.State || (state != ServiceState::Running && state != ServiceState::Stopped && state != ServiceState::Paused))
			continue;
		if (state == ServiceState::Running) {
			if (known.State == ServiceState::Paused) {
				if (IsEnabled(Event::ServicePaused))
					lines.push_back({ now, Event::ServicePaused, false, status.ProcessId, label, L"Service resumed" });
			}
			else if (IsEnabled(Event::ServiceStarted))
				lines.push_back({ now, Event::ServiceStarted, false, status.ProcessId, std::format(L"{}  PID {}", label, status.ProcessId), L"Service started" });
		}
		else if (state == ServiceState::Stopped) {
			if (IsEnabled(Event::ServiceStopped)) {
				auto text = label;
				bool failed = status.Win32ExitCode != 0 && status.Win32ExitCode != ERROR_SERVICE_NEVER_STARTED;
				if (failed)
					text += status.Win32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR
						? std::format(L"  failed: service error {} (0x{:X})", status.ServiceSpecificExitCode, status.ServiceSpecificExitCode)
						: L"  failed: " + Helpers::GetErrorText(status.Win32ExitCode);
				// the process it ran in, for Go to Process while that's still running
				lines.push_back({ now, Event::ServiceStopped, failed, known.Pid, std::move(text), failed ? L"Service failed" : L"Service stopped" });
			}
		}
		else if (IsEnabled(Event::ServicePaused))
			lines.push_back({ now, Event::ServicePaused, false, status.ProcessId, label, L"Service paused" });
		known.State = state;
		if (status.ProcessId)
			known.Pid = status.ProcessId;
	}
	// the ones that went
	for (auto it = m_Services.begin(); it != m_Services.end(); ) {
		if (current.contains(it->first)) {
			++it;
			continue;
		}
		if (IsEnabled(Event::ServiceInstalled))
			lines.push_back({ now, Event::ServiceInstalled, false, 0, std::format(L"{} ({})", it->first, it->second.DisplayName), L"Service deleted" });
		it = m_Services.erase(it);
	}
}

void HistoryRecorder::Update(::ProcessManager& pm, ProcessCreationTracker* creations) {
	std::vector<Line> lines;
	CollectProcesses(pm, lines);
	if (creations)
		MergeExited(*creations);
	TrimLives();
	// services cost an SCM enumeration: only while one of their kinds is on (the baseline goes stale meanwhile)
	if (IsEnabled(Event::ServiceStarted) || IsEnabled(Event::ServiceStopped) || IsEnabled(Event::ServicePaused) || IsEnabled(Event::ServiceInstalled))
		CollectServices(lines);
	if (lines.empty())
		return;
	std::ranges::stable_sort(lines, {}, &Line::Time);
	for (auto& line : lines)
		m_Lines.push_back(std::move(line));
	while (m_Lines.size() > LineLimit) {
		m_Lines.pop_front();
		m_FirstSequence++;
	}
}
