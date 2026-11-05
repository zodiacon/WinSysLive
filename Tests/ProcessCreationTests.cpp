#include "pch.h"
#include "TestFramework.h"
#include <WinSys/ProcessCreation.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// ProcessCreation: ProcessStart's payload, the creator's kind and wording, and a live session that sees a child created
// normally and one created with a spoofed parent (PROC_THREAD_ATTRIBUTE_PARENT_PROCESS: another child of this process)

namespace {
	std::wstring SystemFile(PCWSTR name) {
		WCHAR dir[MAX_PATH];
		::GetSystemDirectory(dir, _countof(dir));
		return std::wstring(dir) + L"\\" + name;
	}

	// cmd.exe suspended, optionally with another parent; killed at the end
	struct Child {
		PROCESS_INFORMATION pi{};
		explicit Child(HANDLE parent = nullptr) {
			STARTUPINFOEX si{ { sizeof(si) } };
			std::vector<BYTE> attributes;
			if (parent) {
				SIZE_T size = 0;
				::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
				attributes.resize(size);
				si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
				::InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size);
				::UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &parent, sizeof(parent), nullptr, nullptr);
			}
			auto cmd = SystemFile(L"cmd.exe");
			std::wstring line = L"\"" + cmd + L"\" /c exit";
			::CreateProcess(cmd.c_str(), line.data(), nullptr, nullptr, FALSE,
				CREATE_SUSPENDED | CREATE_NO_WINDOW | (parent ? EXTENDED_STARTUPINFO_PRESENT : 0), nullptr, nullptr, &si.StartupInfo, &pi);
			if (parent)
				::DeleteProcThreadAttributeList(si.lpAttributeList);
		}
		~Child() {
			if (pi.hProcess) {
				::TerminateProcess(pi.hProcess, 1);
				::CloseHandle(pi.hThread);
				::CloseHandle(pi.hProcess);
			}
		}
		ProcessOrThreadKey Key() const {
			FILETIME created, exited, kernel, user;
			::GetProcessTimes(pi.hProcess, &created, &exited, &kernel, &user);
			return { (int64_t)ULARGE_INTEGER{ created.dwLowDateTime, created.dwHighDateTime }.QuadPart, pi.dwProcessId };
		}
	};

	// the session delivers events within its flush timer (a second)
	std::optional<ProcessCreation> WaitFor(ProcessCreationTracker& tracker, ProcessOrThreadKey const& key) {
		for (int i = 0; i < 50; i++) {
			if (auto c = tracker.Get(key))
				return c;
			::Sleep(100);
		}
		return {};
	}
}

TEST(ProcessCreation_Parse) {
	uint32_t pid = 1234, parent = 5678;
	int64_t created = 0x01DB123456789ABC;
	uint64_t sequence = 0xC7A7, parentSequence = 0xC7A0;
	// versions 0 to 2: ProcessID, CreateTime, ParentProcessID
	BYTE v0[40]{};
	memcpy(v0, &pid, 4);
	memcpy(v0 + 4, &created, 8);
	memcpy(v0 + 12, &parent, 4);
	auto c = ProcessCreationParser::Parse(1, 0, v0, sizeof(v0));
	CHECK(c.has_value());
	if (c) {
		CHECK_EQ(c->Pid, 1234u);
		CHECK_EQ(c->CreateTime, created);
		CHECK_EQ(c->ParentPid, 5678u);
	}
	CHECK(ProcessCreationParser::Parse(1, 2, v0, 16).has_value());
	CHECK(!ProcessCreationParser::Parse(1, 0, v0, 15).has_value());
	// version 3: ProcessID, ProcessSequenceNumber, CreateTime, ParentProcessID, ParentProcessSequenceNumber
	BYTE v3[48]{};
	memcpy(v3, &pid, 4);
	memcpy(v3 + 4, &sequence, 8);
	memcpy(v3 + 12, &created, 8);
	memcpy(v3 + 20, &parent, 4);
	memcpy(v3 + 24, &parentSequence, 8);
	c = ProcessCreationParser::Parse(1, 3, v3, sizeof(v3));
	CHECK(c.has_value());
	if (c) {
		CHECK_EQ(c->Pid, 1234u);
		CHECK_EQ(c->CreateTime, created);
		CHECK_EQ(c->ParentPid, 5678u);
	}
	CHECK(!ProcessCreationParser::Parse(1, 3, v3, 23).has_value());
	// ProcessStop (2) and the others aren't creations
	CHECK(!ProcessCreationParser::Parse(2, 3, v3, sizeof(v3)).has_value());
	CHECK(!ProcessCreationParser::Parse(1, 3, nullptr, 0).has_value());
}

TEST(ProcessCreation_Classify) {
	CHECK(ProcessCreationParser::Classify(100, 100, L"") == ProcessCreator::Parent);
	CHECK(ProcessCreationParser::Classify(100, 200, L"") == ProcessCreator::Other);
	CHECK(ProcessCreationParser::Classify(100, 200, L"Appinfo") == ProcessCreator::OnBehalf);
	CHECK(ProcessCreationParser::Classify(100, 200, L"SecLogon") == ProcessCreator::OnBehalf);
	CHECK(ProcessCreationParser::Classify(100, 200, L"Schedule") == ProcessCreator::Other);

	ProcessCreation c{ 300, 0, 200, 100, 7, ProcessCreator::Parent };
	CHECK(ProcessCreationParser::Describe(c).empty());
	c.Creator = ProcessCreator::Other;
	c.CreatorName = L"evil.exe";
	CHECK_EQ(ProcessCreationParser::Describe(c), std::wstring(L"Parent spoofed: created by evil.exe (100), not its parent (200)"));
	c.CreatorName.clear();
	CHECK_EQ(ProcessCreationParser::Describe(c), std::wstring(L"Parent spoofed: created by process 100, not its parent (200)"));
	c.Creator = ProcessCreator::OnBehalf;
	c.CreatorName = L"svchost.exe";
	c.CreatorService = L"Appinfo";
	CHECK_EQ(ProcessCreationParser::Describe(c), std::wstring(L"Created by svchost.exe (100), the Appinfo service, for its parent"));
}

TEST(ProcessCreation_LiveSpoofedParent) {
	if (!SecurityHelper::IsRunningElevated())
		return;	// the kernel provider's session needs administrator rights
	ProcessCreationTracker tracker;
	CHECK(tracker.Start(L"WinSysLive Tests ProcessCreation"));
	if (!tracker.IsRunning())
		return;
	Child normal;
	CHECK(normal.pi.hProcess != nullptr);
	// the spoofed one's parent: the first child
	Child spoofed(normal.pi.hProcess);
	CHECK(spoofed.pi.hProcess != nullptr);

	auto c = WaitFor(tracker, normal.Key());
	CHECK(c.has_value());
	if (c) {
		CHECK_EQ(c->ParentPid, ::GetCurrentProcessId());
		CHECK_EQ(c->CreatorPid, ::GetCurrentProcessId());
		CHECK(c->Creator == ProcessCreator::Parent);
	}
	auto s = WaitFor(tracker, spoofed.Key());
	CHECK(s.has_value());
	if (s) {
		CHECK_EQ(s->ParentPid, normal.pi.dwProcessId);
		CHECK_EQ(s->CreatorPid, ::GetCurrentProcessId());
		CHECK_EQ(s->CreatorTid, ::GetCurrentThreadId());
		CHECK(s->Creator == ProcessCreator::Other);
		CHECK_EQ(s->CreatorName, std::wstring(L"Tests.exe"));
		CHECK(ProcessCreationParser::Describe(*s).starts_with(L"Parent spoofed: created by Tests.exe"));
	}
	auto mismatches = tracker.GetMismatches();
	CHECK(std::ranges::any_of(mismatches, [&](auto& m) { return m.Pid == spoofed.pi.dwProcessId; }));
	CHECK(std::ranges::none_of(mismatches, [&](auto& m) { return m.Pid == normal.pi.dwProcessId; }));
	tracker.Remove(spoofed.Key());
	CHECK(!tracker.Get(spoofed.Key()).has_value());
	tracker.Stop();
	CHECK(!tracker.IsRunning());
}
