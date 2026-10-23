#include "pch.h"
#include "MainFrm.h"
#include "ProcessesView.h"
#include "ServicesView.h"
#include "PerfView.h"
#include "UsersView.h"
#include "NetworkView.h"
#include "KernelModulesView.h"
#include "IpcView.h"
#include "ZombiesView.h"
#include "ObjectTypesView.h"
#include "ObjectManagerView.h"
#include "PoolTagsView.h"
#include "DiskView.h"

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
		case TabType::Network: return DoCreateView(new CNetworkView(this), m_Tabs);
		case TabType::KernelModules: return DoCreateView(new CKernelModulesView(this), m_Tabs);
		case TabType::Drivers: return DoCreateView(new CServicesView(this, CServicesView::Kind::Drivers), m_Tabs);
		case TabType::Ipc: return DoCreateView(new CIpcView(this), m_Tabs);
		case TabType::Zombies: return DoCreateView(new CZombiesView(this), m_Tabs);
		case TabType::ObjectTypes: return DoCreateView(new CObjectTypesView(this), m_Tabs);
		case TabType::ObjectManager: return DoCreateView(new CObjectManagerView(this), m_Tabs);
		case TabType::PoolTags: return DoCreateView(new CPoolTagsView(this), m_Tabs);
		case TabType::Disk: return DoCreateView(new CDiskView(this), m_Tabs);
	}
	ATLASSERT(false);
	return {};
}
