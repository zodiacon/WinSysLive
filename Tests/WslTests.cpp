#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Wsl.h>

using namespace WinSys;

// WslProtocol's parsing on text in the helper's format, its lines as Ubuntu 26.04 under WSL 2.7 printed them (the
// helper's process chain sh <- Relay(<sh's pid>) <- SessionLeader <- init-systemd(Ub), names cut to 15 characters),
// and a live run of the helper in a running distribution (skipped when none runs)

namespace {
	const char Header[] =
		"#K 100 32 4096 7698\n"
		"#P\n"
		"root:x:0:0:root:/root:/bin/bash\n"
		"polkitd:x:987:987:User for polkitd:/:/usr/sbin/nologin\n"
		"zodia:x:1000:1000:,,,:/home/zodia:/bin/bash\n"
		"#R\n";

	const char Frame[] =
		"#F\n"
		"1234.56 38000.12\n"
		"cpu  1000 10 500 50000 200 0 30 0 0 0\n"
		"MemTotal:       32000000 kB\n"
		"MemAvailable:   20000000 kB\n"
		"#S\n"
		"1 (systemd) S 0 1 1 0 -1 4194560 42484 1720610 13 1006 73 88 40453 17745 20 0 1 0 158 25309184 3707 18446744073709551615 1 1 0 0 0 0 671173123 4096 1260 0 0 0 17 4 0 0 0 0 28396 0 0 0 0 0 0 0 0\n"
		"1816 (polkitd) S 1 1816 1816 0 -1 4194560 1138 0 2 0 146 41 0 0 20 0 4 0 1412900 313802752 2041 18446744073709551615 1 1 0 0 0 0 0 4096 16387 0 0 0 17 31 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"2 (init-systemd(Ub) S 1 2 0 0 -1 4194560 100 0 0 0 5 7 0 0 20 0 2 0 120 2957312 300 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"300 (tmux: server) S 2 300 300 0 -1 4194560 100 0 0 0 250 50 0 0 20 0 1 0 5000 9000000 1000 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"301 (a) b) Z 300 301 300 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 6000 0 0 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"7692 (SessionLeader) S 2 7692 7692 0 -1 4194560 10 0 0 0 0 0 0 0 20 0 1 0 900000 2957312 300 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"7693 (Relay(7698)) S 7692 7692 7692 0 -1 4194560 10 0 0 0 0 0 0 0 20 0 1 0 900001 2957312 300 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"7698 (sh) S 7693 7698 7692 0 -1 4194560 10 0 0 0 0 0 0 0 20 0 1 0 900002 2957312 300 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"7700 (cat) R 7698 7698 7692 0 -1 4194560 10 0 0 0 0 0 0 0 20 0 1 0 900100 2957312 300 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"#U\n"
		"/proc/1/status:Uid:\t0\t0\t0\t0\n"
		"/proc/1816/status:Uid:\t987\t987\t987\t987\n"
		"/proc/300/status:Uid:\t1000\t1000\t1000\t1000\n"
		"/proc/301/status:Uid:\t4242\t4242\t4242\t4242\n"
		"#C\n"
		"==> /proc/1/cmdline <==\n"
		"/sbin/init \n"
		"==> /proc/1816/cmdline <==\n"
		"/usr/lib/polkit-1/polkitd --no-debug --log-level=notice \n"
		"==> /proc/300/cmdline <==\n"
		"tmux new -s work \n"
		"==> /proc/301/cmdline <==\n"
		"\n"
		"#E\n";
}

TEST(Wsl_ParseStat) {
	auto p = WslProtocol::ParseStat("1 (systemd) S 0 1 1 0 -1 4194560 42484 1720610 13 1006 73 88 40453 17745 20 0 1 0 158 25309184 3707 18446744073709551615 1 1", 4096);
	CHECK(p.has_value());
	if (p) {
		CHECK_EQ(p->Pid, 1u);
		CHECK_EQ(p->Name, std::wstring(L"systemd"));
		CHECK_EQ(p->State, L'S');
		CHECK_EQ(p->ParentPid, 0u);
		CHECK_EQ(p->UserTicks, uint64_t(73));
		CHECK_EQ(p->SystemTicks, uint64_t(88));
		CHECK_EQ(p->Threads, 1u);
		CHECK_EQ(p->StartTicks, uint64_t(158));
		CHECK_EQ(p->VirtualBytes, uint64_t(25309184));
		CHECK_EQ(p->ResidentBytes, uint64_t(3707) * 4096);
	}
	// a name with parentheses and spaces ends at the last ')'
	p = WslProtocol::ParseStat("7693 (Relay(7698)) S 7692 7692 7692 0 -1 4194560 10 0 0 0 0 0 0 0 20 0 1 0 900001 2957312 300", 4096);
	CHECK(p && p->Name == L"Relay(7698)" && p->ParentPid == 7692);
	p = WslProtocol::ParseStat("301 (a) b) Z 300 301 300 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 6000 0 0", 4096);
	CHECK(p && p->Name == L"a) b" && p->State == L'Z' && p->ParentPid == 300);
	// cut short, or not a stat line
	CHECK(!WslProtocol::ParseStat("1 (systemd) S 0 1", 4096).has_value());
	CHECK(!WslProtocol::ParseStat("==> /proc/1/cmdline <==", 4096).has_value());
}

TEST(Wsl_ParseHeader) {
	WslSystem system;
	CHECK(WslProtocol::ParseHeader(Header, system));
	CHECK_EQ(system.ClockTicks, 100u);
	CHECK_EQ(system.Processors, 32u);
	CHECK_EQ(system.PageSize, 4096u);
	CHECK_EQ(system.HelperPid, 7698u);
	CHECK_EQ(system.Users.size(), size_t(3));
	CHECK_EQ(system.Users[1000], std::wstring(L"zodia"));
	CHECK_EQ(system.Users[987], std::wstring(L"polkitd"));
	// without its end
	WslSystem partial;
	CHECK(!WslProtocol::ParseHeader("#K 100 32 4096 7698\n#P\nroot:x:0:0::/:/bin/sh\n", partial));
}

TEST(Wsl_ParseFrame) {
	WslSystem system;
	WslProtocol::ParseHeader(Header, system);
	auto frame = WslProtocol::ParseFrame(Frame, system);
	CHECK(frame.has_value());
	if (!frame)
		return;
	CHECK(std::fabs(frame->UptimeSeconds - 1234.56) < 0.001);
	CHECK_EQ(frame->CpuTotalTicks, uint64_t(1000 + 10 + 500 + 50000 + 200 + 0 + 30 + 0));
	CHECK_EQ(frame->CpuIdleTicks, uint64_t(50000 + 200));
	CHECK_EQ(frame->MemTotal, uint64_t(32000000) * 1024);
	CHECK_EQ(frame->MemAvailable, uint64_t(20000000) * 1024);
	// the helper (7698), what it runs (7700) and its WSL session (7693, 7692) are left out
	std::vector<uint32_t> pids;
	for (auto& p : frame->Processes)
		pids.push_back(p.Pid);
	std::ranges::sort(pids);
	CHECK(pids == std::vector<uint32_t>({ 1, 2, 300, 301, 1816 }));
	auto find = [&](uint32_t pid) { return std::ranges::find(frame->Processes, pid, &WslProcess::Pid); };
	CHECK_EQ(find(1)->User, std::wstring(L"root"));
	CHECK_EQ(find(1)->CommandLine, std::wstring(L"/sbin/init"));
	CHECK_EQ(find(300)->User, std::wstring(L"zodia"));
	CHECK_EQ(find(300)->Name, std::wstring(L"tmux: server"));
	CHECK_EQ(find(300)->CommandLine, std::wstring(L"tmux new -s work"));
	CHECK_EQ(find(1816)->CommandLine, std::wstring(L"/usr/lib/polkit-1/polkitd --no-debug --log-level=notice"));
	// a user that isn't in /etc/passwd, and a zombie's empty command line
	CHECK_EQ(find(301)->User, std::wstring(L"4242"));
	CHECK(find(301)->CommandLine.empty());
	// cut short (no "#E"): not a frame
	std::string cut(Frame);
	cut.resize(cut.find("#C"));
	CHECK(!WslProtocol::ParseFrame(cut, system).has_value());
}

TEST(Wsl_StateNamesAndStartTime) {
	CHECK_EQ(std::wstring(WslProtocol::StateName(L'S')), std::wstring(L"Sleeping"));
	CHECK_EQ(std::wstring(WslProtocol::StateName(L'Z')), std::wstring(L"Zombie"));
	CHECK_EQ(std::wstring(WslProtocol::StateName(L'D')), std::wstring(L"Disk sleep"));
	CHECK_EQ(std::wstring(WslProtocol::StateName(L'?')), std::wstring());
	// started 100 s after boot, read 1,000 s after boot: 900 s before the read
	WslProcess p;
	p.StartTicks = 100 * 100;
	int64_t read = 133'000'000'000'000'000LL;
	CHECK_EQ(p.StartTime(1000.0, read, 100), read - 900LL * 10'000'000);
}

TEST(Wsl_LiveHelper) {
	auto running = WslDistros::GetRunning();
	if (!running || running->empty()) {
		printf("  no running distribution\n");
		return;
	}
	auto& distro = running->front();
	CHECK(std::ranges::any_of(WslDistros::Enum(), [&](auto& d) { return d.Name == distro; }));
	WslProcessMonitor monitor;
	CHECK(monitor.Start(distro));
	std::optional<WslFrame> frame;
	for (int i = 0; i < 50 && !frame; i++) {
		if (monitor.GetSystem())
			monitor.Request();
		::Sleep(200);
		frame = monitor.TakeFrame();
	}
	CHECK(frame.has_value());
	if (frame) {
		auto system = monitor.GetSystem();
		printf("  %ls: %zu processes\n", distro.c_str(), frame->Processes.size());
		CHECK(!frame->Processes.empty());
		CHECK(std::ranges::any_of(frame->Processes, [](auto& p) { return p.Pid == 1; }));
		// the helper isn't among them
		CHECK(system && std::ranges::none_of(frame->Processes, [&](auto& p) { return p.Pid == system->HelperPid || p.ParentPid == system->HelperPid; }));
	}
	monitor.Stop();
	CHECK(!monitor.IsRunning());
}
