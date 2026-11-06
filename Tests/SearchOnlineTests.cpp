#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/SearchOnline.h"

// Search Online's URLs (WinSysLive\SearchOnline.h): each engine's, the query UTF-8 and percent-encoded, a file's name quoted

TEST(SearchOnline_Urls) {
	using SearchOnline::Engine;
	CHECK_EQ(SearchOnline::Url(Engine::Google, L"notepad.exe"), std::wstring(L"https://www.google.com/search?q=notepad.exe"));
	CHECK_EQ(SearchOnline::Url(Engine::Bing, L"a b"), std::wstring(L"https://www.bing.com/search?q=a%20b"));
	CHECK_EQ(SearchOnline::Url(Engine::DuckDuckGo, L"x"), std::wstring(L"https://duckduckgo.com/?q=x"));
	// quotes, ampersands and the like encoded; -._~ kept
	CHECK_EQ(SearchOnline::Url(Engine::Google, L"\"a&b=c\"-._~"), std::wstring(L"https://www.google.com/search?q=%22a%26b%3Dc%22-._~"));
	// UTF-8: é is C3 A9
	CHECK_EQ(SearchOnline::Url(Engine::Google, L"caf\u00E9"), std::wstring(L"https://www.google.com/search?q=caf%C3%A9"));
	CHECK_EQ(SearchOnline::NameQuery(L"C:\\Windows\\System32\\notepad.exe"), std::wstring(L"\"notepad.exe\""));
	CHECK_EQ(SearchOnline::NameQuery(L"drivers/x.sys"), std::wstring(L"\"x.sys\""));
	CHECK_EQ(SearchOnline::NameQuery(L"plain.dll"), std::wstring(L"\"plain.dll\""));
	CHECK_EQ(std::wstring(SearchOnline::EngineName(Engine::DuckDuckGo)), std::wstring(L"DuckDuckGo"));
}
