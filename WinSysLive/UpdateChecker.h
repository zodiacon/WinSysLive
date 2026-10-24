#pragma once

//
// Checking for a newer WinSysLive: the latest release on GitHub (api.github.com/repos/zodiacon/WinSysLive/releases/latest,
// its tag_name and html_url, found with string searches as VirusTotal's replies are) against this exe's ProductVersion.
// The request runs on a detached thread (WinSys Http::Get), which posts WM_UPDATE_CHECKED with the result to the window
// given. Downloading and installing a release is UpdateInstaller's.
//
namespace UpdateChecker {
	constexpr UINT WM_UPDATE_CHECKED = WM_APP + 0x190;	// lParam: UpdateInfo*, the receiver's to delete

	struct UpdateInfo {
		bool Manual{ false };	// Help > Check for Updates: say so when there's nothing newer
		bool Ok{ false };		// the release was read
		std::wstring Error;		// why not
		std::wstring Current, Latest;	// "1.2.0"
		std::wstring Url;		// the release's page
		std::wstring AssetUrl;	// the x64 zip (WinSysLive-<version>-x64.zip), if the release has one
		bool Newer{ false };	// Latest is newer than Current
	};

	void CheckAsync(HWND notify, bool manual);
	// the exe's ProductVersion without its fourth part ("1.2.0")
	std::wstring CurrentVersion();
	// installed by WinGet (a portable package under its Packages folder, %LOCALAPPDATA%\Microsoft\WinGet\Packages for
	// the user, %ProgramFiles%\WinGet\Packages for the machine): updating goes through winget then
	bool IsWingetInstall();
	// -1, 0 or 1; "1.10.0" after "1.9.2"; a leading 'v' is ignored
	int CompareVersions(std::wstring const& a, std::wstring const& b);
}
