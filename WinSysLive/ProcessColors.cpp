#include "pch.h"
#include "ProcessColors.h"
#include "AppSettings.h"
#include <ranges>

namespace ProcessColors {
	std::vector<Entry> Defaults() {
		return {
			{ ProcessKind::Exited, L"Exited", true, RGB(255, 160, 160), RGB(128, 0, 0) },
			{ ProcessKind::New, L"New", true, RGB(160, 255, 160), RGB(0, 96, 0) },
			{ ProcessKind::Suspended, L"Suspended", true, RGB(192, 192, 192), RGB(80, 80, 80) },
			{ ProcessKind::Secure, L"Secure", true, RGB(200, 180, 255), RGB(62, 50, 112) },
			{ ProcessKind::Protected, L"Protected", true, RGB(255, 128, 255), RGB(106, 42, 106) },
			{ ProcessKind::Immersive, L"Immersive", true, RGB(128, 255, 255), RGB(30, 90, 90) },
			{ ProcessKind::DotNet, L"DotNet", true, RGB(255, 255, 128), RGB(90, 90, 30) },
			{ ProcessKind::Service, L"Service", true, RGB(255, 208, 208), RGB(90, 46, 46) },
			// many processes are in jobs (browsers, packaged apps...), which would drown the other colors
			{ ProcessKind::Job, L"Job", false, RGB(208, 168, 144), RGB(90, 66, 50) },
			// last: the user's own processes are colored only if nothing else applies
			{ ProcessKind::Own, L"Own", true, RGB(208, 224, 255), RGB(40, 56, 96) },
		};
	}

	static COLORREF ParseColor(std::wstring_view text, COLORREF def) {
		if (text.size() != 6)
			return def;
		auto value = wcstoul(std::wstring(text).c_str(), nullptr, 16);
		return RGB((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
	}

	std::vector<Entry> Load() {
		auto entries = Defaults();
		auto text = AppSettings::Get().ProcessColors();
		for (auto part : std::views::split(std::wstring_view(text), L',')) {
			std::vector<std::wstring_view> fields;
			for (auto field : std::views::split(std::wstring_view(part.begin(), part.end()), L':'))
				fields.emplace_back(field.begin(), field.end());
			if (fields.size() != 4)
				continue;
			auto it = std::ranges::find_if(entries, [&](auto& e) { return fields[0] == e.Key; });
			if (it == entries.end())
				continue;
			it->Enabled = fields[1] == L"1";
			it->Light = ParseColor(fields[2], it->Light);
			it->Dark = ParseColor(fields[3], it->Dark);
		}
		return entries;
	}

	void Save(std::vector<Entry> const& entries) {
		std::wstring text;
		for (auto& e : entries) {
			if (!text.empty())
				text += L',';
			text += std::format(L"{}:{}:{:02X}{:02X}{:02X}:{:02X}{:02X}{:02X}", e.Key, e.Enabled ? 1 : 0,
				GetRValue(e.Light), GetGValue(e.Light), GetBValue(e.Light), GetRValue(e.Dark), GetGValue(e.Dark), GetBValue(e.Dark));
		}
		auto& settings = AppSettings::Get();
		settings.ProcessColors(text);
		settings.Save();
	}

	Entry const* Find(std::vector<Entry> const& entries, ProcessKind kinds) {
		if (kinds == ProcessKind::None)
			return nullptr;
		for (auto& e : entries)
			if (e.Enabled && (kinds & e.Kind) == e.Kind)
				return &e;
		return nullptr;
	}
}
