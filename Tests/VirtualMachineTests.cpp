#include "pch.h"
#include "TestFramework.h"
#include <WinSys/VirtualMachines.h>
#include <WinSys/Processes.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// WinSys VirtualMachines: HCS's answers as this machine gave them (two Hyper-V VMs, WSL 2's VM, Windows Sandbox's
// template), the IDs in the processes' accounts and command lines, and a live read

namespace {
	char const s_Systems[] = R"([{"Id":"10C78245-095C-45DC-A98F-820B7D28CF96","SystemType":"VirtualMachine","Owner":"WSL","RuntimeId":"10c78245-095c-45dc-a98f-820b7d28cf96","State":"Running"},)"
		R"({"Id":"4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246","SystemType":"VirtualMachine","Name":"Windows11","Owner":"VMMS","RuntimeId":"4bf6afee-d1b6-4c85-a616-4f4a7aaeb246","MemoryTopology":{"AccessTrackingEnabled":false,"GpaMappingSizeInPages":0}},)"
		R"({"Id":"82882a95-2a04-446d-a9a3-10f33392c758","SystemType":"VirtualMachine","RuntimeOsType":"Windows","Owner":"CmService","RuntimeId":"82882a95-2a04-446d-a9a3-10f33392c758","State":"SavedAsTemplate"},)"
		R"({"Id":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","SystemType":"Container","Name":"","Owner":"docker","HostingSystemId":"{d4baa737-fc6f-4676-bf91-d98cdfbcaecb}"},)"
		R"({"SystemType":"VirtualMachine"}, 42, "text"])";

	char const s_Memory[] = R"({"Id":"4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246","SystemType":"VirtualMachine","Name":"Windows11","Owner":"VMMS",)"
		R"("Memory":{"VirtualNodeCount":2,"VirtualMachineMemory":{"AvailableMemory":17,"AssignedMemory":2373120,"BalancingEnabled":true},)"
		R"("VirtualNodes":[{"VirtualNodeIndex":0,"VirtualProcessorCount":4,"MemoryUsageInPages":2373120},{"VirtualNodeIndex":1,"VirtualProcessorCount":2}]}})";
}

TEST(Vm_ParseComputeSystems) {
	auto systems = VirtualMachines::ParseComputeSystems(s_Systems);
	CHECK_EQ(systems.size(), 4u);	// the entry without an ID and the non-objects left out
	if (systems.size() != 4)
		return;
	CHECK(systems[0].Id == L"10C78245-095C-45DC-A98F-820B7D28CF96");
	CHECK(systems[0].Owner == L"WSL");
	CHECK(systems[0].Name.empty());
	CHECK(systems[0].State == L"Running");
	CHECK(systems[0].InHcs);
	CHECK(!systems[0].InHyperV);
	CHECK(VirtualMachines::DisplayName(systems[0]) == L"WSL 2");
	CHECK(VirtualMachines::KindText(systems[0]) == L"WSL 2");

	CHECK(systems[1].Name == L"Windows11");
	CHECK(systems[1].State.empty());	// HCS doesn't say for Hyper-V's: WMI does
	CHECK(VirtualMachines::KindText(systems[1]) == L"Hyper-V VM");
	CHECK(VirtualMachines::DisplayName(systems[1]) == L"Windows11");
	CHECK_EQ(systems[1].AssignedMemory, 0ull);	// the enumeration has no Memory

	CHECK(systems[2].Id == L"82882A95-2A04-446D-A9A3-10F33392C758");	// upper case
	CHECK(systems[2].RuntimeOsType == L"Windows");
	CHECK(VirtualMachines::KindText(systems[2]) == L"Windows Sandbox (template)");
	CHECK(VirtualMachines::StateText(systems[2]) == L"Saved as template");
	CHECK(VirtualMachines::StateText(systems[0]) == L"Running");
	CHECK(VirtualMachines::StateText(systems[1]).empty());
	CHECK(VirtualMachines::DisplayName(systems[2]) == L"Windows Sandbox");

	CHECK(systems[3].Id == L"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
	CHECK(systems[3].HostingSystemId == L"D4BAA737-FC6F-4676-BF91-D98CDFBCAECB");
	CHECK(VirtualMachines::KindText(systems[3]) == L"Container (docker)");
	CHECK(VirtualMachines::DisplayName(systems[3]) == L"0123456789AB");

	// not JSON, not an array, truncated
	CHECK(VirtualMachines::ParseComputeSystems("").empty());
	CHECK(VirtualMachines::ParseComputeSystems("{\"Id\":\"x\"}").empty());
	CHECK(VirtualMachines::ParseComputeSystems(R"([{"Id":"10C78245-095C-45DC-A98F-820B7D28CF96",)").empty());
	// wrong types: the strings expected are numbers, the ID an object
	auto odd = VirtualMachines::ParseComputeSystems(R"([{"Id":"A","Name":5,"Owner":null,"State":["x"]},{"Id":{}}])");
	CHECK_EQ(odd.size(), 1u);
	if (!odd.empty()) {
		CHECK(odd[0].Id == L"A");
		CHECK(odd[0].Name.empty() && odd[0].Owner.empty() && odd[0].State.empty());
	}
}

TEST(Vm_ParseMemory) {
	ComputeSystem s;
	VirtualMachines::ParseMemoryProperties(s_Memory, s);
	CHECK_EQ(s.AssignedMemory, 2373120ull * 4096);
	CHECK_EQ(s.ProcessorCount, 6u);
	ComputeSystem none;
	VirtualMachines::ParseMemoryProperties(R"({"Error":-2147024809,"ErrorMessage":"The parameter is incorrect."})", none);
	VirtualMachines::ParseMemoryProperties("not json", none);
	VirtualMachines::ParseMemoryProperties(R"({"Memory":{"VirtualMachineMemory":{"AssignedMemory":-5},"VirtualNodes":{}}})", none);
	CHECK_EQ(none.AssignedMemory, 0ull);
	CHECK_EQ(none.ProcessorCount, 0u);
}

TEST(Vm_Ids) {
	CHECK(VirtualMachines::NormalizeId(L"{d4baa737-fc6f-4676-bf91-d98cdfbcaecb}") == L"D4BAA737-FC6F-4676-BF91-D98CDFBCAECB");
	CHECK(VirtualMachines::NormalizeId(L"D4BAA737-FC6F-4676-BF91-D98CDFBCAECB") == L"D4BAA737-FC6F-4676-BF91-D98CDFBCAECB");
	CHECK(VirtualMachines::NormalizeId(L"D4BAA737-FC6F-4676-BF91-D98CDFBCAECG").empty());
	CHECK(VirtualMachines::NormalizeId(L"D4BAA737FFC6F-4676-BF91-D98CDFBCAECB").empty());
	CHECK(VirtualMachines::NormalizeId(L"{D4BAA737-FC6F-4676-BF91-D98CDFBCAECB").empty());

	CHECK(VirtualMachines::IdFromAccount(L"NT VIRTUAL MACHINE\\4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246") == L"4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246");
	CHECK(VirtualMachines::IdFromAccount(L"nt virtual machine\\4bf6afee-d1b6-4c85-a616-4f4a7aaeb246") == L"4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246");
	CHECK(VirtualMachines::IdFromAccount(L"NT VIRTUAL MACHINE\\Virtual Machines").empty());
	CHECK(VirtualMachines::IdFromAccount(L"NT AUTHORITY\\SYSTEM").empty());
	CHECK(VirtualMachines::IdFromAccount(L"").empty());

	CHECK(VirtualMachines::IdFromCommandLine(L"\"C:\\Windows\\System32\\vmwp.exe\" 4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246 0x408") == L"4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246");
	CHECK(VirtualMachines::IdFromCommandLine(L"vmwp.exe 4bf6afee-d1b6-4c85-a616-4f4a7aaeb246") == L"4BF6AFEE-D1B6-4C85-A616-4F4A7AAEB246");
	CHECK(VirtualMachines::IdFromCommandLine(L" --vm-id {10c78245-095c-45dc-a98f-820b7d28cf96} --handle 2660") == L"10C78245-095C-45DC-A98F-820B7D28CF96");
	CHECK(VirtualMachines::IdFromCommandLine(L" --distro-id {fa9a7c96-f6e1-4423-9c3a-59f5b9fa61af} --vm-id {10c78245-095c-45dc-a98f-820b7d28cf96} --handle 812") == L"10C78245-095C-45DC-A98F-820B7D28CF96");
	CHECK(VirtualMachines::IdFromCommandLine(L" --vm-id").empty());
	CHECK(VirtualMachines::IdFromCommandLine(L"\"C:\\Windows\\System32\\vmwp.exe\"").empty());
	CHECK(VirtualMachines::IdFromCommandLine(L"\"C:\\Windows\\System32\\vmwp.exe").empty());
	CHECK(VirtualMachines::IdFromCommandLine(L"notepad.exe C:\\file.txt").empty());
	CHECK(VirtualMachines::IdFromCommandLine(L"").empty());

	CHECK(VirtualMachines::HyperVStateName(2) == L"Running");
	CHECK(VirtualMachines::HyperVStateName(3) == L"Off");
	CHECK(VirtualMachines::HyperVStateName(6) == L"Saved");
	CHECK(VirtualMachines::HyperVStateName(12345) == L"State 12345");
}

// every running VM worker's ID (its account's) is among the systems read
TEST(Vm_Live) {
	if (!SecurityHelper::IsRunningElevated())
		return;	// HCS and Hyper-V's WMI need an administrator
	auto systems = VirtualMachines::Enumerate();
	if (!systems) {
		CHECK(::GetLastError() != 0);
		return;	// no HCS here (CI's runner may have none)
	}
	for (auto& s : *systems) {
		CHECK(!s.Id.empty());
		CHECK(s.InHcs || s.InHyperV);
		CHECK(!VirtualMachines::KindText(s).empty());
		if (s.InHcs && s.SystemType == L"VirtualMachine" && s.State != L"SavedAsTemplate")
			CHECK(s.ProcessorCount > 0 && s.AssignedMemory > 0);
	}
	for (auto& [pid, name] : Process::EnumProcessNames()) {
		if (_wcsicmp(name.c_str(), L"vmwp.exe") != 0)
			continue;
		auto process = Process::OpenById(pid, ProcessAccessMask::QueryLimitedInformation);
		if (!process)
			continue;
		auto id = VirtualMachines::IdFromAccount(process->GetUserName());
		CHECK(!id.empty());
		CHECK(std::ranges::find(*systems, id, &ComputeSystem::Id) != systems->end());
		auto vm = Process::OpenById(pid, ProcessAccessMask::QueryLimitedInformation | ProcessAccessMask::VmRead);
		if (vm)
			CHECK(VirtualMachines::IdFromCommandLine(vm->GetCommandLine()) == id);
	}
}
