#include "pch.h"
#include "resource.h"
#include "ProcessInfoEx.h"
#include "Interfaces.h"
#include "ProcessDetails.h"

CProcessDetails::CProcessDetails() {
	m_bTabCloseButton = false;
	m_bDestroyImageList = false;	// the frame's
}

CListViewCtrl CProcessDetails::CreateList(std::span<ColumnInfo const> columns) {
	CListViewCtrl list;
	list.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
		LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	for (int i = 0; i < (int)columns.size(); i++)
		list.InsertColumn(i, columns[i].Name, columns[i].Format, columns[i].Width);
	return list;
}

void CProcessDetails::Init(IMainFrame* frame) {
	m_Frame = frame;
	SetImageList(frame->GetImageList());

	static const ColumnInfo handles[] = {
		{ L"Handle", 80, LVCFMT_RIGHT },
		{ L"Type", 120, LVCFMT_LEFT },
		{ L"Name", 400, LVCFMT_LEFT },
		{ L"Address", 140, LVCFMT_RIGHT },	// of the kernel object
		{ L"Access", 100, LVCFMT_RIGHT },
		{ L"Decoded Access", 300, LVCFMT_LEFT },	// the access mask's rights, named for the object's type
		{ L"Attributes", 100, LVCFMT_LEFT },
	};
	static const ColumnInfo modules[] = {
		{ L"Name", 200, LVCFMT_LEFT },
		{ L"Base Address", 140, LVCFMT_RIGHT },
		{ L"Size", 100, LVCFMT_RIGHT },
		{ L"Path", 400, LVCFMT_LEFT },
	};
	m_Handles = CreateList(handles);
	m_Modules = CreateList(modules);
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
}

ProcessInfoEx* CProcessDetails::GetProcess() const {
	return m_Process.get();
}

void CProcessDetails::Refresh() {
	m_Threads.Refresh();
}
