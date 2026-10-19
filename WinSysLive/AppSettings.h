#pragma once

#include <Settings.h>

struct AppSettings : Settings {
	BEGIN_SETTINGS(AppSettings)
		SETTING(DarkMode, 0, SettingType::Bool);
		SETTING(UpdateInterval, 1000, SettingType::Int32);
		SETTING(ActiveTab, 0, SettingType::Int32);
		SETTING(ProcessTreeMode, 1, SettingType::Bool);
		SETTING(PerfPage, 0, SettingType::Int32);
		SETTING(ProcessLowerPane, 0, SettingType::Bool);
		SETTING(ProcessSplitterPos, 60, SettingType::Int32);	// percent of the height for the process list
		SETTING_STRING(ProcessColumns, L"");	// see ColumnLayout::Parse
		SETTING_STRING(ServiceColumns, L"");
	END_SETTINGS

	DEF_SETTING(DarkMode, int)
	DEF_SETTING(UpdateInterval, int)
	DEF_SETTING(ActiveTab, int)
	DEF_SETTING(ProcessTreeMode, int)
	DEF_SETTING(PerfPage, int)
	DEF_SETTING(ProcessLowerPane, int)
	DEF_SETTING(ProcessSplitterPos, int)
	DEF_SETTING_STRING(ProcessColumns)
	DEF_SETTING_STRING(ServiceColumns)

	static constexpr PCWSTR MainWindowPlacement = L"MainWindowPlacement";
};
