#include "pch.h"
#include "UpdateInstaller.h"
#include "UpdateChecker.h"
#include <WinSys/Http.h>
#include <winhttp.h>
#include <WinSys/ZipArchive.h>
#include <WinSys/FileSignature.h>
#include <WinSys/FileVersion.h>
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <thread>

namespace {
	// what a release's zip has (winget\New-Release.ps1), the exe first
	const PCWSTR ReleaseFiles[] = { L"WinSysLive.exe", L"dbghelp.dll", L"symsrv.dll", L"LICENSE.txt" };
	const PCWSTR OldSuffix = L".update.old";

	std::wstring ExePath() {
		WCHAR path[MAX_PATH];
		::GetModuleFileName(nullptr, path, _countof(path));
		return path;
	}

	std::wstring FolderOf(std::wstring const& path) {
		return path.substr(0, path.rfind(L'\\'));
	}

	std::wstring DownloadRoot() {
		WCHAR temp[MAX_PATH];
		::GetTempPath(_countof(temp), temp);
		return std::wstring(temp) + L"WinSysLive Update";
	}

	// a folder and everything in it, as far as it goes (not std::filesystem: its file clock brings in <chrono>'s time
	// zone database, whose copy from WinSys, built without exceptions, LTCG then warns about, C4530)
	void RemoveTree(std::wstring const& folder) {
		WIN32_FIND_DATA fd;
		wil::unique_hfind find(::FindFirstFile((folder + L"\\*").c_str(), &fd));
		if (find) {
			do {
				if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
					continue;
				auto path = folder + L"\\" + fd.cFileName;
				if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
					RemoveTree(path);
				else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					::RemoveDirectory(path.c_str());
				else
					::DeleteFile(path.c_str());
			} while (::FindNextFile(find.get(), &fd));
		}
		find.reset();
		::RemoveDirectory(folder.c_str());
	}

	bool Exists(std::wstring const& path) {
		return ::GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	bool Fail(std::wstring& error, std::wstring text, DWORD code) {
		error = std::move(text);
		::SetLastError(code);
		return false;
	}

	// the staged files, held open so they can't change until the copy is done
	struct StagedFile {
		std::wstring Name, Path;
		wil::unique_hfile Lock;
	};

	bool LockFiles(std::wstring const& folder, std::vector<StagedFile>& files, std::wstring& error) {
		for (auto name : ReleaseFiles) {
			auto path = folder + L"\\" + name;
			if (!Exists(path)) {
				if (name == ReleaseFiles[0])
					return Fail(error, L"The release has no WinSysLive.exe", ERROR_FILE_NOT_FOUND);
				continue;
			}
			wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
			if (!file)
				return Fail(error, std::format(L"{}: {}", name, WinSys::Helpers::GetErrorText(::GetLastError())), ::GetLastError());
			files.push_back({ name, path, std::move(file) });
		}
		return true;
	}

	// the exe signed (by the running exe's signer, when that's signed), the DLLs signed, the version expected (if any)
	// and not older than the installed exe's
	bool Verify(std::vector<StagedFile> const& files, std::wstring const& installedExe, std::wstring const& version,
		std::wstring* signer, std::wstring& error) {
		for (auto& f : files) {
			bool exe = f.Name == ReleaseFiles[0];
			if (!exe && !f.Name.ends_with(L".dll"))
				continue;
			auto sig = WinSys::FileSignature::Verify(f.Path.c_str());
			if (sig.Status != WinSys::SignatureStatus::Valid)
				return Fail(error, std::format(L"{} isn't validly signed", f.Name), ERROR_INVALID_IMAGE_HASH);
			if (!exe)
				continue;
			auto current = WinSys::FileSignature::Verify(installedExe.c_str());
			if (current.Status == WinSys::SignatureStatus::Valid && current.Signer != sig.Signer)
				return Fail(error, std::format(L"WinSysLive.exe is signed by {}, not {}", sig.Signer, current.Signer), ERROR_INVALID_IMAGE_HASH);
			if (signer)
				*signer = sig.Signer;
			auto newVersion = WinSys::FileVersion::Read(f.Path).Version;
			if (!version.empty() && UpdateChecker::CompareVersions(newVersion, version) != 0)
				return Fail(error, std::format(L"WinSysLive.exe is version {}, not {}", newVersion, version), ERROR_INVALID_DATA);
			// the running exe's own (what Debug builds may be told to be), else the file's
			auto installed = _wcsicmp(installedExe.c_str(), ExePath().c_str()) == 0 ? UpdateChecker::CurrentVersion()
				: WinSys::FileVersion::Read(installedExe).Version;
			if (UpdateChecker::CompareVersions(newVersion, installed) < 0)
				return Fail(error, std::format(L"WinSysLive.exe is version {}, older than {}", newVersion, installed), ERROR_INVALID_DATA);
		}
		return true;
	}

	// renames the files in the way and copies the new ones in, putting everything back if one fails
	bool Apply(std::wstring const& from, std::wstring const& to, std::wstring& error) {
		std::vector<StagedFile> files;
		auto exe = ExePath();
		// the exe replaced is the one in the folder by its own name, which needn't be WinSysLive.exe
		auto installedExe = _wcsicmp(FolderOf(exe).c_str(), to.c_str()) == 0 ? exe : to + L"\\" + ReleaseFiles[0];
		if (!LockFiles(from, files, error) || !Verify(files, installedExe, L"", nullptr, error))
			return false;

		struct Done {
			std::wstring Target, Old;
		};
		std::vector<Done> done;
		auto rollback = [&] {
			for (auto it = done.rbegin(); it != done.rend(); ++it) {
				::DeleteFile(it->Target.c_str());
				if (!it->Old.empty())
					::MoveFile(it->Old.c_str(), it->Target.c_str());
			}
		};
		for (auto& f : files) {
			auto target = f.Name == ReleaseFiles[0] ? installedExe : to + L"\\" + f.Name;
			std::wstring old;
			if (Exists(target)) {
				old = target + OldSuffix;
				// a previous update's still in use (its process hasn't exited): another name
				if (Exists(old) && !::DeleteFile(old.c_str()))
					old = target + L"." + std::to_wstring(::GetTickCount64()) + OldSuffix;
				if (!::MoveFileEx(target.c_str(), old.c_str(), 0)) {
					auto code = ::GetLastError();
					rollback();
					return Fail(error, std::format(L"Renaming {}: {}", target, WinSys::Helpers::GetErrorText(code)), code);
				}
			}
			if (!::CopyFile(f.Path.c_str(), target.c_str(), FALSE)) {
				auto code = ::GetLastError();
				if (!old.empty())
					::MoveFile(old.c_str(), target.c_str());
				rollback();
				return Fail(error, std::format(L"Copying {}: {}", target, WinSys::Helpers::GetErrorText(code)), code);
			}
			done.push_back({ target, old });
		}
		// the files not in use go now; the running exe's and loaded DLLs' at the next start (or boot, when elevated:
		// a non-elevated start can't delete them from Program Files)
		bool elevated = WinSys::SecurityHelper::IsRunningElevated();
		for (auto& d : done)
			if (!d.Old.empty() && !::DeleteFile(d.Old.c_str()) && elevated)
				::MoveFileEx(d.Old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
		return true;
	}

	bool IsWritable(std::wstring const& folder) {
		auto probe = folder + L"\\~WinSysLive.write.tmp";
		HANDLE h = ::CreateFile(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		::CloseHandle(h);
		return true;
	}

	struct Progress : WinSys::DownloadProgress {
		HWND Notify;
		std::shared_ptr<std::atomic<bool>> Cancel;
		uint64_t Posted{ 0 };

		bool OnProgress(uint64_t received, uint64_t total) override {
			// every 256 KB, and the end
			if (received - Posted >= (1 << 18) || received == total) {
				Posted = received;
				::PostMessage(Notify, UpdateInstaller::WM_UPDATE_PROGRESS, (WPARAM)received, (LPARAM)total);
			}
			return !*Cancel;
		}
	};
}

bool UpdateInstaller::NeedsElevation() {
	return !IsWritable(FolderOf(ExePath())) && !WinSys::SecurityHelper::IsRunningElevated();
}

void UpdateInstaller::PrepareAsync(HWND notify, std::wstring const& url, std::wstring const& version, std::shared_ptr<std::atomic<bool>> cancel) {
	std::thread([=] {
		::CoInitializeEx(nullptr, COINIT_MULTITHREADED);	// the signature's catalog lookups
		auto result = std::make_unique<Prepared>();
		auto folder = DownloadRoot() + L"\\" + version;
		RemoveTree(folder);
		::SHCreateDirectoryEx(nullptr, (folder + L"\\files").c_str(), nullptr);
		auto zip = folder + L"\\" + url.substr(url.rfind(L'/') + 1);
		Progress progress;
		progress.Notify = notify;
		progress.Cancel = cancel;
		uint32_t status = 0;
		if (!WinSys::Http::Download(url, zip, L"WinSysLive", &progress, &status)) {
			auto code = ::GetLastError();
			result->Error = code == ERROR_WINHTTP_INVALID_SERVER_RESPONSE ? std::format(L"The download failed: the server answered {}", status)
				: L"The download failed: " + WinSys::Helpers::GetErrorText(code);
		}
		else if (!*cancel && !WinSys::ZipArchive::Extract(zip, folder + L"\\files"))
			result->Error = L"Extracting the release failed: " + WinSys::Helpers::GetErrorText(::GetLastError());
		else if (!*cancel) {
			std::vector<StagedFile> files;
			result->Folder = folder + L"\\files";
			if (LockFiles(result->Folder, files, result->Error) && Verify(files, ExePath(), version, &result->Signer, result->Error)) {
				result->Ok = true;
				result->Version = version;
			}
		}
		::CoUninitialize();
		if (::PostMessage(notify, WM_UPDATE_PREPARED, 0, (LPARAM)result.get()))
			result.release();
	}).detach();
}

bool UpdateInstaller::Install(std::wstring const& folder, std::wstring& error) {
	auto exe = ExePath();
	auto target = FolderOf(exe);
	if (!NeedsElevation())
		return Apply(folder, target, error);

	auto parameters = std::format(L"/applyupdate \"{}\" \"{}\"", folder, target);
	SHELLEXECUTEINFO sei{ sizeof(sei) };
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	sei.lpVerb = L"runas";
	sei.lpFile = exe.c_str();
	sei.lpParameters = parameters.c_str();
	sei.nShow = SW_HIDE;
	if (!::ShellExecuteEx(&sei)) {
		auto code = ::GetLastError();
		if (code != ERROR_CANCELLED)
			error = L"Starting the installation failed: " + WinSys::Helpers::GetErrorText(code);
		return false;
	}
	wil::unique_handle process(sei.hProcess);
	DWORD code = ERROR_TIMEOUT;
	if (::WaitForSingleObject(process.get(), 60000) == WAIT_OBJECT_0)
		::GetExitCodeProcess(process.get(), &code);
	if (code != 0) {
		error = L"Installing the update failed: " + WinSys::Helpers::GetErrorText(code);
		return false;
	}
	return true;
}

int UpdateInstaller::RunCommandLine() {
	int argc = 0;
	wil::unique_hlocal_ptr<PWSTR> argv(::CommandLineToArgvW(::GetCommandLine(), &argc));
	if (!argv || argc < 4 || _wcsicmp(argv.get()[1], L"/applyupdate") != 0)
		return -1;
	std::wstring error;
	if (Apply(argv.get()[2], argv.get()[3], error))
		return 0;
	auto code = ::GetLastError();
	return code ? (int)code : (int)ERROR_GEN_FAILURE;
}

void UpdateInstaller::CleanupAsync() {
	auto folder = FolderOf(ExePath());
	std::thread([folder] {
		// the previous instance may still be exiting (a running exe can't be deleted: access denied, as in a folder that
		// isn't writable): a minute of tries while anything is left
		for (int i = 0; i < 30; i++) {
			bool left = false;
			WIN32_FIND_DATA fd;
			wil::unique_hfind find(::FindFirstFile((folder + L"\\*" + OldSuffix).c_str(), &fd));
			if (find) {
				do {
					if (!::DeleteFile((folder + L"\\" + fd.cFileName).c_str()))
						left = true;
				} while (::FindNextFile(find.get(), &fd));
			}
			if (!left)
				break;
			::Sleep(2000);
		}
		RemoveTree(DownloadRoot());
	}).detach();
}
