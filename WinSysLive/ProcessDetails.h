#pragma once

#include <NativeCustomTabView.h>
#include <span>
#include "ThreadsPage.h"

struct IMainFrame;

//
// The lower pane of the Processes view: tabs with the handles, modules and threads of the selected process.
// SetProcess tells the pane which process to show, Refresh is called on every refresh tick while the pane is shown.
// Threads is a page of its own (CThreadsPage); Handles and Modules are still empty list views.
//
class CProcessDetails : public CNativeCustomTabViewImpl<CProcessDetails> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_ProcessDetails", 0, COLOR_APPWORKSPACE)

	CProcessDetails();

	// creates the pages; call after Create
	void Init(IMainFrame* frame);
	// nullptr: no process selected
	void SetProcess(std::shared_ptr<ProcessInfoEx> process);
	[[nodiscard]] ProcessInfoEx* GetProcess() const;
	void Refresh();

	// no message map of its own: the base's has an ALT_MSG_MAP(1) section that it routes messages to

private:
	struct ColumnInfo {
		PCWSTR Name;
		int Width;
		int Format;
	};
	CListViewCtrl CreateList(std::span<ColumnInfo const> columns);

	IMainFrame* m_Frame{ nullptr };
	CListViewCtrl m_Handles, m_Modules;
	CThreadsPage m_Threads;
	std::shared_ptr<ProcessInfoEx> m_Process;
};
