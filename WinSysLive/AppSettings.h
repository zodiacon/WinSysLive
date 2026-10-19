#pragma once

#include <Settings.h>

struct AppSettings : Settings {
	BEGIN_SETTINGS(AppSettings)
		SETTING(DarkMode, 0, SettingType::Bool);
		SETTING(UpdateInterval, 1000, SettingType::Int32);
		SETTING(ActiveTab, 0, SettingType::Int32);
		SETTING(ProcessTreeMode, 1, SettingType::Bool);
		SETTING(PerfPage, 0, SettingType::Int32);
		SETTING_STRING(ProcessColumns, L"");	// see CProcessesView::LoadColumns
	END_SETTINGS

	DEF_SETTING(DarkMode, int)
	DEF_SETTING(UpdateInterval, int)
	DEF_SETTING(ActiveTab, int)
	DEF_SETTING(ProcessTreeMode, int)
	DEF_SETTING(PerfPage, int)
	DEF_SETTING_STRING(ProcessColumns)

	static constexpr PCWSTR MainWindowPlacement = L"MainWindowPlacement";
};
