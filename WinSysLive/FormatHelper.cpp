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
