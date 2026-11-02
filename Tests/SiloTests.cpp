#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Processes.h>
#include <WinSys/Job.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// Process::GetServerSiloId against the jobs: this process is the host's (0); a process in a server silo (a Windows
// container's: run one, process isolated, to exercise it) is in a job that is that server silo, with the same ID

TEST(Silo_LiveOwnProcessIsHost) {
	auto process = Process::OpenById(::GetCurrentProcessId(), ProcessAccessMask::QueryLimitedInformation);
	CHECK(process != nullptr);
	if (!process)
		return;
	auto silo = process->GetServerSiloId();
	CHECK(silo.has_value());
	CHECK_EQ(silo.value_or(1), 0u);
}

TEST(Silo_LiveServerSiloMembership) {
	if (!SecurityHelper::IsRunningElevated())
		return;	// the jobs of other processes need elevation
	size_t inSilos = 0;
	for (auto& [pid, name] : Process::EnumProcessNames()) {
		auto process = Process::OpenById(pid, ProcessAccessMask::QueryLimitedInformation);
		if (!process)
			continue;
		auto silo = process->GetServerSiloId();
		if (!silo || *silo == 0)
			continue;
		inSilos++;
		auto jobs = JobInfo::FindProcessJobs(process->Handle());
		CHECK(jobs.has_value());
		if (!jobs)
			continue;
		bool found = false;
		for (auto& job : *jobs)
			if (auto info = JobInfo::GetSiloInfo(job.Handle.get()); info && info->ServerSilo && info->Id == *silo)
				found = true;
		if (!found)
			printf("  %ls (%u): server silo %u, but no job of it is that silo\n", name.c_str(), pid, *silo);
		CHECK(found);
	}
	printf("  %zu processes in server silos\n", inSilos);
}
