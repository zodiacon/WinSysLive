#pragma once

#include <WinSys/Processes.h>
#include <optional>
#include <string>
#include <vector>

//
// Process rules without the app around them, so the tests can take them (Tests\ProcessRuleTests.cpp): what a rule sets
// on the processes whose image names match it (the priority class, efficiency mode, the affinity, the default CPU sets;
// each may be left alone), its description, its application to a process, and the ProcessRules setting's text (one rule
// a line, Enabled|Pattern|Priority|Efficiency|Affinity|CpuSets). CProcessRules (ProcessRuleRunner.h) applies them.
//
struct ProcessRule {
	bool Enabled{ true };
	std::wstring Pattern;	// image names, as the Alerts' (PathMatchSpec's wildcards, ';'-separated)
	std::optional<WinSys::ProcessPriorityClass> Priority;
	std::optional<bool> Efficiency;
	std::optional<uint64_t> Affinity;	// a mask in the process's processor group
	std::optional<std::vector<GROUP_AFFINITY>> CpuSets;	// empty: every processor

	struct PriorityName {
		WinSys::ProcessPriorityClass Class;
		PCWSTR Key;		// in the setting: must not change
		PCWSTR Text;
	};
	// highest first, as the Process menu has them
	static std::vector<PriorityName> const& Priorities();

	// it sets something
	[[nodiscard]] bool HasAction() const;
	// an image name matches; an empty pattern matches nothing (the Alerts' matches any: here that would be every process)
	[[nodiscard]] bool Matches(std::wstring const& imageName) const;
	// the pattern matches any name, not only some (the dialog asks before such a rule is saved)
	[[nodiscard]] bool MatchesEverything() const;
	// "High priority, efficiency mode on, affinity 0-3, CPU sets 4-7"
	[[nodiscard]] std::wstring DescribeActions() const;
	// "notepad.exe: High priority, ..."
	[[nodiscard]] std::wstring Describe() const;

	// sets what the rule sets on the process (opened with PROCESS_SET_INFORMATION): efficiency mode first, as it changes
	// the priority class, so a priority the rule also has wins. What failed, one text each ("priority: Access is denied.");
	// the others are still set
	std::vector<std::wstring> Apply(WinSys::Process& process) const;
	// "notepad.exe (1234): High priority" or "notepad.exe (1234): failed: priority: Access is denied."
	[[nodiscard]] std::wstring ResultText(std::wstring const& imageName, uint32_t pid, std::vector<std::wstring> const& failures) const;

	// the last error in the system's words
	static std::wstring LastErrorText();
	// "0-3, 8", from bit numbers
	static std::wstring ProcessorsText(uint64_t mask);
	// the masks in words: "every processor", "0-3", "group 1: 0-3"
	static std::wstring ProcessorsText(std::vector<GROUP_AFFINITY> const& masks);

	// the setting's text; lines that aren't six fields, have an unknown value, no pattern or nothing to set are left out
	static std::vector<ProcessRule> ParseRules(std::wstring const& text);
	// a pattern loses '|' and line breaks, which the text uses
	static std::wstring FormatRules(std::vector<ProcessRule> const& rules);
};
