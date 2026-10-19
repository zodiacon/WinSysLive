#include "pch.h"
#include "FormatHelper.h"

std::wstring FormatHelper::FormatNumber(int64_t value) {
	auto text = std::to_wstring(value < 0 ? -value : value);
	for (int i = (int)text.size() - 3; i > 0; i -= 3)
		text.insert(i, 1, L',');
	return value < 0 ? L"-" + text : text;
}

std::wstring FormatHelper::FormatSize(int64_t bytes) {
	return FormatNumber(bytes >> 10) + L" K";
}

std::wstring FormatHelper::FormatBytes(uint64_t bytes) {
	if (bytes == 0)
		return L"";
	if (bytes < 1024)
		return std::format(L"{} B", bytes);
	static PCWSTR const units[] = { L"KB", L"MB", L"GB", L"TB", L"PB" };
	double value = bytes / 1024.0;
	int unit = 0;
	while (value >= 1024 && unit < _countof(units) - 1) {
		value /= 1024;
		unit++;
	}
	return std::format(L"{:.1f} {}", value, units[unit]);
}

std::wstring FormatHelper::FormatTimeSpan(int64_t time) {
	auto ms = time / 10000;
	return std::format(L"{}:{:02}:{:02}.{:03}", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
}

std::wstring FormatHelper::FormatCpu(int32_t cpu) {
	if (cpu <= 0)
		return L"";
	if (cpu < 100)
		return L"< 0.01";
	return std::format(L"{:.2f}", cpu / 10000.0);
}

std::wstring FormatHelper::FormatDateTime(int64_t time) {
	if (time == 0)
		return L"";

	FILETIME ft;
	ft.dwLowDateTime = (DWORD)time;
	ft.dwHighDateTime = (DWORD)(time >> 32);
	SYSTEMTIME st, local;
	if (!::FileTimeToSystemTime(&ft, &st) || !::SystemTimeToTzSpecificLocalTime(nullptr, &st, &local))
		return L"";

	WCHAR date[64], timeText[64];
	::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, _countof(date), nullptr);
	::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, nullptr, timeText, _countof(timeText));
	return std::wstring(date) + L" " + timeText;
}

PCWSTR FormatHelper::IntegrityLevelToString(WinSys::IntegrityLevel level) {
	using enum WinSys::IntegrityLevel;
	switch (level) {
		case Untrusted: return L"Untrusted";
		case Low: return L"Low";
		case Medium: return L"Medium";
		case MediumPlus: return L"Medium+";
		case High: return L"High";
		case System: return L"System";
		case Protected: return L"Protected";
	}
	return L"";
}

std::wstring FormatHelper::ProtectionToString(WinSys::ProcessProtection protection) {
	// PS_PROTECTED_TYPE: 1 is light, 2 full
	if (protection.Type == 0)
		return L"";

	using enum WinSys::ProcessProtectionSigner;
	PCWSTR signer = L"";
	switch (protection.Signer) {
		case Authenticode: signer = L"Authenticode"; break;
		case CodeGen: signer = L"CodeGen"; break;
		case Antimalware: signer = L"Antimalware"; break;
		case Lsa: signer = L"Lsa"; break;
		case Windows: signer = L"Windows"; break;
		case WinTcb: signer = L"WinTcb"; break;
		case WinSystem: signer = L"WinSystem"; break;
		case App: signer = L"App"; break;
	}
	return std::format(L"{} ({}){}", protection.Type == 1 ? L"PPL" : L"PP", signer, protection.Audit ? L" Audit" : L"");
}

PCWSTR FormatHelper::IoPriorityToString(WinSys::IoPriority priority) {
	using enum WinSys::IoPriority;
	switch (priority) {
		case VeryLow: return L"Very Low";
		case Low: return L"Low";
		case Normal: return L"Normal";
		case High: return L"High";
		case Critical: return L"Critical";
	}
	return L"";
}

std::wstring FormatHelper::FormatPriority(int basePriority) {
	switch (basePriority) {
		case 0: return L"";
		case 4: return L"Idle";
		case 6: return L"Below Normal";
		case 8: return L"Normal";
		case 10: return L"Above Normal";
		case 13: return L"High";
		case 24: return L"Realtime";
	}
	return std::to_wstring(basePriority);
}

PCWSTR FormatHelper::ServiceStateToString(WinSys::ServiceState state) {
	using enum WinSys::ServiceState;
	switch (state) {
		case Running: return L"Running";
		case Stopped: return L"Stopped";
		case Paused: return L"Paused";
		case StartPending: return L"Starting";
		case StopPending: return L"Stopping";
		case PausePending: return L"Pausing";
		case ContinuePending: return L"Continuing";
	}
	return L"Unknown";
}

std::wstring FormatHelper::ServiceStartTypeToString(WinSys::ServiceConfiguration const& config) {
	using enum WinSys::ServiceStartType;
	std::wstring text;
	switch (config.StartType) {
		case Boot: text = L"Boot"; break;
		case System: text = L"System"; break;
		case Auto: text = L"Automatic"; break;
		case Demand: text = L"Manual"; break;
		case Disabled: return L"Disabled";
		default: text = std::to_wstring((int)config.StartType); break;
	}
	bool delayed = config.StartType == Auto && config.DelayedAutoStart;
	if (delayed || config.TriggerStart) {
		text += L" (";
		if (delayed)
			text += config.TriggerStart ? L"Delayed Start, " : L"Delayed Start";
		if (config.TriggerStart)
			text += L"Trigger Start";
		text += L")";
	}
	return text;
}

std::wstring FormatHelper::ServiceTypeToString(WinSys::ServiceType type) {
	using enum WinSys::ServiceType;
	std::wstring text;
	if ((type & UserService) == UserService)
		text = (type & UserServiceInstance) == UserServiceInstance ? L"User Service Instance" : L"User Service";
	else if ((type & Win32SharedProcess) == Win32SharedProcess)
		text = L"Shared Process";
	else if ((type & Win32OwnProcess) == Win32OwnProcess)
		text = L"Own Process";
	else if ((type & KernelDriver) == KernelDriver)
		text = L"Kernel Driver";
	else if ((type & FileSystemDriver) == FileSystemDriver)
		text = L"File System Driver";

	if ((type & PackageService) == PackageService)
		text += L", Package";
	if ((type & InteractiveProcess) == InteractiveProcess)
		text += L", Interactive";
	return text;
}

PCWSTR FormatHelper::ServiceErrorControlToString(WinSys::ServiceErrorControl control) {
	using enum WinSys::ServiceErrorControl;
	switch (control) {
		case Ignore: return L"Ignore";
		case Normal: return L"Normal";
		case Severe: return L"Severe";
		case Critical: return L"Critical";
	}
	return L"";
}

std::wstring FormatHelper::ServiceTriggerToString(WinSys::ServiceTriggerType type, WinSys::ServiceTriggerAction action) {
	using enum WinSys::ServiceTriggerType;
	PCWSTR name;
	switch (type) {
		case DeviceInterfaceArrival: name = L"Device arrival"; break;
		case IpAddressAvailability: name = L"IP address"; break;
		case DomainJoin: name = L"Domain join"; break;
		case FirewallPortEvent: name = L"Firewall port"; break;
		case GroupPolicy: name = L"Group policy"; break;
		case NetworkEndpoint: name = L"Network endpoint"; break;
		case SystemStateChanged: name = L"System state"; break;
		case Custom: name = L"Custom (ETW)"; break;
		case Aggregate: name = L"Aggregate"; break;
		default: name = L"Unknown"; break;
	}
	return std::format(L"{}: {}", action == WinSys::ServiceTriggerAction::Stop ? L"Stop" : L"Start", name);
}
