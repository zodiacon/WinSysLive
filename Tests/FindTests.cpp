#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/LockPath.h"
#include "../WinSysLive/ExplorerMenu.h"

// Find Handle or DLL's Whole Path: the file itself, or a folder and what's under it
TEST(Find_LockPathFile) {
	CHECK(LockPath::Matches(L"C:\\Temp\\a.txt", L"C:\\Temp\\a.txt"));
	CHECK(LockPath::Matches(L"c:\\temp\\A.TXT", L"C:\\Temp\\a.txt"));
	// a longer name with the same start isn't the file
	CHECK(!LockPath::Matches(L"C:\\Temp\\a.txt2", L"C:\\Temp\\a.txt"));
	CHECK(!LockPath::Matches(L"C:\\Temp\\a.tx", L"C:\\Temp\\a.txt"));
	// a file isn't a folder: nothing is under it (but a stream is a name of its own)
	CHECK(!LockPath::Matches(L"C:\\Temp\\a.txt:stream", L"C:\\Temp\\a.txt"));
}

TEST(Find_LockPathFolder) {
	CHECK(LockPath::Matches(L"C:\\Temp", L"C:\\Temp"));
	CHECK(LockPath::Matches(L"C:\\Temp\\sub\\b.dll", L"C:\\Temp"));
	CHECK(LockPath::Matches(L"C:\\Temp\\sub\\b.dll", L"C:\\Temp\\"));
	CHECK(LockPath::Matches(L"C:\\Temp", L"C:\\Temp\\"));
	// a sibling whose name starts with the folder's
	CHECK(!LockPath::Matches(L"C:\\Temp2\\b.dll", L"C:\\Temp"));
	CHECK(!LockPath::Matches(L"C:\\Tem", L"C:\\Temp"));
}

TEST(Find_LockPathRoot) {
	// a drive: everything on it, not the volume handle's name (C:)
	CHECK(LockPath::Matches(L"C:\\Windows\\notepad.exe", L"C:\\"));
	CHECK(LockPath::Matches(L"c:\\x", L"C:\\"));
	CHECK(!LockPath::Matches(L"D:\\x", L"C:\\"));
	CHECK(!LockPath::Matches(L"C:", L"C:\\"));
	CHECK(!LockPath::Matches(L"C:\\x", L""));
}

// Explorer's Who's Locking This? starts the exe with /locks "%1"
TEST(Find_LocksArgument) {
	CHECK_EQ(ExplorerMenu::GetLocksArgument(L"/locks \"C:\\Temp\\a b.txt\"").value_or(L""), std::wstring(L"C:\\Temp\\a b.txt"));
	CHECK_EQ(ExplorerMenu::GetLocksArgument(L"/minimized /LOCKS C:\\Temp").value_or(L""), std::wstring(L"C:\\Temp"));
	// a drive: "C:\" escapes its closing quote
	CHECK_EQ(ExplorerMenu::GetLocksArgument(L"/locks \"C:\\\"").value_or(L""), std::wstring(L"C:\\"));
	CHECK(!ExplorerMenu::GetLocksArgument(L"/locks").has_value());
	CHECK(!ExplorerMenu::GetLocksArgument(L"/restart").has_value());
	CHECK(!ExplorerMenu::GetLocksArgument(nullptr).has_value());
}
