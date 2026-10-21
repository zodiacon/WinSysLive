#pragma once

#include <Settings.h>

struct AppSettings : Settings {
	BEGIN_SETTINGS(AppSettings)
		SETTING(DarkMode, 0, SettingType::Bool);
		SETTING(UpdateInterval, 1000, SettingType::Int32);
		SETTING(ActiveTab, 0, SettingType::Int32);
		SETTING(ProcessTreeMode, 1, SettingType::Bool);
		SETTING(ToolBar, 1, SettingType::Bool);	// View > Toolbar
		SETTING(StatusBar, 1, SettingType::Bool);	// View > Status Bar
		SETTING(PerfPage, 0, SettingType::Int32);	// before PerfPageKey: the page's index, which drives coming and going shift
		SETTING(PerfShowProcessors, 1, SettingType::Bool);	// the CPU page's per-processor graphs
		SETTING_STRING(PerfPageKey, L"");	// the Performance view's page: "CPU", "Disk:C", "GPU:0"... (CPerfView::PageKey)
		SETTING(ProcessLowerPane, 0, SettingType::Bool);
		SETTING(ProcessKeepSelectionVisible, 0, SettingType::Bool);	// scroll to the selected process on every update
		SETTING(ProcessSplitterPos, 60, SettingType::Int32);	// percent of the height for the process list
		SETTING_STRING(ProcessColumns, L"");	// see ColumnLayout::Parse
		SETTING_STRING(ProcessSort, L"");	// the sort column's key and the direction ("CPU:desc"); empty: unsorted
		SETTING_STRING(ServiceColumns, L"");
		SETTING_STRING(ThreadColumns, L"");
		SETTING_STRING(ModuleColumns, L"");
		SETTING_STRING(HandleColumns, L"");
		SETTING_STRING(NetworkColumns, L"");
		SETTING_STRING(KernelModuleColumns, L"");	// the Kernel Modules tab
		SETTING_STRING(DriverColumns, L"");	// the Kernel Modules tab's, from when it was the Drivers tab: read while KernelModuleColumns is empty
		SETTING_STRING(DriverServiceColumns, L"");	// the Drivers tab (driver services)
		SETTING_STRING(UserColumns, L"");
		SETTING_STRING(RunHistory, L"");	// File > Run and Run As: the last command lines, newest first, one a line
		SETTING_STRING(RunAsUser, L"");	// File > Run As: the last user name (never the password)
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
		SETTING(SingleInstance, 0, SettingType::Bool);	// starting the app again brings the running instance to the front
		SETTING(ResolveAddresses, 1, SettingType::Bool);	// connections' addresses by their DNS names (AddressResolver)
	END_SETTINGS

	DEF_SETTING(DarkMode, int)
	DEF_SETTING(UpdateInterval, int)
	DEF_SETTING(ActiveTab, int)
	DEF_SETTING(ProcessTreeMode, int)
	DEF_SETTING(ToolBar, int)
	DEF_SETTING(StatusBar, int)
	DEF_SETTING(PerfPage, int)
	DEF_SETTING_STRING(PerfPageKey)
	DEF_SETTING(PerfShowProcessors, int)
	DEF_SETTING(ProcessLowerPane, int)
	DEF_SETTING(ProcessKeepSelectionVisible, int)
	DEF_SETTING(ProcessSplitterPos, int)
	DEF_SETTING_STRING(ProcessColumns)
	DEF_SETTING_STRING(ProcessSort)
	DEF_SETTING_STRING(ServiceColumns)
	DEF_SETTING_STRING(ThreadColumns)
	DEF_SETTING_STRING(ModuleColumns)
	DEF_SETTING_STRING(HandleColumns)
	DEF_SETTING_STRING(NetworkColumns)
	DEF_SETTING_STRING(KernelModuleColumns)
	DEF_SETTING_STRING(DriverColumns)
	DEF_SETTING_STRING(DriverServiceColumns)
	DEF_SETTING_STRING(UserColumns)
	DEF_SETTING_STRING(RunHistory)
	DEF_SETTING_STRING(RunAsUser)
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
	DEF_SETTING(SingleInstance, int)
	DEF_SETTING(ResolveAddresses, int)

	static constexpr PCWSTR MainWindowPlacement = L"MainWindowPlacement";
};
