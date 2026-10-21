#include "pch.h"
#include "resource.h"
#include "ProcessInfoEx.h"
#include "Interfaces.h"
#include "ProcessDetails.h"

CProcessDetails::CProcessDetails() {
	m_bTabCloseButton = false;
	m_bDestroyImageList = false;	// the frame's
}

void CProcessDetails::Init(IMainFrame* frame) {
	m_Frame = frame;
	SetImageList(frame->GetImageList());

	m_Handles.SetFrame(frame);
	m_Handles.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Modules.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Threads.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	AddPage(m_Handles, L"Handles", frame->GetIconIndex(IDI_HANDLES));
	AddPage(m_Modules, L"Modules", frame->GetIconIndex(IDI_MODULES));
	AddPage(m_Threads, L"Threads", frame->GetIconIndex(IDI_THREADS));
	SetActivePage(0);
}

void CProcessDetails::SetProcess(std::shared_ptr<ProcessInfoEx> process) {
	// the selection is restored (to the same process) after every refresh
	if (process == m_Process)
		return;
	m_Process = std::move(process);
	m_Threads.SetProcess(m_Process);
	// the handles and the modules cost more: enumerated only while their tab is shown (and when it's activated)
	int page = GetActivePage();
	m_Modules.SetProcess(m_Process, page == ModulesPage);
	m_Handles.SetProcess(m_Process, page == HandlesPage);
}

ProcessInfoEx* CProcessDetails::GetProcess() const {
	return m_Process.get();
}

void CProcessDetails::SetListFont(HFONT font) {
	m_Handles.SetListFont(font);
	m_Modules.SetListFont(font);
	m_Threads.SetListFont(font);
}

void CProcessDetails::SaveColumns() const {
	m_Handles.SaveColumns();
	m_Modules.SaveColumns();
	m_Threads.SaveColumns();
}

void CProcessDetails::ResetImageList() {
	SetImageList(m_Frame->GetImageList());
	m_Handles.ResetImageList();
}

void CProcessDetails::Refresh() {
	// the threads too only while shown: each has its handle queried four times a refresh. The deltas cover the time
	// since the last refresh, so they're right when the tab is shown again
	switch (GetActivePage()) {
		case ModulesPage: m_Modules.Refresh(); break;
		case HandlesPage: m_Handles.Refresh(); break;
		case ThreadsPage: m_Threads.Refresh(); break;
	}
}

void CProcessDetails::OnPageActivated(int page) {
	CNativeCustomTabViewImpl<CProcessDetails>::OnPageActivated(page);
	// not refreshed while hidden (see Refresh)
	if (page == ModulesPage && m_Modules)
		m_Modules.Refresh();
	else if (page == HandlesPage && m_Handles)
		m_Handles.Refresh();
	else if (page == ThreadsPage && m_Threads)
		m_Threads.Refresh();
}
