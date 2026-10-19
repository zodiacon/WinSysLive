#include "pch.h"
#include "MainFrm.h"
#include "ProcessesView.h"
#include "ServicesView.h"
#include "PerfView.h"
#include "UsersView.h"

template<typename TView>
static std::pair<IView*, CMessageMap*> DoCreateView(TView* view, HWND hParent) {
	if (!view->DoCreate(hParent)) {
		delete view;
		return {};
	}
	return { view, view };
}

std::pair<IView*, CMessageMap*> CMainFrame::CreateView(TabType type) {
	switch (type) {
		case TabType::Processes: return DoCreateView(new CProcessesView(this), m_Tabs);
		case TabType::Services: return DoCreateView(new CServicesView(this), m_Tabs);
		case TabType::Performance: return DoCreateView(new CPerfView(this), m_Tabs);
		case TabType::Users: return DoCreateView(new CUsersView(this), m_Tabs);
	}
	ATLASSERT(false);
	return {};
}
