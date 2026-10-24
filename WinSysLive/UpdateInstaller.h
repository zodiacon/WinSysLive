#pragma once

//
// Installing a release from its zip (UpdateChecker gives its URL): PrepareAsync downloads it to %TEMP%\WinSysLive Update,
// extracts it (WinSys ZipArchive) and verifies it; Install puts its files beside the exe. Only the files a release has
// are taken (WinSysLive.exe, dbghelp.dll, symsrv.dll, LICENSE.txt). The exe must have a valid Authenticode signature,
// by the same signer as the running exe when that's signed, and its version must be the release's; the DLLs must be
// validly signed. A running exe and its loaded DLLs can be renamed but not overwritten, so each file in the way is
// renamed to <name>.update.old and the new one copied in; the next start deletes the old ones (CleanupAsync).
//
// When the exe's folder isn't writable (Program Files) and the app isn't elevated, Install runs the exe elevated with
// /applyupdate "<files>" "<folder>" (a UAC prompt) and waits for it: _tWinMain hands that to RunCommandLine before
// anything else. The elevated half verifies the files again, holding them open without write or delete sharing, as a
// folder in the user's TEMP can be changed by any of the user's processes between the checks and the copy.
//
namespace UpdateInstaller {
	constexpr UINT WM_UPDATE_PROGRESS = WM_APP + 0x191;	// wParam: bytes received, lParam: the size (0: unknown)
	constexpr UINT WM_UPDATE_PREPARED = WM_APP + 0x192;	// lParam: Prepared*, the receiver's to delete

	struct Prepared {
		bool Ok{ false };
		std::wstring Error;
		std::wstring Folder;	// the extracted files
		std::wstring Version;	// the new exe's
		std::wstring Signer;	// its signing certificate's subject
	};

	// on a detached thread, which posts WM_UPDATE_PROGRESS and WM_UPDATE_PREPARED to notify; setting *cancel stops it
	void PrepareAsync(HWND notify, std::wstring const& url, std::wstring const& version, std::shared_ptr<std::atomic<bool>> cancel);
	// false with error set (empty when the UAC prompt was declined)
	bool Install(std::wstring const& folder, std::wstring& error);
	// installing needs elevation: the exe's folder isn't writable and the app isn't elevated
	bool NeedsElevation();
	// /applyupdate: the exit code (0 or a Win32 error); -1 for any other command line
	int RunCommandLine();
	// at startup, on a detached thread: the .update.old files and the downloads deleted
	void CleanupAsync();
}
