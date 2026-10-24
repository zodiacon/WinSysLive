#pragma once

//
// Options > Export Settings and Import Settings. Export writes the settings (as they are saved: the caller saves the
// views' and the frame's first) to an INI file, created as UTF-16 first so that WritePrivateProfileString keeps
// non-ASCII text. Import can't load into the running app (every view saves its state as it closes, over the import,
// and Settings::LoadFromFile would make the file where Save writes): it copies the file to PendingPath, and the next
// start applies it (ApplyPendingImport, right after the settings are loaded) to the app's own storage. The VirusTotal
// key is DPAPI-encrypted for the user who saved it, so it doesn't work for anyone else.
//
namespace SettingsTransfer {
	// false with the last error set
	bool Export(std::wstring const& path);
	// copies the file to PendingPath for the next start; false with the last error set
	bool StageImport(std::wstring const& path);
	// at startup: a staged import into the settings (and their registry key or INI file), the staged file deleted
	bool ApplyPendingImport();
	// %LOCALAPPDATA%\ScorpioSoftware\WinSysLive\PendingImport.ini
	std::wstring PendingPath();
}
