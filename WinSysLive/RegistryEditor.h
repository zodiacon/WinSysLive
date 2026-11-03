#pragma once

#include <string>

//
// Opens the Registry Editor at a key: its path as the editor shows it ("Computer\HKEY_LOCAL_MACHINE\..."), saved as
// its LastKey, which a new instance (-m: a running one ignores LastKey) opens at.
//
void OpenRegistryEditor(HWND hWnd, std::wstring const& editorPath);
