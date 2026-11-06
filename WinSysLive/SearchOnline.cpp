#include "pch.h"
#include "SearchOnline.h"
#include "resource.h"
#include "AppSettings.h"
#include "VirusTotalChecker.h"
#include <WinSys/VirusTotal.h>
#include <WinSys/Helpers.h>

namespace {
	void Open(HWND owner, std::wstring const& query) {
		::ShellExecute(owner, L"open", SearchOnline::Url(SearchOnline::GetEngine(), query).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
}

SearchOnline::Engine SearchOnline::GetEngine() {
	auto engine = AppSettings::Get().SearchEngine();
	return engine >= 0 && engine < (int)Engine::_Count ? (Engine)engine : Engine::Google;
}

void SearchOnline::SetEngine(Engine engine) {
	auto& settings = AppSettings::Get();
	settings.SearchEngine((int)engine);
	settings.Save();
}

void SearchOnline::SearchName(HWND owner, std::wstring const& path) {
	Open(owner, NameQuery(path));
}

void SearchOnline::SearchHash(HWND owner, std::wstring const& path) {
	// VirusTotal's, when it has looked the file up
	if (auto known = VirusTotalChecker::Get().Peek(path); known && !known->Sha256.empty()) {
		Open(owner, known->Sha256);
		return;
	}
	CWaitCursor wait;
	auto hash = WinSys::FileHash::Sha256(path);
	if (!hash) {
		AtlMessageBox(owner, std::format(L"Can't read {}: {}", path, WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return;
	}
	Open(owner, *hash);
}

void SearchOnline::AppendMenu(HMENU menu, bool enabled) {
	CMenu sub;
	sub.CreatePopupMenu();
	UINT flags = MF_STRING | (enabled ? 0 : MF_GRAYED);
	sub.AppendMenu(flags, ID_SEARCH_NAME, L"File &Name");
	sub.AppendMenu(flags, ID_SEARCH_HASH, L"File &Hash (SHA-256)");
	::AppendMenu(menu, MF_POPUP | (enabled ? 0 : MF_GRAYED), (UINT_PTR)sub.Detach(), L"Search &Online");
}

bool SearchOnline::Execute(HWND owner, UINT id, std::wstring const& path) {
	switch (id) {
		case ID_SEARCH_NAME: SearchName(owner, path); return true;
		case ID_SEARCH_HASH: SearchHash(owner, path); return true;
	}
	return false;
}
