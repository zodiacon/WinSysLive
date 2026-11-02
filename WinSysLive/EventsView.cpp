#include "pch.h"
#include "resource.h"
#include "EventsView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <ListViewhelper.h>
#include <DpiHelper.h>
#include <ClipboardHelper.h>
#include <ToolbarHelper.h>
#include <WTLHelper.h>
#include <ranges>
#include "EventDetailsDlg.h"
#include "AlertRules.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

namespace {
	// in CEventsView::Column order; the keys are stored in the EventColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Time", L"Time", 150, LVCFMT_LEFT, true, true },
		{ L"Level", L"Level", 80, LVCFMT_LEFT, true },
		{ L"Log", L"Log", 90, LVCFMT_LEFT, true },
		{ L"Source", L"Source", 220, LVCFMT_LEFT, true },
		{ L"Event ID", L"EventId", 70, LVCFMT_RIGHT, true },
		{ L"Process", L"Process", 130, LVCFMT_LEFT, true },
		{ L"PID", L"ProcessId", 60, LVCFMT_RIGHT, true },
		{ L"Message", L"Message", 380, LVCFMT_LEFT, true },
		{ L"Details", L"Details", 600, LVCFMT_LEFT, true, false, nullptr, 1 },	// added: shown once in older layouts
	};

	const ToolBarButtonInfo s_BarButtons[] = {
		{ ID_EVENTS_WARNINGS, IDI_WARNING, BTNS_CHECK | BTNS_AUTOSIZE, L"Warnings" },	// the system's (IconId)
		{ ID_EVENTS_INFORMATION, IDI_INFORMATION, BTNS_CHECK | BTNS_AUTOSIZE, L"Information" },
		{ ID_EVENTS_LOGS, IDI_EVENT, BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE, L"Logs" },
		{ ID_EVENTS_SPAN, IDI_TIMER, BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE, L"Last 24 Hours" },
		{ ID_EVENTS_REFRESH, IDI_REFRESH, BTNS_BUTTON | BTNS_AUTOSIZE, L"Refresh" },
	};
	const int BarImageSize = 16;	// at 96 DPI

	// how severe, for sorting: critical, error, audit failure, warning, information, audit success, verbose
	int Severity(EventLogEntry const& e) {
		if (e.IsAuditFailure())
			return 25;
		if (e.IsAuditSuccess())
			return 45;
		return (e.Level == 0 ? 4 : e.Level) * 10;
	}

	// the Logs drop-down's choices (those that exist on the system are listed)
	const PCWSTR s_KnownLogs[] = {
		L"System", L"Application", L"Security", L"Setup", L"Windows PowerShell", L"Microsoft-Windows-PowerShell/Operational",
		L"Microsoft-Windows-Windows Defender/Operational", L"Microsoft-Windows-TaskScheduler/Operational",
		L"Microsoft-Windows-Sysmon/Operational",
	};
	const int s_Spans[] = { 1, 24, 24 * 7, 24 * 30 };	// hours

	bool IsError(EventLogEntry const& e) {
		return !e.IsAuditFailure() && !e.IsAuditSuccess() && (e.Level == 1 || e.Level == 2);
	}

	std::wstring FirstLine(std::wstring const& text) {
		auto end = text.find_first_of(L"\r\n");
		return end == std::wstring::npos ? text : text.substr(0, end);
	}
}

CString CEventsView::GetTitle() const {
	return L"Events";
}

std::wstring CEventsView::KeyOf(EventLogEntry const& e) {
	return e.Channel + L"|" + std::to_wstring(e.RecordId);
}

LRESULT CEventsView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	auto& settings = AppSettings::Get();
	SetColumns(ColumnLayout::Parse(settings.EventColumns(), s_Columns));
	m_Warnings = settings.EventsWarnings() != 0;
	m_Information = settings.EventsInformation() != 0;
	// the logs: the setting, else System and Application, and Security unless the older setting turned it off
	std::wstring logs = settings.EventsLogs();
	for (auto part : std::views::split(std::wstring_view(logs), L'|'))
		if (!part.empty())
			m_Logs.emplace_back(part.begin(), part.end());
	if (m_Logs.empty()) {
		m_Logs = { L"System", L"Application" };
		if (settings.EventsSecurity())
			m_Logs.push_back(L"Security");
	}
	m_Hours = std::ranges::find(s_Spans, settings.EventsHours()) != std::end(s_Spans) ? settings.EventsHours() : 24;

	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 320, L"Filter: source, message, details, log or process; id:4625 or id:4720-4767 (Ctrl+F)", m_List, [this](auto&) {
		ShowRows();
		UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	m_Bar = ToolbarHelper::CreateAndInitToolBar(m_hWndToolBar, s_BarButtons, _countof(s_BarButtons), BarImageSize);
	// the drop-downs' arrows; their menus come with TBN_DROPDOWN (through the rebar)
	m_Bar.SetExtendedStyle(m_Bar.GetExtendedStyle() | TBSTYLE_EX_DRAWDDARROWS);
	TBBUTTONINFO span{ sizeof(span), TBIF_TEXT };
	auto spanText = SpanText();
	span.pszText = spanText.data();
	m_Bar.SetButtonInfo(ID_EVENTS_SPAN, &span);
	m_BarFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_Bar)));
	m_Bar.SetFont(m_BarFont);
	m_Bar.AutoSize();
	AddSimpleReBarBand(m_Bar, nullptr, FALSE);
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_STYLE | RBBIM_CHILD };
		rebar.GetBandInfo(i, &band);
		band.fStyle |= RBBS_NOGRIPPER;
		band.fMask = RBBIM_STYLE;
		if (band.hwndChild == m_Filter.GetHwnd()) {
			band.fMask |= RBBIM_CHILDSIZE;
			band.cxMinChild = m_Filter.GetWidth();
		}
		rebar.SetBandInfo(i, &band);
	}
	UpdateBar();
	StartWatching();
	StartRead();
	return 0;
}

LRESULT CEventsView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// waits for a callback in progress; none comes after
	m_Watcher.Stop();
	if (m_Worker.joinable())
		m_Worker.join();
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

void CEventsView::UpdateLayout(BOOL resizeBars) {
	CViewBase::UpdateLayout(resizeBars);
	if (m_Bar) {
		CReBarCtrl rebar(m_hWndToolBar);
		rebar.MaximizeBand(rebar.GetBandCount() - 1, FALSE);
	}
}

void CEventsView::LayoutBar() {
	if (!m_Bar)
		return;
	ToolbarHelper::UpdateToolBarImages(m_Bar, s_BarButtons, _countof(s_BarButtons), BarImageSize);
	if (m_BarFont)
		m_BarFont.DeleteObject();
	m_BarFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_Bar)));
	m_Bar.SetFont(m_BarFont);
	m_Bar.AutoSize();
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_CHILD };
		if (rebar.GetBandInfo(i, &band) && band.hwndChild == m_Bar) {
			SIZE size{};
			m_Bar.GetMaxSize(&size);
			band.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
			band.cxMinChild = band.cxIdeal = size.cx;
			band.cyMinChild = size.cy;
			rebar.SetBandInfo(i, &band);
			break;
		}
	}
	UpdateLayout();
}

void CEventsView::UpdateBar() {
	if (!m_Bar)
		return;
	m_Bar.CheckButton(ID_EVENTS_WARNINGS, m_Warnings);
	m_Bar.CheckButton(ID_EVENTS_INFORMATION, m_Information);
	m_Bar.EnableButton(ID_EVENTS_REFRESH, !m_Reading);
}

void CEventsView::OnActivate(bool active) {
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		UpdateStatus();
		m_List.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CEventsView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

uint8_t CEventsView::MaxLevel() const {
	return EventFilter::MaxLevel(m_Warnings, m_Information);
}

std::vector<std::wstring> CEventsView::Channels() const {
	return m_Logs;
}

std::wstring CEventsView::SpanText() const {
	switch (m_Hours) {
		case 1: return L"Last Hour";
		case 24 * 7: return L"Last 7 Days";
		case 24 * 30: return L"Last 30 Days";
	}
	return std::format(L"Last {} Hours", m_Hours);
}

void CEventsView::LogsChanged() {
	auto& settings = AppSettings::Get();
	std::wstring logs;
	for (auto& log : m_Logs)
		logs += (logs.empty() ? L"" : L"|") + log;
	settings.EventsLogs(logs);
	settings.EventsHours(m_Hours);
	settings.Save();
	TBBUTTONINFO span{ sizeof(span), TBIF_TEXT };
	auto spanText = SpanText();
	span.pszText = spanText.data();
	m_Bar.SetButtonInfo(ID_EVENTS_SPAN, &span);
	LayoutBar();
	m_Watcher.Stop();
	StartWatching();
	StartRead();
}

LRESULT CEventsView::OnBarDropDown(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	auto tb = reinterpret_cast<NMTOOLBAR*>(hdr);
	if (hdr->hwndFrom != m_Bar || (tb->iItem != ID_EVENTS_LOGS && tb->iItem != ID_EVENTS_SPAN)) {
		bHandled = FALSE;
		return 0;
	}
	// the choices are the menu's own IDs, returned (TPM_RETURNCMD), not commands
	CMenu menu;
	menu.CreatePopupMenu();
	std::vector<std::wstring> logs;
	if (tb->iItem == ID_EVENTS_LOGS) {
		for (auto log : s_KnownLogs)
			if (std::ranges::find(m_Logs, log) != m_Logs.end() || EventLog::Exists(log))
				logs.push_back(log);
		// a chosen log that's not a known one stays listed
		for (auto& log : m_Logs)
			if (std::ranges::find(logs, log) == logs.end())
				logs.push_back(log);
		for (int i = 0; i < (int)logs.size(); i++)
			menu.AppendMenu(MF_STRING | (std::ranges::find(m_Logs, logs[i]) != m_Logs.end() ? MF_CHECKED : 0), i + 1, logs[i].c_str());
	}
	else {
		for (int i = 0; i < _countof(s_Spans); i++) {
			auto saved = m_Hours;
			m_Hours = s_Spans[i];
			menu.AppendMenu(MF_STRING | (saved == s_Spans[i] ? MF_CHECKED : 0), i + 1, SpanText().c_str());
			m_Hours = saved;
		}
	}
	m_Bar.PressButton(tb->iItem, TRUE);
	int choice = Frame()->ShowContextMenu(menu, ToolbarHelper::GetDropdownMenuPoint(m_Bar, tb->iItem), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD);
	m_Bar.PressButton(tb->iItem, FALSE);
	if (choice <= 0)
		return TBDDRET_DEFAULT;
	if (tb->iItem == ID_EVENTS_LOGS) {
		auto& log = logs[choice - 1];
		if (auto it = std::ranges::find(m_Logs, log); it != m_Logs.end()) {
			// at least one log
			if (m_Logs.size() == 1)
				return TBDDRET_DEFAULT;
			m_Logs.erase(it);
		}
		else
			m_Logs.push_back(log);
	}
	else
		m_Hours = s_Spans[choice - 1];
	LogsChanged();
	return TBDDRET_DEFAULT;
}

LRESULT CEventsView::OnAlertLikeThis(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto e = GetSelected();
	if (!e)
		return 0;
	// the event's log, source and ID; a Security audit failure as the audit failure kind (the keyword told)
	AlertRule rule;
	rule.Kind = e->IsAuditFailure() ? AlertKind::AuditFailure : AlertKind::EventLogged;
	rule.Log = e->Channel;
	rule.Pattern = e->Provider;
	rule.EventId = e->EventId;
	Frame()->AddAlertRule(rule);
	return 0;
}

LRESULT CEventsView::OnOpenViewer(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// Event Viewer at the event's log
	if (auto e = GetSelected())
		::ShellExecute(m_hWnd, nullptr, L"eventvwr.exe", std::format(L"/c:\"{}\"", e->Channel).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

bool CEventsView::Included(EventLogEntry const& e) const {
	return EventFilter::IsIncluded(e, m_Warnings, m_Information);
}

void CEventsView::StartWatching() {
	// a log that can't be watched (Security without elevation) leaves the others watched
	m_Watcher.Start(Channels(), MaxLevel(), *this, true);
}

void CEventsView::StartRead() {
	if (m_Reading)
		return;
	m_Reading = true;
	UpdateBar();
	UpdateStatus();
	m_Worker = std::jthread([this, hWnd = m_hWnd, maxLevel = MaxLevel(), channels = Channels(), hours = (uint32_t)m_Hours] {
		std::vector<EventLogEntry> all;
		std::vector<std::pair<std::wstring, DWORD>> failed;
		for (auto& channel : channels) {
			// a loop, not a range insert: that warns C4530 in Release, which has no exceptions (winsys/docs)
			// the data fields for the Details column: every Security event's, the others' without a message
			auto data = ::_wcsicmp(channel.c_str(), L"Security") == 0 ? EventDataRead::All : EventDataRead::NoMessage;
			if (auto events = EventLog::Query(channel, maxLevel, hours, MaxEvents, data)) {
				for (auto& e : *events) {
					Condense(e);
					all.push_back(std::move(e));
				}
			}
			else
				failed.push_back({ channel, ::GetLastError() });
		}
		// the logs merged, newest first
		std::ranges::stable_sort(all, [](auto& a, auto& b) { return a.Time > b.Time; });
		if (all.size() > MaxTotal)
			all.resize(MaxTotal);
		{
			std::lock_guard lock(m_Lock);
			m_Result = std::move(all);
			m_ResultFailed = std::move(failed);
		}
		::PostMessage(hWnd, WM_EVENTS_READ, 0, 0);
		});
}

LRESULT CEventsView::OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Reading = false;
	std::optional<std::vector<EventLogEntry>> result;
	{
		std::lock_guard lock(m_Lock);
		result = std::move(m_Result);
		m_Result.reset();
		m_Failed = std::move(m_ResultFailed);
	}
	if (result) {
		m_All = std::move(*result);
		m_Keys.clear();
		for (auto& e : m_All)
			m_Keys.insert(KeyOf(e));
		m_NewSinceRead = 0;
		ShowRows();
	}
	UpdateBar();
	UpdateStatus();
	return 0;
}

void CEventsView::GetSearchEntries(std::vector<SearchEntry>& entries) const {
	// every event read: its summary (else the message's line), the log, ID and source to match on
	for (auto& e : m_All) {
		auto& details = Details(e);
		auto name = details.empty() ? FirstLine(e.Message) : details;
		entries.push_back({ name.empty() ? e.Provider : name, L"Event",
			std::format(L"{} {}, {}", e.Channel, e.EventId, e.Provider), KeyOf(e), LevelImage(e) });
	}
}

bool CEventsView::ShowSearchEntry(std::wstring const& key) {
	auto it = std::ranges::find_if(m_All, [&](auto& e) { return KeyOf(e) == key; });
	if (it == m_All.end())
		return false;
	// a filter that hides it goes
	if (!Matches(*it)) {
		m_Filter.Clear();
		ShowRows();
	}
	auto row = std::ranges::find_if(m_Rows, [&](auto r) { return r == &*it; });
	if (row == m_Rows.end())
		return false;
	int index = (int)(row - m_Rows.begin());
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.EnsureVisible(index, FALSE);
	m_List.SetFocus();
	return true;
}

void CEventsView::Condense(EventLogEntry& e) {
	if (e.Data.empty())
		return;
	auto line = FirstLine(e.Message);
	std::wstring summary;
	if (EventAlertText::UsesData(e.Channel, !e.Message.empty()))
		summary = EventAlertText::Summarize(e.Channel, e.EventId, line, e.Data);
	e.Data.clear();
	// what the message's line doesn't say already ("<line> (fields)" keeps only the fields)
	if (!line.empty() && summary.starts_with(line))
		summary = summary.size() > line.size() + 2 ? summary.substr(line.size() + 2, summary.size() - line.size() - 3) : L"";
	if (!summary.empty() && summary != line)
		e.Data.push_back({ L"", std::move(summary) });
	e.Data.shrink_to_fit();
}

std::wstring const& CEventsView::Details(EventLogEntry const& e) {
	static const std::wstring empty;
	return e.Data.empty() ? empty : e.Data[0].Value;
}

void CEventsView::OnEvent(EventLogEntry&& entry) {
	Condense(entry);
	bool first;
	{
		std::lock_guard lock(m_Lock);
		first = m_Queue.empty();
		m_Queue.push_back(std::move(entry));
	}
	// one message for a burst
	if (first)
		PostMessage(WM_EVENTS_NEW);
}

LRESULT CEventsView::OnNewEvents(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	std::vector<EventLogEntry> queue;
	{
		std::lock_guard lock(m_Lock);
		queue.swap(m_Queue);
	}
	// the list's rows point into m_All, which grows: the selection is kept by key
	auto selected = GetSelected() ? KeyOf(*GetSelected()) : std::wstring();
	int added = 0;
	for (auto& e : queue) {
		if (!Included(e) || !m_Keys.insert(KeyOf(e)).second)
			continue;
		m_All.insert(m_All.begin(), std::move(e));
		added++;
	}
	if (m_All.size() > MaxTotal) {
		for (auto it = m_All.begin() + MaxTotal; it != m_All.end(); ++it)
			m_Keys.erase(KeyOf(*it));
		m_All.resize(MaxTotal);
	}
	if (added) {
		m_NewSinceRead += added;
		m_Rows.clear();	// it pointed into the old storage
		ShowRows();
		if (!selected.empty()) {
			auto it = std::ranges::find_if(m_Rows, [&](auto row) { return KeyOf(*row) == selected; });
			if (it != m_Rows.end())
				m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
		UpdateStatus();
	}
	return 0;
}

bool CEventsView::Matches(EventLogEntry const& e) const {
	// "id:" tokens choose IDs, the rest is text (EventFilter)
	if (!m_ParsedFilter.MatchesId(e.EventId))
		return false;
	auto& filter = m_ParsedFilter.Text;
	if (filter.empty())
		return true;
	return CFilterBox::Contains(e.Provider, filter) || CFilterBox::Contains(e.Message, filter) || CFilterBox::Contains(e.Channel, filter)
		|| CFilterBox::Contains(std::wstring(EventLog::LevelText(e)), filter) || CFilterBox::Contains(Details(e), filter)
		|| CFilterBox::Contains(std::to_wstring(e.EventId), filter) || CFilterBox::Contains(ProcessName(e), filter);
}

void CEventsView::ShowRows() {
	auto selected = GetSelected() ? KeyOf(*GetSelected()) : std::wstring();
	m_ParsedFilter = EventFilter::Parse(m_Filter.GetText());
	m_Rows.clear();
	for (auto& e : m_All)
		if (Matches(e))
			m_Rows.push_back(&e);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List))
		DoSort(si);
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (!selected.empty()) {
		auto it = std::ranges::find_if(m_Rows, [&](auto row) { return KeyOf(*row) == selected; });
		if (it != m_Rows.end())
			m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	m_List.Invalidate();
}

void CEventsView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (m_Reading && m_All.empty())
		text = L"Reading the event logs...";
	else {
		auto errors = std::ranges::count_if(m_All, IsError);
		auto warnings = std::ranges::count_if(m_All, [](auto& e) { return e.Level == 3 && !e.IsAuditFailure(); });
		auto failures = std::ranges::count_if(m_All, [](auto& e) { return e.IsAuditFailure(); });
		auto span = SpanText();
		::CharLowerBuff(span.data(), (DWORD)span.size());	// "last 7 days"
		text = std::format(L"{} events from {} logs in the {}: {} errors, {} warnings", m_All.size(), m_Logs.size(), span, errors, warnings);
		if (std::ranges::find(m_Logs, L"Security") != m_Logs.end())
			text += std::format(L", {} audit failures", failures);
		if (!m_Filter.GetText().empty())
			text += std::format(L"; {} match the filter", m_Rows.size());
		if (m_NewSinceRead)
			text += std::format(L"; {} new since read", m_NewSinceRead);
		text += m_Watcher.IsRunning() ? L"; new events shown as they're written" : L"; new events aren't watched (Refresh)";
		for (auto& [channel, error] : m_Failed)
			text += channel == L"Security" && !SecurityHelper::IsRunningElevated() ? L" (the Security log needs administrator rights)"
				: std::format(L" ({} can't be read: {})", channel, Helpers::GetErrorText(error));
	}
	Frame()->SetStatusText(0, text.c_str());
}

std::wstring CEventsView::ProcessName(EventLogEntry const& e) const {
	if (e.ProcessId == 0)
		return L"";
	if (e.ProcessId == 4)
		return L"System";
	auto p = Frame()->GetProcessManager().GetProcessById(e.ProcessId);
	return p && p->Key.Created <= e.Time ? p->GetImageName() : L"";
}

bool CEventsView::IsRunning(EventLogEntry const& e) const {
	auto p = e.ProcessId ? Frame()->GetProcessManager().GetProcessById(e.ProcessId) : nullptr;
	return p && p->Key.Created <= e.Time;
}

std::wstring CEventsView::GetText(EventLogEntry const& e, Column column) const {
	switch (column) {
		case Column::Time: return FormatHelper::FormatDateTime(e.Time);
		case Column::Level: return EventLog::LevelText(e);
		case Column::Log: return e.Channel;
		case Column::Source: return e.Provider;
		case Column::EventId: return std::to_wstring(e.EventId);
		case Column::Process: return ProcessName(e);
		case Column::ProcessId: return e.ProcessId ? std::to_wstring(e.ProcessId) : L"";
		case Column::Message: return FirstLine(e.Message);
		case Column::Details: return Details(e);
	}
	return L"";
}

CString CEventsView::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(*m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

int CEventsView::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || !ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	return LevelImage(*m_Rows[row]);
}

int CEventsView::LevelImage(EventLogEntry const& e) const {
	if (e.IsAuditFailure() || e.IsAuditSuccess())
		return Frame()->GetIconIndex(IDI_KEY);
	// the system's stock icons, as Event Viewer's
	if (e.Level == 1 || e.Level == 2)
		return Frame()->GetIconIndex(SystemIconId(IDI_ERROR));
	if (e.Level == 3)
		return Frame()->GetIconIndex(SystemIconId(IDI_WARNING));
	return Frame()->GetIconIndex(SystemIconId(IDI_INFORMATION));
}

int CEventsView::GetSaveColumnRange(HWND, int&) const {
	return -1;
}

void CEventsView::DoSort(SortInfo const* si) {
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](EventLogEntry const* a, EventLogEntry const* b) {
		int result;
		switch (column) {
			case Column::Time: result = compare(a->Time, b->Time); break;
			case Column::Level: result = compare(Severity(*a), Severity(*b)); break;
			case Column::EventId: result = compare(a->EventId, b->EventId); break;
			case Column::ProcessId: result = compare(a->ProcessId, b->ProcessId); break;
			default: result = ::_wcsicmp(GetText(*a, column).c_str(), GetText(*b, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
}

EventLogEntry const* CEventsView::GetSelected() const {
	int selected = m_List.GetSelectedIndex();
	return selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected] : nullptr;
}

LRESULT CEventsView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_List) {
		bHandled = FALSE;
		return 0;
	}
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto& e = *m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			if (e.IsAuditFailure()) {
				cd->clrTextBk = dark ? RGB(110, 65, 20) : RGB(255, 222, 185);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (IsError(e)) {
				cd->clrTextBk = dark ? RGB(110, 30, 30) : RGB(255, 205, 205);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (e.Level == 3 && !e.IsAuditSuccess()) {
				cd->clrTextBk = dark ? RGB(80, 70, 20) : RGB(255, 245, 200);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

bool CEventsView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_EVENT_DETAILS, L"&Details...");
	menu.AppendMenu(MF_STRING | (IsRunning(*m_Rows[row]) ? 0 : MF_GRAYED), ID_EVENT_GOTOPROCESS, L"&Go to Process");
	menu.AppendMenu(MF_STRING, ID_EVENT_OPENVIEWER, L"Open in &Event Viewer");
	menu.AppendMenu(MF_STRING, ID_EVENT_ALERT, L"&Alert on Events Like This...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy");
	menu.SetMenuDefaultItem(ID_EVENT_DETAILS);
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CEventsView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Rows.size())
		return false;
	BOOL handled;
	OnDetails(0, 0, nullptr, handled);
	return true;
}

LRESULT CEventsView::OnDetails(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto e = GetSelected();
	if (!e)
		return 0;
	CEventDetailsDlg::Callbacks callbacks;
	// the row delta rows away, selected; a copy (new events may arrive while the dialog is open, moving m_All)
	callbacks.Move = [this](EventLogEntry const& current, int delta) -> std::optional<EventLogEntry> {
		auto key = KeyOf(current);
		auto it = std::ranges::find_if(m_Rows, [&](auto row) { return KeyOf(*row) == key; });
		if (it == m_Rows.end())
			return std::nullopt;
		auto index = (int)(it - m_Rows.begin()) + delta;
		if (index < 0 || index >= (int)m_Rows.size())
			return std::nullopt;
		m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.EnsureVisible(index, FALSE);
		return *m_Rows[index];
	};
	callbacks.ProcessName = [this](EventLogEntry const& entry) { return ProcessName(entry); };
	callbacks.GoToProcess = [this](uint32_t pid) { Frame()->GoToProcess(pid); };
	CEventDetailsDlg dlg(*e, std::move(callbacks));
	dlg.DoModal(m_hWnd);
	return 0;
}

LRESULT CEventsView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto e = GetSelected(); e && IsRunning(*e))
		Frame()->GoToProcess(e->ProcessId);
	return 0;
}

LRESULT CEventsView::OnBarCommand(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	switch (id) {
		case ID_EVENTS_WARNINGS:
			m_Warnings = !m_Warnings;
			settings.EventsWarnings(m_Warnings);
			break;
		case ID_EVENTS_INFORMATION:
			m_Information = !m_Information;
			settings.EventsInformation(m_Information);
			break;
	}
	settings.Save();
	// the levels changed: the watcher's query and the read
	if (id != ID_EVENTS_REFRESH) {
		m_Watcher.Stop();
		StartWatching();
	}
	StartRead();
	UpdateBar();
	return 0;
}

LRESULT CEventsView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto e = GetSelected();
	if (!e)
		return 0;
	// the shown columns, the message whole
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += (Column)column.Id == Column::Message ? e->Message : GetText(*e, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CEventsView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CEventsView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CEventsView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CEventsView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CEventsView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.EventColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CEventsView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		ShowRows();
	}
	return 0;
}

LRESULT CEventsView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	auto column = ColumnLayout::HeaderColumnAt(m_List, GetColumnManager(m_List), pt);
	if (!column) {
		bHandled = FALSE;
		return 0;
	}
	m_MenuColumn = *column;
	CMenu menu;
	Frame()->ShowContextMenu(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), pt, TPM_RIGHTBUTTON);
	return 0;
}

LRESULT CEventsView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	ShowRows();
	return 0;
}

LRESULT CEventsView::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	ShowRows();
	return 0;
}
