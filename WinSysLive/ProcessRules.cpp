#include "pch.h"
#include "ProcessRules.h"
#include "AlertRules.h"

namespace {
	std::vector<std::wstring> Split(std::wstring const& text, wchar_t separator) {
		std::vector<std::wstring> parts;
		size_t start = 0;
		for (;;) {
			auto end = text.find(separator, start);
			parts.push_back(text.substr(start, end == std::wstring::npos ? end : end - start));
			if (end == std::wstring::npos)
				return parts;
			start = end + 1;
		}
	}

	std::optional<uint64_t> ParseHex(std::wstring const& text) {
		if (text.empty())
			return std::nullopt;
		wchar_t* end = nullptr;
		auto value = ::wcstoull(text.c_str(), &end, 16);
		return *end || value == 0 ? std::nullopt : std::optional(value);
	}

	// "0:ff,1:f"; "*" every processor
	std::optional<std::vector<GROUP_AFFINITY>> ParseCpuSets(std::wstring const& text) {
		std::vector<GROUP_AFFINITY> masks;
		if (text == L"*")
			return masks;
		for (auto& part : Split(text, L',')) {
			auto colon = part.find(L':');
			if (colon == std::wstring::npos || colon == 0)
				return std::nullopt;
			wchar_t* end = nullptr;
			auto group = ::wcstoul(part.c_str(), &end, 10);
			auto mask = ParseHex(part.substr(colon + 1));
			if (end != part.c_str() + colon || group > 0xFFFF || !mask)
				return std::nullopt;
			masks.push_back({ (KAFFINITY)*mask, (WORD)group });
		}
		return masks;
	}
}

std::wstring ProcessRule::LastErrorText() {
	auto error = ::GetLastError();
	WCHAR text[256]{};
	auto chars = ::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text, _countof(text), nullptr);
	while (chars && (text[chars - 1] == L'\r' || text[chars - 1] == L'\n'))
		text[--chars] = 0;
	return chars ? text : std::format(L"Error {}", error);
}

std::vector<ProcessRule::PriorityName> const& ProcessRule::Priorities() {
	using P = WinSys::ProcessPriorityClass;
	static const std::vector<PriorityName> names = {
		{ P::Realtime, L"Realtime", L"Realtime" },
		{ P::High, L"High", L"High" },
		{ P::AboveNormal, L"AboveNormal", L"Above Normal" },
		{ P::Normal, L"Normal", L"Normal" },
		{ P::BelowNormal, L"BelowNormal", L"Below Normal" },
		{ P::Idle, L"Idle", L"Idle" },
	};
	return names;
}

bool ProcessRule::HasAction() const {
	return Priority || Efficiency || Affinity || CpuSets;
}

bool ProcessRule::Matches(std::wstring const& imageName) const {
	return !Pattern.empty() && AlertRule::Matches(Pattern, imageName);
}

bool ProcessRule::MatchesEverything() const {
	return Matches(L"no-such-image-name.exe") && Matches(L"x");
}

std::wstring ProcessRule::ProcessorsText(uint64_t mask) {
	std::wstring text;
	for (int bit = 0; bit < 64; bit++) {
		if (!(mask >> bit & 1))
			continue;
		int last = bit;
		while (last < 63 && (mask >> (last + 1) & 1))
			last++;
		if (!text.empty())
			text += L", ";
		text += last == bit ? std::to_wstring(bit) : std::format(L"{}-{}", bit, last);
		bit = last;
	}
	return text;
}

std::wstring ProcessRule::ProcessorsText(std::vector<GROUP_AFFINITY> const& masks) {
	if (masks.empty())
		return L"every processor";
	std::wstring text;
	for (auto& m : masks) {
		if (!text.empty())
			text += L"; ";
		// group 0 alone, the usual machine, needs no saying
		text += masks.size() == 1 && m.Group == 0 ? ProcessorsText((uint64_t)m.Mask) : std::format(L"group {}: {}", m.Group, ProcessorsText((uint64_t)m.Mask));
	}
	return text;
}

std::wstring ProcessRule::DescribeActions() const {
	std::vector<std::wstring> parts;
	if (Priority) {
		auto it = std::ranges::find(Priorities(), *Priority, &PriorityName::Class);
		parts.push_back(std::format(L"{} priority", it == Priorities().end() ? L"Unknown" : it->Text));
	}
	if (Efficiency)
		parts.push_back(*Efficiency ? L"efficiency mode on" : L"efficiency mode off");
	if (Affinity)
		parts.push_back(L"affinity " + ProcessorsText(*Affinity));
	if (CpuSets)
		parts.push_back(L"CPU sets " + ProcessorsText(*CpuSets));
	std::wstring text;
	for (auto& part : parts)
		text += (text.empty() ? L"" : L", ") + part;
	return text;
}

std::wstring ProcessRule::Describe() const {
	return Pattern + L": " + DescribeActions();
}

std::vector<std::wstring> ProcessRule::Apply(WinSys::Process& process) const {
	std::vector<std::wstring> failures;
	auto failed = [&](PCWSTR what) { failures.push_back(std::format(L"{}: {}", what, LastErrorText())); };
	if (Efficiency && !process.SetEfficiencyMode(*Efficiency))
		failed(L"efficiency mode");
	if (Priority && !process.SetPriorityClass(*Priority))
		failed(L"priority");
	if (Affinity && !process.SetAffinity((DWORD_PTR)*Affinity))
		failed(L"affinity");
	if (CpuSets && !process.SetDefaultCpuSetMasks(*CpuSets))
		failed(L"CPU sets");
	return failures;
}

std::wstring ProcessRule::ResultText(std::wstring const& imageName, uint32_t pid, std::vector<std::wstring> const& failures) const {
	auto text = std::format(L"{} ({}): ", imageName, pid);
	if (failures.empty())
		return text + DescribeActions();
	text += L"failed: ";
	for (size_t i = 0; i < failures.size(); i++)
		text += (i ? L"; " : L"") + failures[i];
	return text;
}

std::vector<ProcessRule> ProcessRule::ParseRules(std::wstring const& text) {
	std::vector<ProcessRule> rules;
	for (auto& line : Split(text, L'\n')) {
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();
		auto f = Split(line, L'|');
		if (f.size() != 6 || f[1].empty() || (f[0] != L"0" && f[0] != L"1"))
			continue;
		ProcessRule rule;
		rule.Enabled = f[0] == L"1";
		rule.Pattern = f[1];
		if (!f[2].empty()) {
			auto it = std::ranges::find_if(Priorities(), [&](auto& p) { return f[2] == p.Key; });
			if (it == Priorities().end())
				continue;
			rule.Priority = it->Class;
		}
		if (!f[3].empty()) {
			if (f[3] != L"On" && f[3] != L"Off")
				continue;
			rule.Efficiency = f[3] == L"On";
		}
		if (!f[4].empty()) {
			rule.Affinity = ParseHex(f[4]);
			if (!rule.Affinity)
				continue;
		}
		if (!f[5].empty()) {
			rule.CpuSets = ParseCpuSets(f[5]);
			if (!rule.CpuSets)
				continue;
		}
		if (rule.HasAction())
			rules.push_back(std::move(rule));
	}
	return rules;
}

std::wstring ProcessRule::FormatRules(std::vector<ProcessRule> const& rules) {
	std::wstring text;
	for (auto& rule : rules) {
		auto pattern = rule.Pattern;
		std::erase_if(pattern, [](wchar_t c) { return c == L'|' || c == L'\r' || c == L'\n'; });
		std::wstring priority, cpuSets;
		if (rule.Priority)
			if (auto it = std::ranges::find(Priorities(), *rule.Priority, &PriorityName::Class); it != Priorities().end())
				priority = it->Key;
		if (rule.CpuSets) {
			if (rule.CpuSets->empty())
				cpuSets = L"*";
			for (auto& m : *rule.CpuSets)
				cpuSets += std::format(L"{}{}:{:x}", cpuSets.empty() ? L"" : L",", m.Group, (uint64_t)m.Mask);
		}
		text += std::format(L"{}|{}|{}|{}|{}|{}\n", rule.Enabled ? 1 : 0, pattern, priority,
			rule.Efficiency ? (*rule.Efficiency ? L"On" : L"Off") : L"",
			rule.Affinity ? std::format(L"{:x}", *rule.Affinity) : L"", cpuSets);
	}
	return text;
}
