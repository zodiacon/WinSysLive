#include "pch.h"
#include "HistoryView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/Helpers.h>
#include "ListExport.h"
#include <WTLHelper.h>

using namespace WinSys;

namespace {
	// a gutter marker per kind (the text keeps the view's colors, light or dark); styles 1.. in Event order
	const COLORREF s_Markers[] = {
		RGB(60, 170, 80),	// process started
		RGB(210, 80, 80),	// process exited
		RGB(60, 130, 210),	// service started
		RGB(200, 120, 40),	// service stopped
		RGB(150, 110, 200),	// service paused
		RGB(120, 120, 120),	// service installed or deleted
	};

	std::wstring Duration(int64_t ticks) {
		// 100 ns units, as h:mm:ss
		auto seconds = ticks / 10000000;
		return std::format(L"{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
	}
}

CString CHistoryView::GetTitle() const {
	return L"History";
}

LRESULT CHistoryView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Markers) == (int)Event::_Count);
	m_hWndClient = m_Log.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Log.ShowToolbar(true);
	m_Log.ShowLineNumbers(false);
	m_Log.SetLineLimit(LineLimit);
	for (int i = 0; i < (int)Event::_Count; i++) {
		LGVSTYLE style;
		style.crMarker = s_Markers[i];
		m_Log.SetStyle(BYTE(i + 1), &style);
	}
	m_Events = (uint32_t)AppSettings::Get().HistoryEvents();
	m_Since = Now();
	TakeBaseline();
	return 0;
}

int64_t CHistoryView::Now() {
	int64_t now;
	::GetSystemTimeAsFileTime((FILETIME*)&now);
	return now;
}

bool CHistoryView::IsEnabled(Event kind) const {
	return (m_Events & (1u << (int)kind)) != 0;
}

PCWSTR CHistoryView::EventName(Event kind) {
	switch (kind) {
		case Event::ProcessStarted: return L"Process Started";
		case Event::ProcessExited: return L"Process Exited";
		case Event::ServiceStarted: return L"Service Started";
		case Event::ServiceStopped: return L"Service Stopped";
		case Event::ServicePaused: return L"Service Paused or Resumed";
		case Event::ServiceInstalled: return L"Service Installed or Deleted";
	}
	return L"";
}

void CHistoryView::TakeBaseline() {
	auto& pm = Frame()->GetProcessManager();
	for (auto& p : pm.GetProcesses()) {
		m_BaselineProcesses.insert(p->Key);
		if (p->Id > 4)
			m_Processes[p->Key] = Process::OpenById(p->Id, p->Key.Created, ProcessAccessMask::QueryLimitedInformation | ProcessAccessMask::Synchronize);
	}
	for (auto& svc : ServiceManager::EnumServices(ServiceEnumType::AllServices)) {
		auto& status = svc.GetStatusProcess();
		m_Services[svc.GetName()] = { status.CurrentState, status.ProcessId, svc.GetDisplayName() };
	}
}

std::wstring CHistoryView::Format(int64_t time, PCWSTR label, std::wstring const& text) {
	FILETIME local;
	SYSTEMTIME st{};
	if (::FileTimeToLocalFileTime((FILETIME const*)&time, &local))
		::FileTimeToSystemTime(&local, &st);
	return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}  {:<18} {}", st.wYear, st.wMonth, st.wDay, st.wHour,
		st.wMinute, st.wSecond, st.wMilliseconds, label, text);
}

void CHistoryView::CollectProcesses(std::vector<Line>& lines) {
	auto& pm = Frame()->GetProcessManager();
	for (auto& p : pm.GetNewProcesses()) {
		// there when the tab opened: not a start
		if (m_BaselineProcesses.erase(p->Key))
			continue;
		if (p->Id > 4 && !m_Processes.contains(p->Key))
			m_Processes[p->Key] = Process::OpenById(p->Id, p->Key.Created, ProcessAccessMask::QueryLimitedInformation | ProcessAccessMask::Synchronize);
		if (!IsEnabled(Event::ProcessStarted))
			continue;
		auto parent = pm.GetProcessNameById(p->ParentId);
		auto text = std::format(L"{} ({})  parent {} ({})", p->GetImageName(), p->Id, parent.empty() ? L"?" : parent, p->ParentId);
		if (auto& user = p->GetUser(); !user.empty())
			text += L"  " + user;
		if (auto& cmd = p->GetCommandLine(); !cmd.empty())
			text += L"  " + cmd;
		lines.push_back({ p->Key.Created, Event::ProcessStarted, false, p->Id, std::move(text), L"Process started" });
	}
	for (auto& p : pm.GetTerminatedProcesses()) {
		std::optional<uint32_t> code;
		if (auto it = m_Processes.find(p->Key); it != m_Processes.end()) {
			if (it->second)
				code = it->second->GetExitCode();
			m_Processes.erase(it);
		}
		m_BaselineProcesses.erase(p->Key);
		if (!IsEnabled(Event::ProcessExited))
			continue;
		auto now = Now();
		auto text = std::format(L"{} ({})  ran {}", p->GetImageName(), p->Id, Duration(now - p->Key.Created));
		bool warning = false;
		if (code) {
			text += std::format(L"  exit code {} (0x{:X})", (int32_t)*code, *code);
			warning = *code != 0;
		}
		lines.push_back({ now, Event::ProcessExited, warning, p->Id, std::move(text), L"Process exited" });
	}
}

void CHistoryView::CollectServices(std::vector<Line>& lines) {
	auto services = ServiceManager::EnumServices(ServiceEnumType::AllServices);
	if (services.empty())
		return;	// enumeration failed: keep what we have
	auto now = Now();
	std::unordered_set<std::wstring> current;
	current.reserve(services.size());
	for (auto& svc : services) {
		auto& name = svc.GetName();
		current.insert(name);
		auto& status = svc.GetStatusProcess();
		auto label = std::format(L"{} ({})", name, svc.GetDisplayName());
		auto it = m_Services.find(name);
		if (it == m_Services.end()) {
			if (IsEnabled(Event::ServiceInstalled))
				lines.push_back({ now, Event::ServiceInstalled, false, 0, label, L"Service installed" });
			m_Services[name] = { status.CurrentState, status.ProcessId, svc.GetDisplayName() };
			continue;
		}
		auto& known = it->second;
		auto state = status.CurrentState;
		// pending states are waited out: an event when a service gets to running, stopped or paused
		if (state == known.State || (state != ServiceState::Running && state != ServiceState::Stopped && state != ServiceState::Paused))
			continue;
		if (state == ServiceState::Running) {
			if (known.State == ServiceState::Paused) {
				if (IsEnabled(Event::ServicePaused))
					lines.push_back({ now, Event::ServicePaused, false, status.ProcessId, label, L"Service resumed" });
			}
			else if (IsEnabled(Event::ServiceStarted))
				lines.push_back({ now, Event::ServiceStarted, false, status.ProcessId, std::format(L"{}  PID {}", label, status.ProcessId), L"Service started" });
		}
		else if (state == ServiceState::Stopped) {
			if (IsEnabled(Event::ServiceStopped)) {
				auto text = label;
				bool failed = status.Win32ExitCode != 0 && status.Win32ExitCode != ERROR_SERVICE_NEVER_STARTED;
				if (failed)
					text += status.Win32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR
						? std::format(L"  failed: service error {} (0x{:X})", status.ServiceSpecificExitCode, status.ServiceSpecificExitCode)
						: L"  failed: " + Helpers::GetErrorText(status.Win32ExitCode);
				// the process it ran in, for Go to Process while that's still running
				lines.push_back({ now, Event::ServiceStopped, failed, known.Pid, std::move(text), failed ? L"Service failed" : L"Service stopped" });
			}
		}
		else if (IsEnabled(Event::ServicePaused))
			lines.push_back({ now, Event::ServicePaused, false, status.ProcessId, label, L"Service paused" });
		known.State = state;
		if (status.ProcessId)
			known.Pid = status.ProcessId;
	}
	// the ones that went
	for (auto it = m_Services.begin(); it != m_Services.end(); ) {
		if (current.contains(it->first)) {
			++it;
			continue;
		}
		if (IsEnabled(Event::ServiceInstalled))
			lines.push_back({ now, Event::ServiceInstalled, false, 0, std::format(L"{} ({})", it->first, it->second.DisplayName), L"Service deleted" });
		it = m_Services.erase(it);
	}
}

void CHistoryView::OnRefresh(bool /*active*/) {
	std::vector<Line> lines;
	CollectProcesses(lines);
	// services cost an SCM enumeration: only while one of their kinds is on (the baseline goes stale meanwhile)
	bool services = IsEnabled(Event::ServiceStarted) || IsEnabled(Event::ServiceStopped) || IsEnabled(Event::ServicePaused)
		|| IsEnabled(Event::ServiceInstalled);
	if (services)
		CollectServices(lines);

	if (!lines.empty()) {
		std::ranges::stable_sort(lines, {}, &Line::Time);
		std::vector<std::wstring> texts;
		std::vector<LGVLINE> entries;
		texts.reserve(lines.size());
		for (auto& line : lines)
			texts.push_back(Format(line.Time, line.Label, line.Text));
		for (size_t i = 0; i < lines.size(); i++) {
			LGVLINE entry;
			entry.pszText = texts[i].c_str();
			entry.level = lines[i].Warning ? LGVL_WARNING : LGVL_INFO;
			entry.style = BYTE((int)lines[i].Kind + 1);
			entry.lParam = lines[i].Pid;
			entries.push_back(entry);
		}
		m_Log.AppendLines(entries.data(), (int)entries.size());
	}
	UpdateStatus();
}

void CHistoryView::OnActivate(bool active) {
	m_Active = active;
	if (active) {
		UpdateStatus();
		m_Log.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CHistoryView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring kinds;
	for (int i = 0; i < (int)Event::_Count; i++)
		if (IsEnabled((Event)i))
			kinds += (kinds.empty() ? L"" : L", ") + std::wstring(EventName((Event)i));
	auto text = std::format(L"{} events since {}; tracking: {}", FormatHelper::FormatNumber((int64_t)m_Log.GetLineCount()),
		FormatHelper::FormatDateTime(m_Since), kinds.empty() ? L"nothing (right-click to choose)" : kinds);
	Frame()->SetStatusText(0, text.c_str());
}

uint32_t CHistoryView::GetLinePid(size_t line) const {
	LGVLINEINFO info;
	if (line == LGV_NOLINE || !m_Log.GetLineInfo(line, &info))
		return 0;
	return (uint32_t)info.lParam;
}

LRESULT CHistoryView::OnLogContextMenu(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto nm = reinterpret_cast<NMLGVLINE*>(hdr);
	m_MenuLine = nm->line;
	auto pid = GetLinePid(m_MenuLine);
	bool running = pid && Frame()->GetProcessManager().GetProcessById(pid) != nullptr;

	CMenu menu, events;
	menu.CreatePopupMenu();
	events.CreatePopupMenu();
	for (int i = 0; i < (int)Event::_Count; i++)
		events.AppendMenu(MF_STRING | (IsEnabled((Event)i) ? MF_CHECKED : 0), ID_HISTORY_EVENT_FIRST + i, EventName((Event)i));
	menu.AppendMenu(MF_STRING | (running ? 0 : MF_GRAYED), ID_HISTORY_GOTOPROCESS, L"Go to &Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, ID_HISTORY_SELECTALL, L"Select &All\tCtrl+A");
	menu.AppendMenu(MF_STRING | (m_Log.GetLineCount() ? 0 : MF_GRAYED), ID_FILE_EXPORT, L"&Save Log...\tCtrl+S");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_POPUP, (UINT_PTR)events.Detach(), L"&Events");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_HISTORY_CLEAR, L"C&lear");
	// commands land in the frame and are routed to this (the active) view
	Frame()->ShowContextMenu(menu, nm->pt, TPM_RIGHTBUTTON);
	return 0;
}

LRESULT CHistoryView::OnLogActivate(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto nm = reinterpret_cast<NMLGVLINE*>(hdr);
	if (auto pid = GetLinePid(nm->line); pid && Frame()->GetProcessManager().GetProcessById(pid))
		Frame()->GoToProcess(pid);
	return 0;
}

LRESULT CHistoryView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto pid = GetLinePid(m_MenuLine))
		Frame()->GoToProcess(pid);
	return 0;
}

bool CHistoryView::CanExport() const {
	return m_Log.GetLineCount() > 0;
}

LRESULT CHistoryView::OnSaveLog(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// read before the dialog: lines go on coming while it's open
	std::wstring text;
	size_t count = 0;
	for (size_t i = 0; i < m_Log.GetLineCount(); i++) {
		if (!m_Log.IsLineShown(i))
			continue;
		text += m_Log.GetLineText(i);
		text += L"\r\n";
		count++;
	}
	if (count == 0)
		return 0;

	SYSTEMTIME st;
	::GetLocalTime(&st);
	auto name = std::format(L"History {:04}-{:02}-{:02} {:02}{:02}{:02}.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	SuspendResumeHook sr;
	CSimpleFileDialog dlg(FALSE, L"log", name.c_str(), OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER,
		L"Log Files (*.log)\0*.log\0Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0", m_hWnd);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	std::wstring path = dlg.m_szFileName;
	if (!ListExport::Save(path, text)) {
		AtlMessageBox(m_hWnd, std::format(L"Failed to write {}: {}", path, Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	Frame()->SetStatusText(0, std::format(L"{} lines saved to {}{}", FormatHelper::FormatNumber((int64_t)count), path,
		m_Log.IsFiltered() ? L" (the lines the filter shows)" : L"").c_str());
	return 0;
}

bool CHistoryView::CanCopy() const {
	// the selection, or the caret's line
	return m_Log.GetLineCount() > 0;
}

LRESULT CHistoryView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Log.Copy();
	return 0;
}

LRESULT CHistoryView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the log's search box, the second of its toolbar's edit boxes (the frame's Ctrl+F comes here before the log
	// sees the key)
	auto filter = ::FindWindowEx(m_Log, nullptr, L"Edit", nullptr);
	auto search = filter ? ::FindWindowEx(m_Log, filter, L"Edit", nullptr) : nullptr;
	if (search) {
		::SetFocus(search);
		::SendMessage(search, EM_SETSEL, 0, -1);
	}
	return 0;
}

LRESULT CHistoryView::OnSelectAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Log.SelectAll();
	return 0;
}

LRESULT CHistoryView::OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Log.Clear();
	m_Since = Now();
	UpdateStatus();
	return 0;
}

LRESULT CHistoryView::OnToggleEvent(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto bit = 1u << (id - ID_HISTORY_EVENT_FIRST);
	m_Events ^= bit;
	auto& settings = AppSettings::Get();
	settings.HistoryEvents((int)m_Events);
	settings.Save();
	// a service kind turned on after they all were off: start from the services as they are now
	if (m_Events & bit) {
		auto enumerated = ServiceManager::EnumServices(ServiceEnumType::AllServices);
		if (!enumerated.empty()) {
			m_Services.clear();
			for (auto& svc : enumerated) {
				auto& status = svc.GetStatusProcess();
				m_Services[svc.GetName()] = { status.CurrentState, status.ProcessId, svc.GetDisplayName() };
			}
		}
	}
	UpdateStatus();
	return 0;
}
