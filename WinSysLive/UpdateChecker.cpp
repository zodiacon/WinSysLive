#include "pch.h"
#include "UpdateChecker.h"
#include <WinSys/Http.h>
#include <WinSys/Helpers.h>
#include <VersionResourceHelper.h>
#include <thread>

namespace {
	// a JSON string value: "name": "value" (no escapes in the values read here)
	std::string JsonString(std::string const& json, char const* name) {
		auto key = std::string("\"") + name + "\"";
		auto pos = json.find(key);
		if (pos == std::string::npos)
			return {};
		pos = json.find(':', pos + key.size());
		if (pos == std::string::npos)
			return {};
		auto start = json.find('"', pos + 1);
		if (start == std::string::npos)
			return {};
		auto end = json.find('"', start + 1);
		return end == std::string::npos ? std::string() : json.substr(start + 1, end - start - 1);
	}

	// the release's zip for x64 among its assets' download URLs, else any zip
	std::string ZipAsset(std::string const& json) {
		std::string any;
		for (size_t pos = 0; (pos = json.find("\"browser_download_url\"", pos)) != std::string::npos; pos++) {
			auto url = JsonString(json.substr(pos), "browser_download_url");
			if (url.ends_with("-x64.zip"))
				return url;
			if (any.empty() && url.ends_with(".zip"))
				any = url;
		}
		return any;
	}

	std::wstring Widen(std::string const& text) {
		return std::wstring(text.begin(), text.end());	// ASCII: tags and URLs
	}
}

std::wstring UpdateChecker::CurrentVersion() {
#ifdef _DEBUG
	// testing the updater with a release that isn't newer
	WCHAR test[32];
	if (::GetEnvironmentVariable(L"WINSYSLIVE_TEST_VERSION", test, _countof(test)))
		return test;
#endif
	VersionResourceHelper vh;
	std::wstring version = (PCWSTR)vh.GetValue(L"ProductVersion");
	// "1.2.0.0" without its fourth part, as the release tags have it
	if (std::count(version.begin(), version.end(), L'.') == 3)
		version.erase(version.rfind(L'.'));
	return version;
}

bool UpdateChecker::IsWingetInstall() {
	WCHAR path[MAX_PATH];
	::GetModuleFileName(nullptr, path, _countof(path));
	std::wstring p(path);
	::CharLowerBuff(p.data(), (DWORD)p.size());
	return p.find(L"\\winget\\packages\\") != std::wstring::npos;
}

int UpdateChecker::CompareVersions(std::wstring const& a, std::wstring const& b) {
	auto parts = [](std::wstring s) {
		if (!s.empty() && (s[0] == L'v' || s[0] == L'V'))
			s.erase(0, 1);
		std::vector<unsigned long> numbers;
		for (size_t pos = 0; pos <= s.size(); ) {
			auto dot = s.find(L'.', pos);
			numbers.push_back(::wcstoul(s.substr(pos, dot == std::wstring::npos ? std::wstring::npos : dot - pos).c_str(), nullptr, 10));
			if (dot == std::wstring::npos)
				break;
			pos = dot + 1;
		}
		return numbers;
	};
	auto x = parts(a), y = parts(b);
	for (size_t i = 0; i < (std::max)(x.size(), y.size()); i++) {
		auto u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
		if (u != v)
			return u < v ? -1 : 1;
	}
	return 0;
}

void UpdateChecker::CheckAsync(HWND notify, bool manual) {
	auto current = CurrentVersion();
	std::thread([notify, manual, current] {
		auto info = std::make_unique<UpdateInfo>();
		info->Manual = manual;
		info->Current = current;
		auto response = WinSys::Http::Get(L"api.github.com", L"/repos/zodiacon/WinSysLive/releases/latest", L"WinSysLive",
			L"Accept: application/vnd.github+json\r\n");
		if (!response)
			info->Error = WinSys::Helpers::GetErrorText(::GetLastError());
		else if (response->Status != 200)
			info->Error = std::format(L"GitHub answered {}", response->Status);
		else {
			auto tag = Widen(JsonString(response->Body, "tag_name"));
			info->Url = Widen(JsonString(response->Body, "html_url"));
			info->AssetUrl = Widen(ZipAsset(response->Body));
			if (tag.empty())
				info->Error = L"The release has no version";
			else {
				info->Ok = true;
				info->Latest = tag[0] == L'v' || tag[0] == L'V' ? tag.substr(1) : tag;
				info->Newer = CompareVersions(info->Latest, info->Current) > 0;
			}
		}
		// the window may be gone by now (the app closing): then the result goes too
		if (::PostMessage(notify, WM_UPDATE_CHECKED, 0, (LPARAM)info.get()))
			info.release();
	}).detach();
}
