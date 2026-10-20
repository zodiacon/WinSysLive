#include "pch.h"
#include "JobPage.h"
#include "FormatHelper.h"
#include <WinSys/Handles.h>
#include <WinSys/Helpers.h>
#include <unordered_set>

namespace {
	enum class ProcessColumn {
		Name, Id, Session, CpuTime, PrivateBytes, WorkingSet, Threads, Handles
	};

	void InitList(CListViewCtrl& list, HWND hList, std::initializer_list<std::tuple<PCWSTR, int, int>> columns) {
		list.Attach(hList);
		list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		auto dpi = ::GetDpiForWindow(hList);
		int i = 0;
		for (auto& [name, width, format] : columns)
			list.InsertColumn(i++, name, format, ::MulDiv(width, dpi, 96));
	}

	struct Row {
		LPARAM Key;
		std::vector<std::wstring> Texts;
	};

	// sets the list's rows, changing only the texts that differ, so it doesn't flicker or lose its scroll position;
	// the selection follows its row's key
	void SetRows(CListViewCtrl& list, std::vector<Row> const& rows) {
		int selected = list.GetNextItem(-1, LVNI_SELECTED);
		auto key = selected < 0 ? LPARAM(-1) : list.GetItemData(selected);
		list.SetRedraw(FALSE);
		int count = list.GetItemCount();
		for (int i = count - 1; i >= (int)rows.size(); i--)
			list.DeleteItem(i);
		CString text;
		for (int i = 0; i < (int)rows.size(); i++) {
			auto& row = rows[i];
			if (i >= count)
				list.InsertItem(i, row.Texts[0].c_str());
			list.SetItemData(i, row.Key);
			for (int col = 0; col < (int)row.Texts.size(); col++) {
				list.GetItemText(i, col, text);
				if (text != row.Texts[col].c_str())
					list.SetItemText(i, col, row.Texts[col].c_str());
			}
		}
		if (selected >= 0) {
			int now = -1;
			for (int i = 0; i < (int)rows.size() && now < 0; i++)
				if (rows[i].Key == key)
					now = i;
			if (now != selected) {
				list.SetItemState(-1, 0, LVIS_SELECTED);
				if (now >= 0)
					list.SetItemState(now, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
		}
		list.SetRedraw(TRUE);
	}

	std::wstring FormatBytes(uint64_t bytes) {
		return bytes ? FormatHelper::FormatBytes(bytes) : L"0";
	}

	PCWSTR PriorityClassToString(DWORD priorityClass) {
		switch (priorityClass) {
			case IDLE_PRIORITY_CLASS: return L"Idle";
			case BELOW_NORMAL_PRIORITY_CLASS: return L"Below Normal";
			case NORMAL_PRIORITY_CLASS: return L"Normal";
			case ABOVE_NORMAL_PRIORITY_CLASS: return L"Above Normal";
			case HIGH_PRIORITY_CLASS: return L"High";
			case REALTIME_PRIORITY_CLASS: return L"Realtime";
		}
		return L"Unknown";
	}

	// in 1/100 of a percent, as the job's CPU rates are
	std::wstring FormatRate(DWORD rate) {
		return rate % 100 ? std::format(L"{:.2f}%", rate / 100.0) : std::format(L"{}%", rate / 100);
	}

	std::wstring GetObjectName(HANDLE h) {
		BYTE buffer[1024];
		ULONG len;
		if (!NT_SUCCESS(::NtQueryObject(h, ObjectNameInformation, buffer, sizeof(buffer), &len)))
			return L"";
		auto name = reinterpret_cast<UNICODE_STRING const*>(buffer);
		return std::wstring(name->Buffer, name->Length / sizeof(WCHAR));
	}

	DWORD GetActiveProcesses(HANDLE hJob) {
		JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
		return ::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)
			? info.ActiveProcesses : 0;
	}

	// the IDs of the job's processes; empty if they can't be read
	std::vector<uint32_t> GetJobProcesses(HANDLE hJob) {
		DWORD count = 64;
		for (int i = 0; i < 4; i++) {
			auto size = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + count * sizeof(ULONG_PTR);
			auto buffer = std::make_unique<BYTE[]>(size);
			auto list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.get());
			if (::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectBasicProcessIdList, list, (DWORD)size, nullptr)) {
				std::vector<uint32_t> ids;
				for (DWORD n = 0; n < list->NumberOfProcessIdsInList; n++)
					ids.push_back((uint32_t)list->ProcessIdList[n]);
				return ids;
			}
			if (::GetLastError() != ERROR_MORE_DATA)
				break;
			// processes are added between the calls
			count = list->NumberOfAssignedProcesses + 64;
		}
		return {};
	}
}

CProcJobPage::CProcJobPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Props(props), m_Snapshot(snapshot) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_JOB);
}

LRESULT CProcJobPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_JobList.Attach(GetDlgItem(IDC_JOB_JOBS));
	InitList(m_Limits, GetDlgItem(IDC_JOB_LIMITS), { { L"Limit", 150, LVCFMT_LEFT }, { L"Value", 220, LVCFMT_LEFT } });
	InitList(m_Stats, GetDlgItem(IDC_JOB_STATS), { { L"Statistic", 150, LVCFMT_LEFT }, { L"Value", 120, LVCFMT_RIGHT } });
	InitList(m_Processes, GetDlgItem(IDC_JOB_PROCESSES), {
		{ L"Name", 180, LVCFMT_LEFT }, { L"PID", 60, LVCFMT_RIGHT }, { L"Session", 60, LVCFMT_RIGHT },
		{ L"CPU Time", 100, LVCFMT_RIGHT }, { L"Private Bytes", 100, LVCFMT_RIGHT }, { L"Working Set", 100, LVCFMT_RIGHT },
		{ L"Threads", 60, LVCFMT_RIGHT }, { L"Handles", 60, LVCFMT_RIGHT } });
	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcJobPage::OnSetActive() {
	if (!m_Loaded) {
		m_Loaded = true;
		CWaitCursor wait;
		FindJobs();
	}
	// the sheet's last snapshot; the sheet feeds the page only while it's active
	OnSample(m_Snapshot);
	return 0;
}

void CProcJobPage::OnSample(ProcessSnapshot const& snapshot) {
	if (m_JobList && !m_Jobs.empty())
		ShowJob(snapshot);
}

LRESULT CProcJobPage::OnJobChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Selected = false;
	ShowJob(m_Snapshot);
	return 0;
}

void CProcJobPage::FindJobs() {
	auto status = [&](std::wstring const& text) {
		CWindow status(GetDlgItem(IDC_JOB_STATUS));
		status.SetWindowText(text.c_str());
		status.ShowWindow(SW_SHOW);
	};

	auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION);
	if (!hProcess) {
		status(L"Can't open the process: " + WinSys::Helpers::GetErrorText(::GetLastError()));
		return;
	}

	// the system's handle table; the size needed grows between calls
	ULONG size = 1 << 22;
	std::unique_ptr<BYTE[]> buffer;
	NTSTATUS st = STATUS_INFO_LENGTH_MISMATCH;
	for (int i = 0; i < 6 && st == STATUS_INFO_LENGTH_MISMATCH; i++) {
		buffer = std::make_unique<BYTE[]>(size);
		ULONG len = 0;
		st = ::NtQuerySystemInformation(SystemExtendedHandleInformation, buffer.get(), size, &len);
		if (st == STATUS_INFO_LENGTH_MISMATCH)
			size = (std::max)(size * 2, len + (1 << 20));
	}
	if (!NT_SUCCESS(st)) {
		status(L"Can't read the handle table: " + WinSys::Helpers::GetErrorText(::RtlNtStatusToDosError(st)));
		return;
	}

	auto table = reinterpret_cast<SYSTEM_HANDLE_INFORMATION_EX const*>(buffer.get());
	std::unordered_map<USHORT, bool> isJobType;
	std::unordered_map<ULONG_PTR, wil::unique_handle> owners;	// opened for duplicating; null if they can't be
	std::unordered_set<void*> seen;	// job objects already checked (their addresses are known only when elevated)
	auto self = ::GetCurrentProcess();
	for (ULONG_PTR i = 0; i < table->NumberOfHandles; i++) {
		auto& entry = table->Handles[i];
		auto [type, added] = isJobType.try_emplace(entry.ObjectTypeIndex, false);
		if (added)
			type->second = WinSys::ObjectTypes::GetName(entry.ObjectTypeIndex) == L"Job";
		if (!type->second || (entry.Object && seen.contains(entry.Object)))
			continue;

		auto pid = (ULONG_PTR)entry.UniqueProcessId;
		auto owner = owners.find(pid);
		if (owner == owners.end())
			owner = owners.emplace(pid, wil::unique_handle(::OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)pid))).first;
		if (!owner->second)
			continue;
		wil::unique_handle hJob;
		if (!::DuplicateHandle(owner->second.get(), entry.HandleValue, self, hJob.addressof(), JOB_OBJECT_QUERY, FALSE, 0))
			continue;
		if (entry.Object)
			seen.insert(entry.Object);

		BOOL inJob = FALSE;
		if (!::IsProcessInJob(hProcess.get(), hJob.get(), &inJob) || !inJob)
			continue;
		// another handle to a job already found (when the addresses aren't known)
		if (std::ranges::any_of(m_Jobs, [&](auto& job) { return ::NtCompareObjects(job.Handle.get(), hJob.get()) == STATUS_SUCCESS; }))
			continue;
		Job job;
		job.Name = GetObjectName(hJob.get());
		SILOOBJECT_BASIC_INFORMATION silo;
		job.Silo = NT_SUCCESS(::NtQueryInformationJobObject(hJob.get(), (JOBOBJECTINFOCLASS)JobObjectSiloBasicInformation,
			&silo, sizeof(silo), nullptr));
		job.Handle = std::move(hJob);
		m_Jobs.push_back(std::move(job));
	}

	if (m_Jobs.empty()) {
		status(L"The process is in a job, but no handle to it could be opened (most need administrator rights).");
		return;
	}

	// a job's ancestors hold its processes and more, so the innermost job has the fewest
	std::vector<std::pair<DWORD, size_t>> order;
	for (size_t i = 0; i < m_Jobs.size(); i++)
		order.emplace_back(GetActiveProcesses(m_Jobs[i].Handle.get()), i);
	std::ranges::stable_sort(order);
	std::vector<Job> jobs;
	for (auto& [count, i] : order)
		jobs.push_back(std::move(m_Jobs[i]));
	m_Jobs = std::move(jobs);

	for (size_t i = 0; i < m_Jobs.size(); i++) {
		auto& job = m_Jobs[i];
		auto text = job.Name.empty() ? std::wstring(L"(unnamed)") : job.Name;
		if (job.Silo)
			text += L" (silo)";
		if (m_Jobs.size() > 1)
			text += i == 0 ? L" - innermost" : std::format(L" - nesting level {}", i);
		m_JobList.AddString(text.c_str());
	}
	m_JobList.SetCurSel(0);
	// a choice only for nested jobs
	if (m_Jobs.size() > 1) {
		m_JobList.ShowWindow(SW_SHOW);
	}
	else {
		CWindow name(GetDlgItem(IDC_JOB_NAME));
		CString text;
		m_JobList.GetLBText(0, text);
		name.SetWindowText(text);
		name.ShowWindow(SW_SHOW);
	}
}

void CProcJobPage::ShowJob(ProcessSnapshot const& snapshot) {
	int index = m_JobList.GetCurSel();
	if (index < 0 || index >= (int)m_Jobs.size())
		return;
	auto hJob = m_Jobs[index].Handle.get();
	ShowLimits(hJob);
	ShowStatistics(hJob);
	ShowProcesses(hJob, snapshot);
}

void CProcJobPage::ShowLimits(HANDLE hJob) {
	std::vector<Row> rows;
	auto add = [&](std::wstring name, std::wstring value) {
		rows.push_back({ (LPARAM)rows.size(), { std::move(name), std::move(value) } });
	};

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
	if (!::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectExtendedLimitInformation, &info, sizeof(info), nullptr)) {
		add(L"Error", WinSys::Helpers::GetErrorText(::GetLastError()));
		SetRows(m_Limits, rows);
		return;
	}
	auto& basic = info.BasicLimitInformation;
	auto flags = basic.LimitFlags;
	if (flags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS)
		add(L"Active processes", std::to_wstring(basic.ActiveProcessLimit));
	if (flags & JOB_OBJECT_LIMIT_JOB_MEMORY)
		add(L"Job memory", FormatBytes(info.JobMemoryLimit));
	if (flags & JOB_OBJECT_LIMIT_PROCESS_MEMORY)
		add(L"Process memory", FormatBytes(info.ProcessMemoryLimit));
	if (flags & JOB_OBJECT_LIMIT_WORKINGSET) {
		add(L"Minimum working set", FormatBytes(basic.MinimumWorkingSetSize));
		add(L"Maximum working set", FormatBytes(basic.MaximumWorkingSetSize));
	}
	if (flags & JOB_OBJECT_LIMIT_JOB_TIME)
		add(L"Job user time", FormatHelper::FormatTimeSpan(basic.PerJobUserTimeLimit.QuadPart));
	if (flags & JOB_OBJECT_LIMIT_PROCESS_TIME)
		add(L"Process user time", FormatHelper::FormatTimeSpan(basic.PerProcessUserTimeLimit.QuadPart));
	if (flags & JOB_OBJECT_LIMIT_PRIORITY_CLASS)
		add(L"Priority class", PriorityClassToString(basic.PriorityClass));
	if (flags & JOB_OBJECT_LIMIT_SCHEDULING_CLASS)
		add(L"Scheduling class", std::to_wstring(basic.SchedulingClass));
	if (flags & JOB_OBJECT_LIMIT_AFFINITY)
		add(L"Affinity", std::format(L"0x{:X}", basic.Affinity));
	if (flags & JOB_OBJECT_LIMIT_SUBSET_AFFINITY)
		add(L"Subset affinity", L"Yes");
	if (flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
		add(L"Kill on job close", L"Yes");
	if (flags & JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION)
		add(L"Die on unhandled exception", L"Yes");
	if (flags & JOB_OBJECT_LIMIT_BREAKAWAY_OK)
		add(L"Breakaway OK", L"Yes");
	if (flags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
		add(L"Silent breakaway OK", L"Yes");
	if (flags & JOB_OBJECT_LIMIT_PRESERVE_JOB_TIME)
		add(L"Preserve job time", L"Yes");

	JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu;
	if (::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectCpuRateControlInformation, &cpu, sizeof(cpu), nullptr)
		&& (cpu.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE)) {
		std::wstring value;
		if (cpu.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED)
			value = std::format(L"Weight {}", cpu.Weight);
		else if (cpu.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE)
			value = std::format(L"Minimum {}, maximum {}", FormatRate(cpu.MinRate), FormatRate(cpu.MaxRate));
		else
			value = FormatRate(cpu.CpuRate);
		if (cpu.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP)
			value += L" (hard cap)";
		if (cpu.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_NOTIFY)
			value += L" (notify)";
		add(L"CPU rate", value);
	}

	JOBOBJECT_NET_RATE_CONTROL_INFORMATION net;
	if (::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectNetRateControlInformation, &net, sizeof(net), nullptr)
		&& (net.ControlFlags & JOB_OBJECT_NET_RATE_CONTROL_ENABLE)) {
		if (net.ControlFlags & JOB_OBJECT_NET_RATE_CONTROL_MAX_BANDWIDTH)
			add(L"Network bandwidth", FormatBytes(net.MaxBandwidth) + L"/s");
		if (net.ControlFlags & JOB_OBJECT_NET_RATE_CONTROL_DSCP_TAG)
			add(L"Network DSCP tag", std::to_wstring(net.DscpTag));
	}

	JOBOBJECT_IO_RATE_CONTROL_INFORMATION* io = nullptr;
	ULONG ioCount = 0;
	if (::QueryIoRateControlInformationJobObject(hJob, nullptr, &io, &ioCount) == ERROR_SUCCESS && io) {
		for (ULONG i = 0; i < ioCount; i++) {
			auto& rate = io[i];
			if (!(rate.ControlFlags & JOB_OBJECT_IO_RATE_CONTROL_ENABLE))
				continue;
			std::wstring value;
			auto append = [&](std::wstring text) {
				value += (value.empty() ? L"" : L", ") + text;
			};
			if (rate.MaxIops)
				append(std::format(L"{} IOPS", rate.MaxIops));
			if (rate.MaxBandwidth)
				append(FormatBytes(rate.MaxBandwidth) + L"/s");
			if (rate.ReservationIops)
				append(std::format(L"{} IOPS reserved", rate.ReservationIops));
			add(rate.VolumeName && *rate.VolumeName ? std::format(L"I/O rate ({})", rate.VolumeName) : L"I/O rate", value);
		}
		::FreeMemoryJobObject(io);
	}

	JOBOBJECT_BASIC_UI_RESTRICTIONS ui;
	if (::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectBasicUIRestrictions, &ui, sizeof(ui), nullptr) && ui.UIRestrictionsClass) {
		static const std::pair<DWORD, PCWSTR> restrictions[] = {
			{ JOB_OBJECT_UILIMIT_DESKTOP, L"Desktops" },
			{ JOB_OBJECT_UILIMIT_DISPLAYSETTINGS, L"Display settings" },
			{ JOB_OBJECT_UILIMIT_EXITWINDOWS, L"Exit Windows" },
			{ JOB_OBJECT_UILIMIT_GLOBALATOMS, L"Global atoms" },
			{ JOB_OBJECT_UILIMIT_HANDLES, L"USER handles" },
			{ JOB_OBJECT_UILIMIT_READCLIPBOARD, L"Read clipboard" },
			{ JOB_OBJECT_UILIMIT_WRITECLIPBOARD, L"Write clipboard" },
			{ JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS, L"System parameters" },
		};
		std::wstring value;
		for (auto& [flag, name] : restrictions)
			if (ui.UIRestrictionsClass & flag)
				value += (value.empty() ? L"" : L", ") + std::wstring(name);
		add(L"UI restrictions", value);
	}

	if (rows.empty())
		add(L"No limits", L"");
	SetRows(m_Limits, rows);
}

void CProcJobPage::ShowStatistics(HANDLE hJob) {
	std::vector<Row> rows;
	auto add = [&](PCWSTR name, std::wstring value) {
		rows.push_back({ (LPARAM)rows.size(), { name, std::move(value) } });
	};
	auto number = [](int64_t value) {
		return FormatHelper::FormatNumber(value);
	};

	JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION info;
	if (!::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectBasicAndIoAccountingInformation, &info, sizeof(info), nullptr)) {
		add(L"Error", WinSys::Helpers::GetErrorText(::GetLastError()));
		SetRows(m_Stats, rows);
		return;
	}
	auto& basic = info.BasicInfo;
	auto& io = info.IoInfo;
	add(L"Active processes", number(basic.ActiveProcesses));
	add(L"Total processes", number(basic.TotalProcesses));
	add(L"Terminated processes", number(basic.TotalTerminatedProcesses));
	add(L"Total CPU time", FormatHelper::FormatTimeSpan(basic.TotalUserTime.QuadPart + basic.TotalKernelTime.QuadPart));
	add(L"Total user time", FormatHelper::FormatTimeSpan(basic.TotalUserTime.QuadPart));
	add(L"Total kernel time", FormatHelper::FormatTimeSpan(basic.TotalKernelTime.QuadPart));
	add(L"Period user time", FormatHelper::FormatTimeSpan(basic.ThisPeriodTotalUserTime.QuadPart));
	add(L"Period kernel time", FormatHelper::FormatTimeSpan(basic.ThisPeriodTotalKernelTime.QuadPart));
	add(L"Page faults", number(basic.TotalPageFaultCount));

	JOBOBJECT_MEMORY_USAGE_INFORMATION memory;
	if (NT_SUCCESS(::NtQueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectMemoryUsageInformation, &memory,
		sizeof(memory), nullptr))) {
		add(L"Job memory", FormatBytes(memory.JobMemory));
		add(L"Peak job memory", FormatBytes(memory.PeakJobMemoryUsed));
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
	if (::QueryInformationJobObject(hJob, (JOBOBJECTINFOCLASS)JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
		add(L"Peak process memory", FormatBytes(limits.PeakProcessMemoryUsed));

	add(L"Read operations", number(io.ReadOperationCount));
	add(L"Read bytes", FormatBytes(io.ReadTransferCount));
	add(L"Write operations", number(io.WriteOperationCount));
	add(L"Write bytes", FormatBytes(io.WriteTransferCount));
	add(L"Other operations", number(io.OtherOperationCount));
	add(L"Other bytes", FormatBytes(io.OtherTransferCount));
	SetRows(m_Stats, rows);
}

void CProcJobPage::ShowProcesses(HANDLE hJob, ProcessSnapshot const& snapshot) {
	std::vector<Row> rows;
	for (auto pid : GetJobProcesses(hJob)) {
		Row row{ (LPARAM)pid };
		auto& texts = row.Texts;
		auto p = snapshot.Find(pid);
		if (p) {
			m_Names[pid] = std::wstring(p->ImageName.Buffer, p->ImageName.Length / sizeof(WCHAR));
		}
		else if (!m_Names.contains(pid)) {
			// not in the snapshot: newer than it, or the sheet's process is gone and the sheet takes no more
			wil::unique_handle hProcess(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
			WCHAR path[MAX_PATH];
			DWORD size = _countof(path);
			if (hProcess && ::QueryFullProcessImageName(hProcess.get(), 0, path, &size)) {
				auto name = wcsrchr(path, L'\\');
				m_Names[pid] = name ? name + 1 : path;
			}
		}
		auto name = m_Names.find(pid);
		texts.push_back(name == m_Names.end() ? L"" : name->second);
		texts.push_back(std::to_wstring(pid));
		if (p) {
			texts.push_back(std::to_wstring(p->SessionId));
			texts.push_back(FormatHelper::FormatTimeSpan(p->UserTime.QuadPart + p->KernelTime.QuadPart));
			texts.push_back(FormatHelper::FormatSize(p->PagefileUsage));
			texts.push_back(FormatHelper::FormatSize(p->WorkingSetSize));
			texts.push_back(std::to_wstring(p->NumberOfThreads));
			texts.push_back(std::to_wstring(p->HandleCount));
		}
		else {
			texts.resize((size_t)ProcessColumn::Handles + 1);
		}
		rows.push_back(std::move(row));
	}
	SetRows(m_Processes, rows);
	SetDlgItemText(IDC_JOB_PROCS_LABEL, std::format(L"Processes ({}):", rows.size()).c_str());

	if (!m_Selected) {
		// the sheet's process, when the job is first shown
		m_Selected = true;
		for (int i = 0; i < (int)rows.size(); i++)
			if (rows[i].Key == (LPARAM)m_Props.Id) {
				m_Processes.SetItemState(-1, 0, LVIS_SELECTED);
				m_Processes.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				m_Processes.EnsureVisible(i, FALSE);
				break;
			}
	}
}
