#pragma once

#include <NativeCustomTabView.h>
#include <span>
#include "ThreadsPage.h"
#include "ModulesPage.h"
#include "HandlesPage.h"

struct IMainFrame;

//
// The lower pane of the Processes view: tabs with the handles, modules and threads of the selected process.
// SetProcess tells the pane which process to show, Refresh is called on every refresh tick while the pane is shown.
// The pages are CHandlesPage, CModulesPage and CThreadsPage. Each is refreshed only while its tab is active (and when
// it's activated): Handles and Modules enumerate, and the threads' handles are queried.
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
	// the pages' lists
	void SetListFont(HFONT font);
	// the frame's image list again (the tabs' and the Handles page's), after it took a new DPI's size
	void ResetImageList();
	// the pages' column layouts, saved in the settings
	void SaveColumns() const;

	// CNativeCustomTabViewImpl
	void OnPageActivated(int page);

	// for the Kill command, which acts on the page with the focus
	CThreadsPage& GetThreadsPage() { return m_Threads; }
	CHandlesPage& GetHandlesPage() { return m_Handles; }
	CModulesPage& GetModulesPage() { return m_Modules; }

	// no message map of its own: the base's has an ALT_MSG_MAP(1) section that it routes messages to

private:
	IMainFrame* m_Frame{ nullptr };
	enum Page { HandlesPage, ModulesPage, ThreadsPage };

	CHandlesPage m_Handles;
	CModulesPage m_Modules;
	CThreadsPage m_Threads;
	std::shared_ptr<ProcessInfoEx> m_Process;
};
