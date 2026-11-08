#pragma once

#include <WinSys/VirtualMachines.h>
#include "ProcessInfoEx.h"

//
// The compute systems (WinSys VirtualMachines: HCS and Hyper-V's WMI, which need an administrator) as last read on a
// thread of its own (COM initialized, MTA; the first WMI connection takes a moment), shared by the Processes tab's
// Virtual Machine column, the process sheet and the Virtual Machines dialog. A read is asked for when an ID nobody has
// read appears (a VM started), at most every 5 seconds; the views show it from the refresh after it arrives.
//
struct VirtualMachineLookup final {
	struct Result {
		std::vector<WinSys::ComputeSystem> Systems;
		bool Read{ false };		// a read ended (Systems may still be empty)
		DWORD Error{ 0 };		// the read failed (not an administrator, no HCS)
		uint64_t Generation{ 0 };	// counts the reads
	};

	// the last read (a copy)
	static Result Get();
	// asks for a read now (Refresh in the dialog); done is posted to (window, message) when it ends, if given
	static void Refresh(HWND window = nullptr, UINT message = 0);

	// the ID of the VM the process belongs to: a worker's (vmwp.exe) or security process's (vmsp.exe) by its account,
	// else its command line; a WSL host's (wslhost.exe) by its command line; a memory process's (vmmem, vmmemWSL) by
	// its worker, its parent; Windows Sandbox's template's (vmmemCmZygote, whose parent has gone) by the one template
	// HCS has. Empty for other processes, and those that can't be read (not elevated)
	static std::wstring IdOf(ProcessManager const& pm, ProcessInfoEx& p);
	// the system with this ID; nothing if the last read hasn't it (a read is asked for, at most every 5 seconds)
	static std::optional<WinSys::ComputeSystem> Find(std::wstring const& id);
	// the Virtual Machine column's text: "Windows11 (Hyper-V VM)", else the ID while it's unknown; empty if the process
	// isn't a VM's
	static std::wstring TextOf(ProcessManager const& pm, ProcessInfoEx& p);
	// the role of a VM's process: "worker process", "memory process", "security process", "WSL host"; empty for others
	static PCWSTR RoleOf(ProcessInfoEx& p);
};
