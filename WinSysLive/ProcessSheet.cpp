#include "pch.h"
#include "ProcessSheet.h"
#include <WTLHelper.h>
#include <WinSys/ServiceManager.h>
#include <thread>
#include <mutex>
#include <atomic>

using namespace WinSys;

namespace {
	bool IsInJob(uint32_t pid) {
		wil::unique_handle hProcess(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
		BOOL inJob = FALSE;
		return hProcess && ::IsProcessInJob(hProcess.get(), nullptr, &inJob) && inJob;
	}

	// the display names of the running services the process hosts
	std::vector<std::wstring> GetHostedServices(uint32_t pid) {
		std::vector<std::wstring> names;
		for (auto& svc : ServiceManager::EnumServices(ServiceEnumType::AllServices, ServiceEnumState::Active))
			if (svc.GetStatusProcess().ProcessId == pid)
				names.push_back(svc.GetDisplayName());
		return names;
	}
}

CProcessSheet::CProcessSheet(ProcessProperties props) : m_Props(std::move(props)) {
	m_Title = std::format(L"{} ({}) Properties", m_Props.ImageName, m_Props.Id);
	SetTitle(m_Title.c_str());
	m_psh.dwFlags |= PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;

	if (m_Props.ImagePath.empty() || ::ExtractIconEx(m_Props.ImagePath.c_str(), 0, &m_Icon.m_hIcon, &m_SmallIcon.m_hIcon, 1) == 0
		|| !m_Icon) {
		m_Icon.LoadIcon(IDI_APPLICATION);
		m_SmallIcon.LoadIcon(IDI_APPLICATION);
	}

	// the pages' order is the tabs'
	m_General = std::make_unique<CProcGeneralPage>(m_Props, m_Icon);
	AddPage(*m_General);
	auto add = [&](PCWSTR title, UINT icon, std::wstring text) {
		auto& page = m_Pages.emplace_back(std::make_unique<CProcPlaceholderPage>(title, icon, std::move(text)));
		AddPage(*page);
	};
	add(L"Performance", IDI_PERFORMANCE, L"CPU, memory and I/O over time.");
	add(L"Environment", IDI_ENVIRONMENT, L"The process's environment variables.");
	add(L"Memory", IDI_MEMORY, L"The process's address space: regions, their protection and usage.");
	add(L"Token", IDI_TOKEN, L"The process token: user, groups, privileges and integrity level.");
	add(L"Threads", IDI_THREADS, L"The process's threads.");
	if (IsInJob(m_Props.Id))
		add(L"Job", IDI_PROCESSES, L"The job the process is in: its limits and its processes.");
	if (auto services = GetHostedServices(m_Props.Id); !services.empty()) {
		std::wstring text = L"The services this process hosts:";
		for (auto& name : services)
			text += L"\n" + name;
		add(services.size() == 1 ? L"Service" : L"Services", IDI_SERVICES, std::move(text));
	}
}

void CProcessSheet::OnSheetInitialized() {
	BaseSheet::OnSheetInitialized();

	// a window of its own: the main window belongs to another thread
	ModifyStyleEx(0, WS_EX_APPWINDOW);
	SetIcon(m_Icon, TRUE);
	SetIcon(m_SmallIcon, FALSE);

	// only a Close button: nothing to apply or cancel
	CWindow cancel(GetDlgItem(IDCANCEL));
	// hidden, not disabled: Escape and the close box still close the sheet
	cancel.ShowWindow(SW_HIDE);
	GetDlgItem(IDOK).SetWindowText(L"Close");
	// rather than the first field, with its text selected
	PostMessage(WM_NEXTDLGCTL, (WPARAM)GetDlgItem(IDOK).m_hWnd, TRUE);

	// WTLHelper's hook themes new windows on the main thread only
	DarkMode::setDarkWndNotifySafe(m_hWnd);
	DarkMode::setDarkTitleBarEx(m_hWnd, true);
}

namespace {
	// the sheets' threads, so the app can close them before it exits
	struct SheetThread {
		std::thread Thread;
		std::shared_ptr<std::atomic<DWORD>> ThreadId;	// 0 once the thread is done
	};
	std::mutex s_Lock;
	std::vector<SheetThread> s_Threads;
	std::atomic<bool> s_Closing;
}

void ShowProcessProperties(ProcessProperties props) {
	std::lock_guard lock(s_Lock);
	if (s_Closing)
		return;
	// the threads of sheets already closed
	std::erase_if(s_Threads, [](auto& t) {
		if (*t.ThreadId)
			return false;
		t.Thread.join();
		return true;
	});

	auto id = std::make_shared<std::atomic<DWORD>>(0);
	std::thread thread([props = std::move(props), id]() mutable {
		{
			// a message queue, before CloseProcessProperties can post to it
			MSG msg;
			::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);
			*id = ::GetCurrentThreadId();
			CProcessSheet sheet(std::move(props));
			if (!s_Closing && sheet.Create(nullptr)) {
				sheet.ShowWindow(SW_SHOW);
				// a modeless sheet stays open until its active page is gone (Close, Escape, the close box);
				// CloseProcessProperties ends the loop with WM_QUIT
				while (::GetMessage(&msg, nullptr, 0, 0)) {
					if (!sheet.IsDialogMessage(&msg)) {
						::TranslateMessage(&msg);
						::DispatchMessage(&msg);
					}
					if (!sheet.IsWindow() || !sheet.GetActivePage())
						break;
				}
				if (sheet.IsWindow())
					sheet.DestroyWindow();
			}
		}
		*id = 0;
	});
	// the thread sets its ID once it has a message queue
	while (*id == 0 && ::WaitForSingleObject(thread.native_handle(), 0) == WAIT_TIMEOUT)
		::Sleep(1);
	s_Threads.push_back({ std::move(thread), id });
}

void CloseProcessProperties() {
	std::vector<SheetThread> threads;
	{
		std::lock_guard lock(s_Lock);
		s_Closing = true;
		threads = std::move(s_Threads);
	}
	std::vector<HANDLE> handles;
	for (auto& t : threads) {
		if (DWORD tid = *t.ThreadId)
			::PostThreadMessage(tid, WM_QUIT, 0, 0);
		handles.push_back(t.Thread.native_handle());
	}
	// a sheet stuck in a page's query isn't waited for long: the process is exiting anyway
	if (!handles.empty() && ::WaitForMultipleObjects((DWORD)handles.size(), handles.data(), TRUE, 3000) != WAIT_TIMEOUT) {
		for (auto& t : threads)
			t.Thread.join();
	}
	else {
		for (auto& t : threads)
			t.Thread.detach();
	}
}
