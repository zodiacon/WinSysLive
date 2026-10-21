#include "pch.h"
#include "ProcessSheet.h"
#include <WTLHelper.h>
#include <WinSys/ServiceManager.h>
#include <thread>
#include <mutex>
#include <atomic>

using namespace WinSys;

namespace {
	const UINT_PTR SampleTimerId = 1;

	bool IsInJob(uint32_t pid) {
		wil::unique_handle hProcess(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
		BOOL inJob = FALSE;
		return hProcess && ::IsProcessInJob(hProcess.get(), nullptr, &inJob) && inJob;
	}

	// the running services the process hosts
	size_t CountHostedServices(uint32_t pid) {
		return std::ranges::count_if(ServiceManager::EnumServices(ServiceEnumType::AllServices, ServiceEnumState::Active),
			[&](auto& svc) { return svc.GetStatusProcess().ProcessId == pid; });
	}
}

CProcessSheet::CProcessSheet(ProcessProperties props) : m_Props(std::move(props)) {
	m_Title = std::format(L"{} ({}) Properties", m_Props.ImageName, m_Props.Id);
	SetTitle(m_Title.c_str());
	// Apply stays for the Security page's changes (disabled until there are some; Close applies them too, as OK does)
	m_psh.dwFlags |= PSH_NOCONTEXTHELP;

	if (m_Props.ImagePath.empty() || ::ExtractIconEx(m_Props.ImagePath.c_str(), 0, &m_Icon.m_hIcon, &m_SmallIcon.m_hIcon, 1) == 0
		|| !m_Icon) {
		m_Icon.LoadIcon(IDI_APPLICATION);
		m_SmallIcon.LoadIcon(IDI_APPLICATION);
	}

	// the pages' order is the tabs'
	m_General = std::make_unique<CProcGeneralPage>(m_Props, m_Icon);
	AddPage(*m_General);
	m_Performance = std::make_unique<CProcPerformancePage>(m_Props);
	AddPage(*m_Performance);
	m_Environment = std::make_unique<CProcEnvironmentPage>(m_Props);
	AddPage(*m_Environment);
	m_Memory = std::make_unique<CProcMemoryPage>(m_Props);
	AddPage(*m_Memory);
	m_Token = std::make_unique<CProcTokenPage>(m_Props);
	AddPage(*m_Token);
	m_Security = CObjectSecurityInfo::Open([&](DWORD access) { return OpenSheetProcess(m_Props, access).release(); }, L"Process",
		std::format(L"{} ({})", m_Props.ImageName, m_Props.Id));
	if (auto page = ::CreateSecurityPage(m_Security)) {
		m_SecurityTab = GetPageCount();
		AddPage(page);
	}
	m_Threads = std::make_unique<CProcThreadsPage>(m_Props, m_Snapshot);
	AddPage(*m_Threads);
	m_TcpIp = std::make_unique<CProcTcpIpPage>(m_Props, m_Snapshot);
	AddPage(*m_TcpIp);
	if (IsInJob(m_Props.Id)) {
		m_Job = std::make_unique<CProcJobPage>(m_Props, m_Snapshot);
		AddPage(*m_Job);
	}
	if (auto count = CountHostedServices(m_Props.Id); count > 0) {
		m_Services = std::make_unique<CProcServicesPage>(m_Props, count == 1 ? L"Service" : L"Services");
		AddPage(*m_Services);
	}
}

CProcessSheet::~CProcessSheet() {
	if (m_Security)
		m_Security->Release();
}

void CProcessSheet::OnSheetInitialized() {
	BaseSheet::OnSheetInitialized();
	if (m_SecurityTab >= 0)
		SetSecurityTabIcon(m_hWnd, m_SecurityTab);
	m_AclTheming.Install(m_hWnd);

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

	// the controls are themed by WTLHelper's hook (InitThread), the title bar is not
	DarkMode::setDarkTitleBarEx(m_hWnd, true);

	Sample();
	SetTimer(SampleTimerId, m_Props.UpdateInterval, nullptr);
}

LRESULT CProcessSheet::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != SampleTimerId) {
		bHandled = FALSE;
		return 0;
	}
	Sample();
	return 0;
}

void CProcessSheet::Sample() {
	m_Snapshot.Take();
	m_Performance->OnSample(m_Snapshot);
	if (m_Threads->IsWindow() && GetActivePage() == m_Threads->m_hWnd)
		m_Threads->OnSample(m_Snapshot);
	if (m_TcpIp->IsWindow() && GetActivePage() == m_TcpIp->m_hWnd)
		m_TcpIp->OnSample(m_Snapshot);
	if (m_Job && m_Job->IsWindow() && GetActivePage() == m_Job->m_hWnd)
		m_Job->OnSample(m_Snapshot);
	if (m_Snapshot.Exited())
		KillTimer(SampleTimerId);
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
			// the sheet's windows themed as the main thread's are
			WTLHelper::InitThread();
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
		WTLHelper::TermThread();
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
	for (auto& t : threads)
		if (DWORD tid = *t.ThreadId)
			::PostThreadMessage(tid, WM_QUIT, 0, 0);
	// a sheet stuck in a page's query isn't waited for long: the process is exiting anyway. One by one, as
	// WaitForMultipleObjects takes at most 64 handles
	auto deadline = ::GetTickCount64() + 3000;
	for (auto& t : threads) {
		auto now = ::GetTickCount64();
		if (::WaitForSingleObject(t.Thread.native_handle(), now < deadline ? DWORD(deadline - now) : 0) == WAIT_OBJECT_0)
			t.Thread.join();
		else
			t.Thread.detach();
	}
}
