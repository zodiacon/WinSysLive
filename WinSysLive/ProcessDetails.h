#pragma once

#include <NativeCustomTabView.h>
#include <span>
#include "ThreadsPage.h"
#include "ModulesPage.h"

struct IMainFrame;

//
// The lower pane of the Processes view: tabs with the handles, modules and threads of the selected process.
// SetProcess tells the pane which process to show, Refresh is called on every refresh tick while the pane is shown.
// Threads (CThreadsPage) and Modules (CModulesPage) are pages of their own; Handles is still an empty list view.
// Modules is enumerated only while it's the active tab (walking an address space costs more).
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

	// CNativeCustomTabViewImpl
	void OnPageActivated(int page);

	// no message map of its own: the base's has an ALT_MSG_MAP(1) section that it routes messages to

private:
	struct ColumnInfo {
		PCWSTR Name;
		int Width;
		int Format;
	};
	CListViewCtrl CreateList(std::span<ColumnInfo const> columns);

	IMainFrame* m_Frame{ nullptr };
	enum Page { HandlesPage, ModulesPage, ThreadsPage };

	CListViewCtrl m_Handles;
	CModulesPage m_Modules;
	CThreadsPage m_Threads;
	std::shared_ptr<ProcessInfoEx> m_Process;
};
