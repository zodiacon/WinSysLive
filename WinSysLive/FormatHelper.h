#pragma once

struct FormatHelper abstract final {
	// with thousands separators
	static std::wstring FormatNumber(int64_t value);
	// in KB, as Task Manager shows memory
	static std::wstring FormatSize(int64_t bytes);
	// ProcessInfo::CPU units (1/10000 of a percent); empty for zero
	static std::wstring FormatCpu(int32_t cpu);
	// FILETIME units, shown in local time; empty for zero
	static std::wstring FormatDateTime(int64_t time);
	// kernel base priority (as in SYSTEM_PROCESS_INFORMATION) to a priority class name
	static std::wstring FormatPriority(int basePriority);
};
