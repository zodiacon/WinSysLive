#include "pch.h"
#include "Alerts.h"
#include "resource.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/SystemInfo.h>
#include <Shlwapi.h>
#include <ranges>

#pragma comment(lib, "shlwapi")

using namespace WinSys;

namespace {
	struct KindInfo {
		PCWSTR Key;		// in the setting: must not change
		PCWSTR Name;	// in the rule dialog
		bool Pattern, Threshold;
		PCWSTR Unit;
	};

	const KindInfo s_Kinds[] = {
		{ L"ProcessStarted", L"A process starts", true, false, L"" },
		{ L"ProcessExited", L"A process exits", true, false, L"" },
		{ L"SystemCpu", L"CPU usage is above", false, true, L"%" },
		{ L"Commit", L"Commit charge is above", false, true, L"%" },
		{ L"Memory", L"Physical memory in use is above", false, true, L"%" },
		{ L"ProcessCpu", L"A process's CPU usage is above", true, true, L"%" },
		{ L"ProcessPrivate", L"A process's private bytes are above", true, true, L"MB" },
	};
	static_assert(_countof(s_Kinds) == (int)AlertKind::_Count);

	KindInfo const& Info(AlertKind kind) {
		return s_Kinds[(int)kind];
	}

	std::wstring Number(double value) {
		return value == (int64_t)value ? std::to_wstring((int64_t)value) : std::format(L"{:.1f}", value);
	}

	std::wstring ForSeconds(int seconds) {
		return seconds <= 0 ? L"" : seconds == 1 ? L" for a second" : std::format(L" for {} seconds", seconds);
	}

	bool Matches(std::wstring const& pattern, std::wstring const& name) {
		return pattern.empty() || ::PathMatchSpecW(name.c_str(), pattern.c_str());
	}

	int64_t Now() {
		FILETIME ft;
		::GetSystemTimeAsFileTime(&ft);
		return (int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart;
	}
}

PCWSTR AlertRule::KindName(AlertKind kind) {
	return Info(kind).Name;
}

bool AlertRule::HasPattern(AlertKind kind) {
	return Info(kind).Pattern;
}

bool AlertRule::HasThreshold(AlertKind kind) {
	return Info(kind).Threshold;
}

PCWSTR AlertRule::Unit(AlertKind kind) {
	return Info(kind).Unit;
}

std::wstring AlertRule::Describe() const {
	auto who = Pattern.empty() ? std::wstring(L"Any process") : Pattern;
	auto limit = Number(Threshold) + (Kind == AlertKind::ProcessPrivateBytes ? L" MB" : L"%");
	switch (Kind) {
		case AlertKind::ProcessStarted: return who + L" starts";
		case AlertKind::ProcessExited: return who + L" exits";
		case AlertKind::SystemCpu: return L"CPU above " + limit + ForSeconds(Seconds);
		case AlertKind::CommitCharge: return L"Commit charge above " + limit + ForSeconds(Seconds);
		case AlertKind::PhysicalMemory: return L"Physical memory above " + limit + ForSeconds(Seconds);
		case AlertKind::ProcessCpu: return who + L": CPU above " + limit + ForSeconds(Seconds);
		case AlertKind::ProcessPrivateBytes: return who + L": private bytes above " + limit + ForSeconds(Seconds);
	}
	return L"";
}

std::vector<AlertRule> CAlerts::Load() {
	std::vector<AlertRule> rules;
	std::wstring text = AppSettings::Get().AlertRules();
	for (auto line : std::views::split(std::wstring_view(text), L'\n')) {
		std::vector<std::wstring> fields;
		for (auto field : std::views::split(std::wstring_view(line.begin(), line.end()), L'|'))
			fields.emplace_back(field.begin(), field.end());
		if (fields.size() != 5)
			continue;
		auto it = std::ranges::find_if(s_Kinds, [&](auto& k) { return fields[0] == k.Key; });
		if (it == std::end(s_Kinds))
			continue;
		AlertRule rule;
		rule.Kind = (AlertKind)(it - std::begin(s_Kinds));
		rule.Enabled = fields[1] == L"1";
		rule.Threshold = ::_wtof(fields[2].c_str());
		rule.Seconds = ::_wtoi(fields[3].c_str());
		rule.Pattern = fields[4];
		rules.push_back(std::move(rule));
	}
	return rules;
}

void CAlerts::Save(std::vector<AlertRule> const& rules) {
	std::wstring text;
	for (auto& r : rules) {
		if (!text.empty())
			text += L'\n';
		// a pattern can't hold the separators (a file name can't have '|'; line breaks dropped)
		auto pattern = r.Pattern;
		std::erase_if(pattern, [](wchar_t c) { return c == L'|' || c == L'\n' || c == L'\r'; });
		text += std::format(L"{}|{}|{}|{}|{}", Info(r.Kind).Key, r.Enabled ? 1 : 0, Number(r.Threshold), r.Seconds, pattern);
	}
	auto& settings = AppSettings::Get();
	settings.AlertRules(text);
	settings.Save();
}

void CAlerts::SetRules(std::vector<AlertRule> rules) {
	m_Rules = std::move(rules);
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

bool CAlerts::Condition::Update(bool holds, uint64_t now, int seconds) {
	if (!holds) {
		Since = 0;
		Fired = false;
		return false;
	}
	if (Since == 0)
		Since = now;
	if (Fired || now - Since < (uint64_t)(std::max)(seconds, 0) * 1000)
		return false;
	Fired = true;
	return true;
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

	auto processes = pm.GetProcesses();
	for (size_t i = 0; i < m_Rules.size(); i++) {
		auto& rule = m_Rules[i];
		auto& state = m_States[i];
		if (!rule.Enabled)
			continue;
		auto limit = Number(rule.Threshold) + (rule.Kind == AlertKind::ProcessPrivateBytes ? L" MB" : L"%");
		switch (rule.Kind) {
			case AlertKind::ProcessStarted:
				if (!baseline)
					for (auto& p : pm.GetNewProcesses())
						if (Matches(rule.Pattern, p->GetImageName()))
							add(std::format(L"{} ({}) started", p->GetImageName(), p->Id), p->Id, true);
				break;

			case AlertKind::ProcessExited:
				if (!baseline)
					for (auto& p : pm.GetTerminatedProcesses())
						if (Matches(rule.Pattern, p->GetImageName()))
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
					add(std::format(L"{} above {}{} (now {:.0f}%)", what, limit, ForSeconds(rule.Seconds), *value));
				}
				break;
			}

			case AlertKind::ProcessCpu:
			case AlertKind::ProcessPrivateBytes:
			{
				std::unordered_map<ProcessOrThreadKey, Condition> current;
				for (auto& p : processes) {
					// Idle's CPU is the idle time
					if (p->Id == 0 || !Matches(rule.Pattern, p->GetImageName()))
						continue;
					double value = rule.Kind == AlertKind::ProcessCpu ? p->CPU / 10000.0 : p->NativeInfo->PrivatePageCount / 1048576.0;
					auto condition = state.Processes.contains(p->Key) ? state.Processes[p->Key] : Condition{};
					if (condition.Update(value > rule.Threshold, tick, rule.Seconds) && !baseline) {
						auto now = rule.Kind == AlertKind::ProcessCpu ? std::format(L"{:.0f}%", value)
							: FormatHelper::FormatBytes((int64_t)p->NativeInfo->PrivatePageCount);
						add(std::format(L"{} ({}): {} above {}{} (now {})", p->GetImageName(), p->Id,
							rule.Kind == AlertKind::ProcessCpu ? L"CPU" : L"private bytes", limit, ForSeconds(rule.Seconds), now), p->Id, true);
					}
					// only the processes still running are kept
					if (condition.Since)
						current[p->Key] = condition;
				}
				state.Processes = std::move(current);
				break;
			}
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
