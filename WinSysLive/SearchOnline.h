#pragma once

//
// A row's Search Online submenu, beside its VirusTotal one (the Processes view and the Process menu, the Modules pages,
// the Kernel Modules and Autoruns tabs): the file's name (quoted) or its SHA-256 hash (VirusTotal's, when it knows the
// file, else computed) searched for with the engine chosen in Options > Search Engine (the SearchEngine setting), in
// the default browser. The URLs are built apart (Url), so the tests take them.
//
namespace SearchOnline {
	enum class Engine { Google, Bing, DuckDuckGo, _Count };

	[[nodiscard]] inline PCWSTR EngineName(Engine engine) {
		switch (engine) {
			case Engine::Google: return L"Google";
			case Engine::Bing: return L"Bing";
			case Engine::DuckDuckGo: return L"DuckDuckGo";
		}
		return L"";
	}

	// the engine's search for the text: UTF-8, percent-encoded (letters, digits and -._~ as they are)
	[[nodiscard]] inline std::wstring Url(Engine engine, std::wstring const& query) {
		PCWSTR base = L"https://www.google.com/search?q=";
		switch (engine) {
			case Engine::Bing: base = L"https://www.bing.com/search?q="; break;
			case Engine::DuckDuckGo: base = L"https://duckduckgo.com/?q="; break;
		}
		auto size = ::WideCharToMultiByte(CP_UTF8, 0, query.data(), (int)query.size(), nullptr, 0, nullptr, nullptr);
		std::string utf8(size, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, query.data(), (int)query.size(), utf8.data(), size, nullptr, nullptr);
		static const char hex[] = "0123456789ABCDEF";
		std::wstring url(base);
		for (unsigned char ch : utf8) {
			// ASCII only: isalnum may take a byte above 127 as a letter in some locales
			if ((ch < 0x80 && isalnum(ch)) || ch == '-' || ch == '.' || ch == '_' || ch == '~')
				url += (wchar_t)ch;
			else {
				url += L'%';
				url += (wchar_t)hex[ch >> 4];
				url += (wchar_t)hex[ch & 15];
			}
		}
		return url;
	}

	// the query for a file's name: its name, quoted ("notepad.exe")
	[[nodiscard]] inline std::wstring NameQuery(std::wstring const& path) {
		auto slash = path.find_last_of(L"\\/");
		return L"\"" + (slash == std::wstring::npos ? path : path.substr(slash + 1)) + L"\"";
	}

	// the SearchEngine setting's
	[[nodiscard]] Engine GetEngine();
	void SetEngine(Engine engine);

	// owner: the window for message boxes
	void SearchName(HWND owner, std::wstring const& path);
	void SearchHash(HWND owner, std::wstring const& path);
	// Search Online (ID_SEARCH_NAME, ID_SEARCH_HASH), after the VirusTotal submenu
	void AppendMenu(HMENU menu, bool enabled);
	// an ID_SEARCH_* command on the file; false for another command
	bool Execute(HWND owner, UINT id, std::wstring const& path);
}
