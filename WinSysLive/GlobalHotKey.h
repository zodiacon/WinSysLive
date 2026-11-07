#pragma once

#include <string>

//
// Options > Global Hotkey: the key that brings the main window up from anywhere (RegisterHotKey on the frame). The
// GlobalHotKey setting keeps it as the hotkey control's value: the virtual key in the low byte, the HOTKEYF_ modifiers
// in the next one; 0 is none. A hotkey needs Ctrl or Alt, so a plain key (or Shift and a key) isn't taken from every
// other program.
//
namespace GlobalHotKey {
	inline UINT VirtualKey(WORD hotKey) {
		return LOBYTE(hotKey);
	}

	// HOTKEYF_ (the control's) to MOD_ (RegisterHotKey's), with MOD_NOREPEAT: holding the keys acts once
	inline UINT Modifiers(WORD hotKey) {
		auto flags = HIBYTE(hotKey);
		UINT mods = MOD_NOREPEAT;
		if (flags & HOTKEYF_CONTROL)
			mods |= MOD_CONTROL;
		if (flags & HOTKEYF_ALT)
			mods |= MOD_ALT;
		if (flags & HOTKEYF_SHIFT)
			mods |= MOD_SHIFT;
		return mods;
	}

	inline bool IsValid(WORD hotKey) {
		return VirtualKey(hotKey) != 0 && (HIBYTE(hotKey) & (HOTKEYF_CONTROL | HOTKEYF_ALT)) != 0;
	}

	// "Ctrl+Alt+W"; the key's name from the keyboard layout
	inline std::wstring Text(WORD hotKey) {
		if (!hotKey)
			return L"None";
		std::wstring text;
		auto flags = HIBYTE(hotKey);
		if (flags & HOTKEYF_CONTROL)
			text += L"Ctrl+";
		if (flags & HOTKEYF_ALT)
			text += L"Alt+";
		if (flags & HOTKEYF_SHIFT)
			text += L"Shift+";
		auto vk = VirtualKey(hotKey);
		LONG scan = ::MapVirtualKey(vk, MAPVK_VK_TO_VSC) << 16;
		if (flags & HOTKEYF_EXT)
			scan |= 1 << 24;
		WCHAR name[64]{};
		if (::GetKeyNameText(scan, name, _countof(name)) > 0)
			text += name;
		else
			text += std::format(L"0x{:02X}", vk);
		return text;
	}
}
