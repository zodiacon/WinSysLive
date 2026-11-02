#include "pch.h"
#include "Snapshot.h"
#include "ProcessInfoEx.h"
#include "FormatHelper.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/KernelModules.h>
#include <WinSys/Autoruns.h>
#include <WinSys/SystemDetails.h>

// Snapshot::Capture, apart from the file format and the comparison (Snapshot.cpp), which the tests take

Snapshot Snapshot::Capture() {
	Snapshot s;
	auto os = WinSys::SystemDetails::GetOs();
	s.Computer = os.ComputerName;
	s.Os = std::format(L"{} {}.{}.{}.{}", os.ProductName, os.Major, os.Minor, os.Build, os.Revision);
	FILETIME now;
	::GetSystemTimeAsFileTime(&now);
	s.Time = ((int64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;

	// a snapshot of our own: the frame's belongs to the UI thread
	ProcessManager pm;
	pm.Update();
	for (auto& p : pm.GetProcesses()) {
		if (p->Id == 0)
			continue;
		Process process;
		process.Pid = p->Id;
		process.ParentPid = p->ParentId;
		process.Name = p->GetImageName();
		process.Path = p->GetImagePath();
		process.CommandLine = p->GetCommandLine();
		process.User = p->GetUser();
		if (auto ni = p->NativeInfo) {
			process.CreateTime = ni->CreateTime.QuadPart;
			process.Threads = ni->NumberOfThreads;
			process.Handles = ni->HandleCount;
			process.PrivateBytes = ni->PrivatePageCount;
		}
		if (auto handle = WinSys::Process::OpenById(p->Id, WinSys::ProcessAccessMask::QueryLimitedInformation | WinSys::ProcessAccessMask::VmRead))
			for (auto& m : WinSys::Process::EnumLoadedModules(handle->Handle()))
				if (::_wcsicmp(m.Path.c_str(), process.Path.c_str()) != 0)
					process.Modules.push_back(m.Path);
		s.Processes.push_back(std::move(process));
	}

	for (auto& svc : WinSys::ServiceManager::EnumServices(WinSys::ServiceEnumType::AllServices)) {
		Service service;
		service.Name = svc.GetName();
		service.DisplayName = svc.GetDisplayName();
		auto& status = svc.GetStatusProcess();
		service.State = FormatHelper::ServiceStateToString(status.CurrentState);
		service.Pid = status.ProcessId;
		if (auto config = WinSys::ServiceManager::GetServiceConfiguration(service.Name)) {
			service.StartType = FormatHelper::ServiceStartTypeToString(*config);
			service.Account = config->AccountName;
			service.Path = config->BinaryPathName;
		}
		s.Services.push_back(std::move(service));
	}

	for (auto& m : WinSys::KernelModules::Enum())
		s.Drivers.push_back({ m.Name, m.Path });

	for (auto& e : WinSys::Autoruns::Enum())
		s.Autoruns.push_back({ WinSys::Autoruns::CategoryName(e.Category), e.Location, e.Name,
			e.Enabled ? (*e.Enabled ? L"Yes" : L"No") : L"", e.Command, e.ImagePath });
	return s;
}
