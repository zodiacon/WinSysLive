#pragma once

#include <WinSys/ServiceManager.h>

struct FormatHelper abstract final {
	// with thousands separators
	static std::wstring FormatNumber(int64_t value);
	// in KB, as Task Manager shows memory
	static std::wstring FormatSize(int64_t bytes);
	// B, KB, MB, GB...; empty for zero
	static std::wstring FormatBytes(uint64_t bytes);
	// 100 ns units, as h:mm:ss.mmm
	static std::wstring FormatTimeSpan(int64_t time);
	// ProcessInfo::CPU units (1/10000 of a percent); empty for zero
	static std::wstring FormatCpu(int32_t cpu);
	// FILETIME units, shown in local time; empty for zero
	static std::wstring FormatDateTime(int64_t time);
	// kernel base priority (as in SYSTEM_PROCESS_INFORMATION) to a priority class name
	static std::wstring FormatPriority(int basePriority);

	static PCWSTR ServiceStateToString(WinSys::ServiceState state);
	// as services.msc shows it, e.g. "Automatic (Delayed Start)"
	static std::wstring ServiceStartTypeToString(WinSys::ServiceConfiguration const& config);
	static std::wstring ServiceTypeToString(WinSys::ServiceType type);
};
