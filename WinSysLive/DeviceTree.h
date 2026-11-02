#pragma once

#include <map>
#include <string>
#include <vector>
#include <WinSys/Devices.h>

//
// The Devices tab's rows without the tab: which devices the connection tree keeps, and the by-type tree's classes.
// Header-only, so the tests take it (Tests\AlertTests.cpp).
//
namespace DeviceTree {
	// the devices shown (shown[i]) and their ancestors (Parent), so each has its place in the tree; Devices::Enum lists
	// a parent before its children
	inline std::vector<char> KeepWithAncestors(std::vector<WinSys::DeviceInfo> const& devices, std::vector<char> const& shown) {
		std::vector<char> keep(devices.size());
		for (int i = 0; i < (int)devices.size(); i++)
			if (shown[i])
				for (int p = i; p >= 0 && p < (int)devices.size() && !keep[p]; p = devices[p].Parent)
					keep[p] = 1;
		return keep;
	}

	struct ClassGroup {
		std::wstring Name;			// the class's description, "Other devices" for none
		std::vector<int> Devices;	// by name
		int Problems{ 0 };
	};

	// the shown devices by class (the computer, depth 0, isn't one of a class's), the classes by name and their devices
	// by name, both ignoring case
	inline std::vector<ClassGroup> GroupByClass(std::vector<WinSys::DeviceInfo> const& devices, std::vector<char> const& shown) {
		auto lower = [](std::wstring text) {
			::CharLowerBuff(text.data(), (DWORD)text.size());
			return text;
		};
		std::map<std::wstring, ClassGroup> classes;
		for (int i = 0; i < (int)devices.size(); i++) {
			if (!shown[i] || devices[i].Depth == 0)
				continue;
			auto name = devices[i].ClassName.empty() ? std::wstring(L"Other devices") : devices[i].ClassName;
			auto& group = classes[lower(name)];
			group.Name = name;
			group.Devices.push_back(i);
		}
		std::vector<ClassGroup> groups;
		for (auto& [key, group] : classes) {
			std::ranges::sort(group.Devices, [&](int a, int b) { return ::_wcsicmp(devices[a].Name.c_str(), devices[b].Name.c_str()) < 0; });
			for (int i : group.Devices)
				if (devices[i].HasProblem())
					group.Problems++;
			groups.push_back(std::move(group));
		}
		return groups;
	}
}
