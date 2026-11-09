#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/ProcessRules.h"
#include <WinSys/Processes.h>

// Process rules (ProcessRules.h): the setting's text, what a line must have, the matching, the wording, and a live
// application to a suspended cmd.exe of this test's: efficiency mode before the priority, so the rule's priority wins

using WinSys::ProcessPriorityClass;

namespace {
	struct SuspendedChild {
		PROCESS_INFORMATION pi{};
		SuspendedChild() {
			WCHAR cmd[MAX_PATH];
			::GetSystemDirectory(cmd, _countof(cmd));
			::wcscat_s(cmd, L"\\cmd.exe");
			STARTUPINFO si{ sizeof(si) };
			std::wstring line = L"\"" + std::wstring(cmd) + L"\" /c exit";
			::CreateProcess(cmd, line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		}
		~SuspendedChild() {
			if (pi.hProcess) {
				::TerminateProcess(pi.hProcess, 1);
				::CloseHandle(pi.hThread);
				::CloseHandle(pi.hProcess);
			}
		}
	};

	std::unique_ptr<WinSys::Process> Open(SuspendedChild const& child, WinSys::ProcessAccessMask access) {
		return WinSys::Process::OpenById(child.pi.dwProcessId, access);
	}
}

TEST(ProcessRule_TextRoundTrip) {
	ProcessRule all;
	all.Pattern = L"game*.exe;notepad.exe";
	all.Enabled = false;
	all.Priority = ProcessPriorityClass::AboveNormal;
	all.Efficiency = false;
	all.Affinity = 0xF0;
	all.CpuSets = std::vector<GROUP_AFFINITY>{ { 0xFF, 0 }, { 0xF, 1 } };
	ProcessRule every;
	every.Pattern = L"a.exe";
	every.CpuSets = std::vector<GROUP_AFFINITY>{};

	auto text = ProcessRule::FormatRules({ all, every });
	CHECK_EQ(text, std::wstring(L"0|game*.exe;notepad.exe|AboveNormal|Off|f0|0:ff,1:f\n1|a.exe||||*\n"));

	auto rules = ProcessRule::ParseRules(text);
	CHECK_EQ(rules.size(), 2ull);
	if (rules.size() != 2)
		return;
	CHECK(!rules[0].Enabled);
	CHECK_EQ(rules[0].Pattern, all.Pattern);
	CHECK(rules[0].Priority == ProcessPriorityClass::AboveNormal);
	CHECK(rules[0].Efficiency == false);
	CHECK(rules[0].Affinity == 0xF0ull);
	CHECK(rules[0].CpuSets && rules[0].CpuSets->size() == 2 && (*rules[0].CpuSets)[1].Group == 1 && (*rules[0].CpuSets)[1].Mask == 0xF);
	CHECK(rules[1].Enabled);
	CHECK(!rules[1].Priority && !rules[1].Efficiency && !rules[1].Affinity);
	CHECK(rules[1].CpuSets && rules[1].CpuSets->empty());

	// what the text uses as separators can't get in through a pattern
	ProcessRule odd;
	odd.Pattern = L"a|b\r\nc.exe";
	odd.Priority = ProcessPriorityClass::Idle;
	CHECK_EQ(ProcessRule::FormatRules({ odd }), std::wstring(L"1|abc.exe|Idle|||\n"));
}

TEST(ProcessRule_BadLinesLeftOut) {
	for (auto line : {
		L"",									// nothing
		L"1|a.exe|High",						// too few fields
		L"1|a.exe|High||||",					// too many
		L"2|a.exe|High|||",						// not an Enabled value
		L"1||High|||",							// no pattern
		L"1|a.exe||||",							// nothing to set
		L"1|a.exe|Urgent|||",					// no such priority
		L"1|a.exe||Maybe||",					// no such efficiency
		L"1|a.exe|||zz|",						// not a mask
		L"1|a.exe|||0|",						// no processor
		L"1|a.exe||||0:0",						// no processor in the group
		L"1|a.exe||||ff",						// no group
		L"1|a.exe||||x:ff" })					// not a group
		CHECK_EQ(ProcessRule::ParseRules(line).size(), 0ull);
	// a bad line doesn't take the good ones with it
	CHECK_EQ(ProcessRule::ParseRules(L"1|a.exe|Idle|||\nnonsense\r\n1|b.exe|High|||\r\n").size(), 2ull);
}

TEST(ProcessRule_Matching) {
	ProcessRule rule;
	CHECK(!rule.Matches(L"notepad.exe"));	// an empty pattern is no process, not every process
	rule.Pattern = L"notepad.exe; game*.exe";
	CHECK(rule.Matches(L"notepad.exe"));
	CHECK(rule.Matches(L"NOTEPAD.EXE"));
	CHECK(rule.Matches(L"game12.exe"));
	CHECK(!rule.Matches(L"notepad2.exe"));
	CHECK(!rule.MatchesEverything());
	rule.Pattern = L"*";
	CHECK(rule.MatchesEverything());
	rule.Pattern = L"game*.exe;*";
	CHECK(rule.MatchesEverything());
}

TEST(ProcessRule_Wording) {
	CHECK_EQ(ProcessRule::ProcessorsText(0x10Full), std::wstring(L"0-3, 8"));
	CHECK_EQ(ProcessRule::ProcessorsText(0x1ull), std::wstring(L"0"));
	CHECK_EQ(ProcessRule::ProcessorsText(0x8000000000000001ull), std::wstring(L"0, 63"));
	CHECK_EQ(ProcessRule::ProcessorsText(std::vector<GROUP_AFFINITY>{}), std::wstring(L"every processor"));
	CHECK_EQ(ProcessRule::ProcessorsText(std::vector<GROUP_AFFINITY>{ { 0xF, 0 } }), std::wstring(L"0-3"));
	CHECK_EQ(ProcessRule::ProcessorsText(std::vector<GROUP_AFFINITY>{ { 0xF, 0 }, { 0x3, 1 } }), std::wstring(L"group 0: 0-3; group 1: 0-1"));

	ProcessRule rule;
	rule.Pattern = L"notepad.exe";
	CHECK(!rule.HasAction());
	rule.Priority = ProcessPriorityClass::BelowNormal;
	rule.Efficiency = true;
	rule.Affinity = 0xF;
	rule.CpuSets = std::vector<GROUP_AFFINITY>{ { 0xF0, 0 } };
	CHECK(rule.HasAction());
	CHECK_EQ(rule.Describe(), std::wstring(L"notepad.exe: Below Normal priority, efficiency mode on, affinity 0-3, CPU sets 4-7"));
	CHECK_EQ(rule.ResultText(L"notepad.exe", 12, {}), std::wstring(L"notepad.exe (12): Below Normal priority, efficiency mode on, affinity 0-3, CPU sets 4-7"));
	CHECK_EQ(rule.ResultText(L"notepad.exe", 12, { L"priority: no", L"affinity: no" }), std::wstring(L"notepad.exe (12): failed: priority: no; affinity: no"));
}

TEST(ProcessRule_ApplyLive) {
	SuspendedChild child;
	CHECK(child.pi.hProcess != nullptr);
	if (!child.pi.hProcess)
		return;
	auto process = Open(child, WinSys::ProcessAccessMask::SetInformation | WinSys::ProcessAccessMask::QueryLimitedInformation);
	CHECK(process != nullptr);
	if (!process)
		return;
	DWORD_PTR mask = 0, systemMask = 0;
	CHECK(process->GetAffinity(mask, systemMask));

	// the lowest processor; efficiency mode alone would leave Idle: the rule's priority must win
	ProcessRule rule;
	rule.Pattern = L"cmd.exe";
	rule.Priority = ProcessPriorityClass::BelowNormal;
	rule.Efficiency = true;
	rule.Affinity = (uint64_t)(systemMask & (~systemMask + 1));
	auto failures = rule.Apply(*process);
	CHECK_EQ(failures.size(), 0ull);
	CHECK(process->GetPriorityClass() == ProcessPriorityClass::BelowNormal);
	CHECK(process->GetEfficiencyMode() == true);
	CHECK(process->GetAffinity(mask, systemMask));
	CHECK_EQ((uint64_t)mask, *rule.Affinity);

	// only what the rule has is touched
	ProcessRule priorityOnly;
	priorityOnly.Pattern = L"cmd.exe";
	priorityOnly.Priority = ProcessPriorityClass::High;
	CHECK_EQ(priorityOnly.Apply(*process).size(), 0ull);
	CHECK(process->GetEfficiencyMode() == true);
	CHECK(process->GetAffinity(mask, systemMask));
	CHECK_EQ((uint64_t)mask, *rule.Affinity);

	// efficiency off: the system decides again, and without a priority of the rule's, at normal priority
	ProcessRule off;
	off.Pattern = L"cmd.exe";
	off.Efficiency = false;
	CHECK_EQ(off.Apply(*process).size(), 0ull);
	CHECK(process->GetEfficiencyMode() == false);
	CHECK(process->GetPriorityClass() == ProcessPriorityClass::Normal);

	// the CPU sets, and back to none
	ProcessRule sets;
	sets.Pattern = L"cmd.exe";
	sets.CpuSets = std::vector<GROUP_AFFINITY>{ { (KAFFINITY)*rule.Affinity, 0 } };
	auto setFailures = sets.Apply(*process);
	CHECK_EQ(setFailures.size(), 0ull);
	auto read = process->GetDefaultCpuSetMasks();
	CHECK(read && read->size() == 1 && (*read)[0].Mask == (KAFFINITY)*rule.Affinity);
	sets.CpuSets = std::vector<GROUP_AFFINITY>{};
	CHECK_EQ(sets.Apply(*process).size(), 0ull);
	read = process->GetDefaultCpuSetMasks();
	CHECK(read && read->empty());
}

TEST(ProcessRule_FailuresLive) {
	SuspendedChild child;
	if (!child.pi.hProcess)
		return;
	// a handle without PROCESS_SET_INFORMATION: every setting fails, each said, none stopping the rest
	auto process = Open(child, WinSys::ProcessAccessMask::QueryLimitedInformation);
	CHECK(process != nullptr);
	if (!process)
		return;
	ProcessRule rule;
	rule.Pattern = L"cmd.exe";
	rule.Priority = ProcessPriorityClass::Idle;
	rule.Affinity = 1;
	auto failures = rule.Apply(*process);
	CHECK_EQ(failures.size(), 2ull);
	if (failures.size() == 2) {
		CHECK(failures[0].starts_with(L"priority: "));
		CHECK(failures[1].starts_with(L"affinity: "));
		CHECK(failures[0].size() > 12);	// the reason
	}
}
