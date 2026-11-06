#include "pch.h"
#include "ThreadCommands.h"
#include "resource.h"
#include "StackDlg.h"
#include "WaitChainDlg.h"
#include "AffinityDlg.h"
#include <WTLHelper.h>
#include <WinSys/SystemInfo.h>
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>

using namespace WinSys;

namespace {
	// in the Set Priority submenu's order (ID_THREAD_PRIORITY_TIMECRITICAL...)
	const struct {
		ThreadPriorityLevel Level;
		PCWSTR Name;
	} s_Priorities[] = {
		{ ThreadPriorityLevel::TimeCritical, L"&Time Critical" },
		{ ThreadPriorityLevel::Highest, L"&Highest" },
		{ ThreadPriorityLevel::AboveNormal, L"&Above Normal" },
		{ ThreadPriorityLevel::Normal, L"&Normal" },
		{ ThreadPriorityLevel::BelowNormal, L"&Below Normal" },
		{ ThreadPriorityLevel::Lowest, L"&Lowest" },
		{ ThreadPriorityLevel::Idle, L"&Idle" },
	};
	static_assert(_countof(s_Priorities) == ID_THREAD_PRIORITY_IDLE - ID_THREAD_PRIORITY_TIMECRITICAL + 1);

	// the thread, with the access (and limited query access, to tell it from a later thread with its ID by its creation
	// time); nullptr with the last error set
	std::unique_ptr<Thread> OpenTarget(ThreadCommands::Target const& target, ThreadAccessMask access) {
		auto thread = Thread::OpenById(target.Thread.Id, access | ThreadAccessMask::QueryLimitedInformation);
		if (thread) {
			auto created = thread->GetCreateTime();
			if (created && created != target.Thread.Created) {
				::SetLastError(ERROR_INVALID_PARAMETER);
				return nullptr;
			}
		}
		return thread;
	}

	void Failed(HWND owner, PCWSTR what) {
		AtlMessageBox(owner, std::format(L"Failed to {}: {}", what, Helpers::GetErrorText(::GetLastError())).c_str(), IDR_MAINFRAME, MB_ICONERROR);
	}

	void Control(HWND owner, UINT id, ThreadCommands::Target const& target) {
		PCWSTR verb = id == ID_THREAD_TERMINATE ? L"Terminate" : id == ID_THREAD_SUSPEND ? L"Suspend" : L"Resume";
		auto question = std::format(L"{} thread {} of {} ({})?", verb, target.Thread.Id, target.ProcessName, target.Pid);
		if (id == ID_THREAD_TERMINATE)
			question += L"\n\nTerminating a thread can leave the process in an inconsistent state.";
		if (AtlMessageBox(owner, question.c_str(), IDR_MAINFRAME,
			MB_OKCANCEL | (id == ID_THREAD_RESUME ? MB_ICONQUESTION : MB_ICONWARNING | MB_DEFBUTTON2)) != IDOK)
			return;
		auto thread = OpenTarget(target, id == ID_THREAD_TERMINATE ? ThreadAccessMask::Terminate : ThreadAccessMask::SuspendResume);
		bool ok = thread && (id == ID_THREAD_TERMINATE ? thread->Terminate(1) : id == ID_THREAD_SUSPEND ? thread->Suspend() : thread->Resume());
		if (!ok)
			AtlMessageBox(owner, std::format(L"Failed to {} the thread: {}", verb, Helpers::GetErrorText(::GetLastError())).c_str(),
				IDR_MAINFRAME, MB_ICONERROR);
		// the state shows with the next refresh
	}

	void Affinity(HWND owner, ThreadCommands::Target const& target) {
		auto thread = OpenTarget(target, ThreadAccessMask::SetInformation | ThreadAccessMask::QueryInformation);
		uint16_t group = 0;
		KAFFINITY mask = 0, allowed = 0;
		if (!thread || !thread->GetAffinity(group, mask, allowed)) {
			Failed(owner, L"get the thread's affinity");
			return;
		}
		auto title = std::format(L"Thread {} of {} ({})", target.Thread.Id, target.ProcessName, target.Pid);
		if (auto groups = SystemInfo::GetProcessorGroups(); groups.size() > 1) {
			// over 64 processors: the thread may move to another group, unless its process is held to one by a mask
			DWORD_PTR processMask = 0, systemMask = 0;
			auto process = Process::OpenById(target.Pid);
			bool held = process && process->GetAffinity(processMask, systemMask);
			std::vector<CAffinityDlg::Group> choices;
			int first = 0;
			for (auto& g : groups) {
				choices.push_back({ g.Group, g.Group == group ? allowed : g.ActiveMask, g.Group == group ? mask : 0, !held || g.Group == group, first });
				first += g.ActiveCount;
			}
			auto note = held ? std::format(L"The process is held to group {} by its affinity mask; its threads stay there.", group)
				: std::wstring(L"A thread runs in one group: choosing processors in another moves it there.");
			CAffinityDlg dlg(title, choices, true, note);
			if (dlg.DoModal(owner) != IDOK)
				return;
			auto& chosen = dlg.GetGroups();
			auto it = std::ranges::find_if(chosen, [](auto& g) { return g.Selected != 0; });
			if (it == chosen.end() || (it->Number == group && it->Selected == mask))
				return;
			if (!thread->SetGroupAffinity(it->Number, it->Selected))
				Failed(owner, L"set the thread's affinity");
			return;
		}
		// the processors of the thread's group
		CAffinityDlg dlg(title, mask, allowed, true);
		if (dlg.DoModal(owner) == IDOK && dlg.GetMask() != mask && !thread->SetAffinity(dlg.GetMask()))
			Failed(owner, L"set the thread's affinity");
	}
}

UINT ThreadCommands::StateIcon(uint32_t state) {
	switch (state) {
		case 1: case 3: case 7: return IDI_CONTINUE;	// Ready, Standby, Deferred Ready
		case 2: return IDI_RUN;		// Running
		case 4: return IDI_KILL;	// Terminated
		case 5: case 8: return IDI_PAUSE;	// Waiting, Gate Wait
		case 6: case 9: return IDI_MEMORY;	// Transition, Waiting for Swap: the kernel stack isn't resident
	}
	return IDI_THREADS;	// Initialized
}

bool ThreadCommands::CanControl(uint32_t pid) {
	return pid > 4 && pid != ::GetCurrentProcessId();
}

void ThreadCommands::BuildMenu(CMenu& menu, Target const& target, std::optional<ThreadPriorityLevel> current, UINT copyId, int iconSize) {
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_THREAD_STACK, L"&Stack...");
	menu.AppendMenu(MF_STRING, ID_THREAD_WAITCHAIN, L"&Wait Chain...");
	menu.AppendMenu(MF_SEPARATOR);
	UINT control = CanControl(target.Pid) ? MF_STRING : MF_STRING | MF_GRAYED;
	if (target.Suspended)
		menu.AppendMenu(control, ID_THREAD_RESUME, L"&Resume");
	else
		menu.AppendMenu(control, ID_THREAD_SUSPEND, L"S&uspend");
	menu.AppendMenu(control, ID_THREAD_TERMINATE, L"&Terminate");
	menu.AppendMenu(MF_SEPARATOR);
	CMenu priorities;
	priorities.CreatePopupMenu();
	for (int i = 0; i < _countof(s_Priorities); i++)
		priorities.AppendMenu(MF_STRING, ID_THREAD_PRIORITY_TIMECRITICAL + i, s_Priorities[i].Name);
	if (current)
		for (int i = 0; i < _countof(s_Priorities); i++)
			if (s_Priorities[i].Level == *current)
				priorities.CheckMenuRadioItem(ID_THREAD_PRIORITY_TIMECRITICAL, ID_THREAD_PRIORITY_IDLE, ID_THREAD_PRIORITY_TIMECRITICAL + i, MF_BYCOMMAND);
	menu.AppendMenu(control | MF_POPUP, (UINT_PTR)priorities.Detach(), L"Set &Priority");
	menu.AppendMenu(control, ID_THREAD_AFFINITY, L"&Affinity...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, copyId, L"C&opy");
	// the icons, as the frame's menus have them
	MenuItemData const icons[] = {
		{ ID_THREAD_STACK, IDI_STACK },
		{ target.Suspended ? ID_THREAD_RESUME : ID_THREAD_SUSPEND, target.Suspended ? IDI_RUN : IDI_PAUSE },
		{ ID_THREAD_TERMINATE, IDI_DELETE },
		{ (int)copyId, IDI_COPY },
	};
	WTLHelper::InitMenu(menu.m_hMenu, icons, _countof(icons), iconSize);
}

bool ThreadCommands::Execute(HWND owner, UINT id, Target const& target) {
	switch (id) {
		case ID_THREAD_STACK:
		{
			CStackDlg dlg(target.Pid, target.ProcessCreated, target.Thread.Id, target.ProcessName);
			dlg.DoModal(owner);
			return true;
		}
		case ID_THREAD_WAITCHAIN:
		{
			CWaitChainDlg dlg(target.Pid, target.ProcessName, { target.Thread.Id });
			dlg.DoModal(owner);
			return true;
		}
		case ID_THREAD_TERMINATE: case ID_THREAD_SUSPEND: case ID_THREAD_RESUME:
			if (CanControl(target.Pid))
				Control(owner, id, target);
			return true;
		case ID_THREAD_AFFINITY:
			if (CanControl(target.Pid))
				Affinity(owner, target);
			return true;
	}
	if (id >= ID_THREAD_PRIORITY_TIMECRITICAL && id <= ID_THREAD_PRIORITY_IDLE) {
		if (!CanControl(target.Pid))
			return true;
		auto thread = OpenTarget(target, ThreadAccessMask::SetLimitedInformation);
		if (!thread || !thread->SetPriority(s_Priorities[id - ID_THREAD_PRIORITY_TIMECRITICAL].Level))
			Failed(owner, L"set the thread's priority");
		// the new priority shows with the next refresh
		return true;
	}
	return false;
}
