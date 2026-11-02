#include "pch.h"
#include "HistoryView.h"
#include "Recorders.h"
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
}

CString CHistoryView::GetTitle() const {
	return L"History";
}

LRESULT CHistoryView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Markers) == (int)Event::_Count);
	m_hWndClient = m_Log.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Log.ShowToolbar(true);
	m_Log.ShowLineNumbers(false);
	m_Log.SetLineLimit(HistoryRecorder::LineLimit);
	for (int i = 0; i < (int)Event::_Count; i++) {
		LGVSTYLE style;
		style.crMarker = s_Markers[i];
		m_Log.SetStyle(BYTE(i + 1), &style);
	}
	// with Record from Startup, the frame's recorder, with what happened since the app started
	m_Recorder = Recorders::AcquireHistory(Frame()->GetProcessManager());
	AppendNewLines();
	return 0;
}

void CHistoryView::AppendNewLines() {
	auto& lines = m_Recorder->GetLines();
	auto first = m_Recorder->GetFirstSequence();
	// lines dropped from the recorder before they were shown are gone (it keeps as many as the log does)
	if (m_Shown < first)
		m_Shown = first;
	auto end = m_Recorder->GetEndSequence();
	if (m_Shown >= end)
		return;
	std::vector<std::wstring> texts;
	std::vector<LGVLINE> entries;
	texts.reserve(size_t(end - m_Shown));
	entries.reserve(size_t(end - m_Shown));
	for (auto seq = m_Shown; seq < end; seq++)
		texts.push_back(HistoryRecorder::Format(lines[size_t(seq - first)]));
	for (auto seq = m_Shown; seq < end; seq++) {
		auto& line = lines[size_t(seq - first)];
		LGVLINE entry;
		entry.pszText = texts[size_t(seq - m_Shown)].c_str();
		entry.level = line.Warning ? LGVL_WARNING : LGVL_INFO;
		entry.style = BYTE((int)line.Kind + 1);
		entry.lParam = line.Pid;
		entries.push_back(entry);
	}
	m_Log.AppendLines(entries.data(), (int)entries.size());
	m_Shown = end;
}

void CHistoryView::OnRefresh(bool /*active*/) {
	// the frame updated the recorder before the views (Recorders::OnRefresh)
	AppendNewLines();
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
		if (m_Recorder->IsEnabled((Event)i))
			kinds += (kinds.empty() ? L"" : L", ") + std::wstring(HistoryRecorder::EventName((Event)i));
	auto text = std::format(L"{} events since {}; tracking: {}", FormatHelper::FormatNumber((int64_t)m_Log.GetLineCount()),
		FormatHelper::FormatDateTime(m_Recorder->GetSince()), kinds.empty() ? L"nothing (right-click to choose)" : kinds);
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
		events.AppendMenu(MF_STRING | (m_Recorder->IsEnabled((Event)i) ? MF_CHECKED : 0), ID_HISTORY_EVENT_FIRST + i, HistoryRecorder::EventName((Event)i));
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
	m_Recorder->Clear();
	m_Shown = m_Recorder->GetEndSequence();
	UpdateStatus();
	return 0;
}

LRESULT CHistoryView::OnToggleEvent(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto events = m_Recorder->GetEvents() ^ (1u << (id - ID_HISTORY_EVENT_FIRST));
	m_Recorder->SetEvents(events);
	auto& settings = AppSettings::Get();
	settings.HistoryEvents((int)events);
	settings.Save();
	UpdateStatus();
	return 0;
}
