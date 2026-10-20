#pragma once

#include <ResizablePropertySheet.h>
#include <IconPropertySheet.h>
#include "ProcessPages.h"

//
// A process's properties: a modeless, resizable property sheet with tab icons, run on a thread of its own (see
// ShowProcessProperties), so a slow page doesn't hold up the main window and the main window's refreshes don't hold
// up the sheet. It has no owner, and a taskbar button of its own. The Job page is added only for a process in a job,
// the Services page only for a process hosting services.
//
class CProcessSheet :
	public CIconPropertySheetImpl<CProcessSheet, CResizablePropertySheetImpl<CProcessSheet>> {
	using BaseSheet = CIconPropertySheetImpl<CProcessSheet, CResizablePropertySheetImpl<CProcessSheet>>;
public:
	explicit CProcessSheet(ProcessProperties props);

	void OnSheetInitialized();

	BEGIN_MSG_MAP(CProcessSheet)
		CHAIN_MSG_MAP(BaseSheet)
	END_MSG_MAP()

private:
	ProcessProperties m_Props;
	std::wstring m_Title;
	CIcon m_Icon, m_SmallIcon;	// the process's, or the system's application icon
	std::unique_ptr<CProcGeneralPage> m_General;
	std::unique_ptr<CProcEnvironmentPage> m_Environment;
	std::unique_ptr<CProcTokenPage> m_Token;
	std::vector<std::unique_ptr<CProcPlaceholderPage>> m_Pages;
};

// opens the sheet on a new thread, which ends when the sheet is closed
void ShowProcessProperties(ProcessProperties props);
// closes the open sheets and waits (a while) for their threads, before the app exits: a sheet left open while the
// app shut down lost its window under its message loop
void CloseProcessProperties();
