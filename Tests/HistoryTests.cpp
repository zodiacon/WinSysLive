#include "pch.h"
#include "TestFramework.h"
#include "HistoryRecorder.h"

// HistoryRecorder on a live snapshot: a process of the test's own (cmd /c exit 3, started suspended so the recorder
// sees it running) gives a start line and then an exit line with its exit code, as a warning; a kind turned off
// records nothing; the sequence numbers go on across Clear

namespace {
	struct TestProcess {
		PROCESS_INFORMATION pi{};
		bool Started{ false };

		explicit TestProcess(PCWSTR commandLine) {
			std::wstring cmd(commandLine);
			STARTUPINFO si{ sizeof(si) };
			Started = ::CreateProcess(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		}
		void RunToEnd() {
			::ResumeThread(pi.hThread);
			::WaitForSingleObject(pi.hProcess, 10000);
		}
		~TestProcess() {
			if (Started) {
				::TerminateProcess(pi.hProcess, 1);
				::CloseHandle(pi.hThread);
				::CloseHandle(pi.hProcess);
			}
		}
	};

	std::vector<HistoryRecorder::Line> LinesOf(HistoryRecorder const& recorder, uint32_t pid) {
		std::vector<HistoryRecorder::Line> lines;
		for (auto& line : recorder.GetLines())
			if (line.Pid == pid)
				lines.push_back(line);
		return lines;
	}

	const uint32_t ProcessEvents = (1u << (int)HistoryRecorder::Event::ProcessStarted) | (1u << (int)HistoryRecorder::Event::ProcessExited);
}

TEST(History_LiveProcessStartAndExit) {
	ProcessManager pm;
	pm.Update();
	HistoryRecorder recorder(pm, ProcessEvents);
	CHECK_EQ(recorder.GetFirstSequence(), uint64_t(0));

	TestProcess process(L"cmd.exe /c exit 3");
	CHECK(process.Started);
	if (!process.Started)
		return;
	auto pid = process.pi.dwProcessId;
	pm.Update();
	recorder.Update(pm);
	auto lines = LinesOf(recorder, pid);
	CHECK_EQ(lines.size(), size_t(1));
	if (lines.size() == 1) {
		CHECK_EQ(lines[0].Kind, HistoryRecorder::Event::ProcessStarted);
		CHECK(!lines[0].Warning);
		CHECK(lines[0].Text.starts_with(L"cmd.exe (" + std::to_wstring(pid) + L")"));
		CHECK(HistoryRecorder::Format(lines[0]).find(L"Process started") != std::wstring::npos);
	}

	process.RunToEnd();
	pm.Update();
	recorder.Update(pm);
	lines = LinesOf(recorder, pid);
	CHECK_EQ(lines.size(), size_t(2));
	if (lines.size() == 2) {
		CHECK_EQ(lines[1].Kind, HistoryRecorder::Event::ProcessExited);
		CHECK(lines[1].Warning);
		CHECK(lines[1].Text.find(L"exit code 3 (0x3)") != std::wstring::npos);
	}

	// the sequence numbers go on across Clear
	auto end = recorder.GetEndSequence();
	recorder.Clear();
	CHECK(recorder.GetLines().empty());
	CHECK_EQ(recorder.GetFirstSequence(), end);
	CHECK_EQ(recorder.GetEndSequence(), end);
}

TEST(History_LiveDisabledKindRecordsNothing) {
	ProcessManager pm;
	pm.Update();
	// exits only
	HistoryRecorder recorder(pm, 1u << (int)HistoryRecorder::Event::ProcessExited);
	TestProcess process(L"cmd.exe /c exit 0");
	CHECK(process.Started);
	if (!process.Started)
		return;
	auto pid = process.pi.dwProcessId;
	pm.Update();
	recorder.Update(pm);
	CHECK(LinesOf(recorder, pid).empty());
	process.RunToEnd();
	pm.Update();
	recorder.Update(pm);
	auto lines = LinesOf(recorder, pid);
	CHECK_EQ(lines.size(), size_t(1));
	if (lines.size() == 1) {
		CHECK_EQ(lines[0].Kind, HistoryRecorder::Event::ProcessExited);
		// exit code 0 isn't a warning
		CHECK(!lines[0].Warning);
	}
}

TEST(History_LiveBaselineIsNotStarts) {
	ProcessManager pm;
	pm.Update();
	// a process running before the recorder started isn't reported as started
	TestProcess process(L"cmd.exe /c exit 0");
	CHECK(process.Started);
	if (!process.Started)
		return;
	// the snapshot where it's new is the one the recorder starts from: it's in the baseline, not a start
	pm.Update();
	auto isNew = std::ranges::any_of(pm.GetNewProcesses(), [&](auto& p) { return p->Id == process.pi.dwProcessId; });
	CHECK(isNew);
	HistoryRecorder recorder(pm, ProcessEvents);
	recorder.Update(pm);
	CHECK(LinesOf(recorder, process.pi.dwProcessId).empty());
}
