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
		SETTING(ProcessKeepSelectionVisible, 0, SettingType::Bool);	// scroll to the selected process on every update
		SETTING(ProcessSplitterPos, 60, SettingType::Int32);	// percent of the height for the process list
		SETTING_STRING(ProcessColumns, L"");	// see ColumnLayout::Parse
		SETTING_STRING(ServiceColumns, L"");
		SETTING_STRING(ThreadColumns, L"");
		SETTING_STRING(ModuleColumns, L"");
		SETTING_STRING(HandleColumns, L"");
		SETTING(ResolveSymbols, 1, SettingType::Bool);
		// the Symbol Settings (see SymbolPathOptions)
		SETTING(SymbolUseEnvPath, 1, SettingType::Bool);
		SETTING(SymbolServerEnabled, 1, SettingType::Bool);
		SETTING_STRING(SymbolServerUrl, L"https://msdl.microsoft.com/download/symbols");
		SETTING_STRING(SymbolCache, L"");	// empty: TotalPE's symbol cache
		SETTING_STRING(SymbolExtraPaths, L"");
		SETTING_STRING(ProcessColors, L"");	// see ProcessColors::Load
		// the views' font, its height at 96 DPI; no face name: the default font
		SETTING(Font, LOGFONT{}, SettingType::Binary);
		SETTING(TrayIcon, 1, SettingType::Bool);	// the CPU graph in the notification area
		SETTING(MinimizeToTray, 0, SettingType::Bool);	// minimized, the window leaves the taskbar
		SETTING(AlwaysOnTop, 0, SettingType::Bool);
	END_SETTINGS

	DEF_SETTING(DarkMode, int)
	DEF_SETTING(UpdateInterval, int)
	DEF_SETTING(ActiveTab, int)
	DEF_SETTING(ProcessTreeMode, int)
	DEF_SETTING(PerfPage, int)
	DEF_SETTING(ProcessLowerPane, int)
	DEF_SETTING(ProcessKeepSelectionVisible, int)
	DEF_SETTING(ProcessSplitterPos, int)
	DEF_SETTING_STRING(ProcessColumns)
	DEF_SETTING_STRING(ServiceColumns)
	DEF_SETTING_STRING(ThreadColumns)
	DEF_SETTING_STRING(ModuleColumns)
	DEF_SETTING_STRING(HandleColumns)
	DEF_SETTING_STRING(ProcessColors)
	DEF_SETTING(ResolveSymbols, int)
	DEF_SETTING(SymbolUseEnvPath, int)
	DEF_SETTING(SymbolServerEnabled, int)
	DEF_SETTING_STRING(SymbolServerUrl)
	DEF_SETTING_STRING(SymbolCache)
	DEF_SETTING_STRING(SymbolExtraPaths)
	DEF_SETTING(Font, LOGFONT)
	DEF_SETTING(TrayIcon, int)
	DEF_SETTING(MinimizeToTray, int)
	DEF_SETTING(AlwaysOnTop, int)

	static constexpr PCWSTR MainWindowPlacement = L"MainWindowPlacement";
};
