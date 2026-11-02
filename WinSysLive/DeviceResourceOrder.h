#pragma once

#include <cstdint>
#include <utility>
#include <WinSys/Devices.h>

//
// The order of the Resources by Type dialog (CDeviceResourceMapDlg): by type, then address. Header-only, so the tests
// take it (Tests\AlertTests.cpp).
//
namespace DeviceResourceOrder {
	// a resource's place: its type (memory and large memory together, the same address space), then its start; an
	// interrupt by its number, the line-based ones first, then the message-signaled ones (negative) by their magnitude
	inline std::pair<int, int64_t> Key(WinSys::DeviceResource const& r) {
		using WinSys::DeviceResourceType;
		int type = r.Type == DeviceResourceType::LargeMemory ? (int)DeviceResourceType::Memory : (int)r.Type;
		if (r.Type == DeviceResourceType::Interrupt) {
			auto value = (int32_t)r.Value;
			return { type, value >= 0 ? (int64_t)value : 0x100000000LL - value };
		}
		if (r.Type == DeviceResourceType::Dma)
			return { type, (int64_t)r.Value };
		return { type, (int64_t)r.Start };
	}

	// the dialog's order: by Key, and a range before the narrower ones at its start (a bridge window before its devices')
	inline bool Before(WinSys::DeviceResource const& a, WinSys::DeviceResource const& b) {
		auto ka = Key(a), kb = Key(b);
		if (ka != kb)
			return ka < kb;
		return a.End > b.End;
	}
}
