#include "pch.h"
#include <DpiHelper.h>
#include "resource.h"
#include "ServicesView.h"
#include "ServiceSheet.h"
#include "FormatHelper.h"
#include "AppSettings.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ClipboardHelper.h>
#include <SortHelper.h>
#include <unordered_set>

using namespace WinSys;

namespace {
	// highlighting lasts this many updates
	const int HighlightTicks = 2;
	// how long a restart waits for the service to stop
	const ULONGLONG RestartTimeout = 30000;

	// in CServicesView::Column order; the keys are stored in the ServiceColumns setting
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Name", L"Name", 180, LVCFMT_LEFT, true, true },
		{ L"Display Name", L"DisplayName", 260, LVCFMT_LEFT, true },
		{ L"State", L"State", 80, LVCFMT_LEFT, true, true },
		{ L"Start Type", L"StartType", 170, LVCFMT_LEFT, true },
		{ L"Type", L"Type", 110, LVCFMT_LEFT, true },
		{ L"PID", L"ProcessId", 60, LVCFMT_RIGHT, true },
		{ L"Process", L"ProcessName", 120, LVCFMT_LEFT, true },
		{ L"Account", L"Account", 160, LVCFMT_LEFT, true },
		{ L"Binary Path", L"BinaryPath", 350, LVCFMT_LEFT, true },
		{ L"Description", L"Description", 400, LVCFMT_LEFT, true },
		{ L"Error Control", L"ErrorControl", 90, LVCFMT_LEFT, false },
		{ L"Dependencies", L"Dependencies", 250, LVCFMT_LEFT, false },
		{ L"Required Privileges", L"Privileges", 350, LVCFMT_LEFT, false },
		{ L"Triggers", L"Triggers", 300, LVCFMT_LEFT, false },
	};
	static_assert(_countof(s_Columns) == 14);

	template<typename Range, typename Format>
	std::wstring Join(Range const& range, Format format) {
		std::wstring text;
		for (auto& value : range) {
			if (!text.empty())
				text += L", ";
			text += format(value);
		}
		return text;
	}
}

WinSys::ServiceConfiguration const* CServicesView::ServiceItem::GetConfig() {
	if (!m_ConfigLoaded) {
		m_ConfigLoaded = true;
		m_Config = ServiceManager::GetServiceConfiguration(Info.GetName());
	}
	return m_Config.get();
}

std::wstring const& CServicesView::ServiceItem::GetDescription() {
	if (!m_DescriptionLoaded) {
		m_DescriptionLoaded = true;
		m_Description = ServiceManager::GetServiceDescription(Info.GetName());
	}
	return m_Description;
}

std::wstring const& CServicesView::ServiceItem::GetPrivileges() {
	if (!m_PrivilegesLoaded) {
		m_PrivilegesLoaded = true;
		if (auto svc = Service::Open(Info.GetName(), ServiceAccessMask::QueryConfig))
			m_Privileges = Join(svc->GetRequiredPrivileges(), [](auto& p) { return p; });
	}
	return m_Privileges;
}

std::wstring const& CServicesView::ServiceItem::GetTriggers() {
	if (!m_TriggersLoaded) {
		m_TriggersLoaded = true;
		// only the type and action: ServiceTrigger::TriggerSubtype points into a buffer GetTriggers has freed
		if (auto svc = Service::Open(Info.GetName(), ServiceAccessMask::QueryConfig)) {
			// without the subtypes, triggers of a type look the same: "Start: Network endpoint (3)"
			std::vector<std::pair<std::wstring, int>> triggers;
			for (auto& t : svc->GetTriggers()) {
				auto text = FormatHelper::ServiceTriggerToString(t.Type, t.Action);
				if (auto it = std::ranges::find(triggers, text, &std::pair<std::wstring, int>::first); it != triggers.end())
					it->second++;
				else
					triggers.push_back({ std::move(text), 1 });
			}
			m_Triggers = Join(triggers, [](auto& t) { return t.second > 1 ? std::format(L"{} ({})", t.first, t.second) : t.first; });
		}
	}
	return m_Triggers;
}

void CServicesView::ServiceItem::Reload() {
	m_Config.reset();
	m_ConfigLoaded = m_DescriptionLoaded = m_PrivilegesLoaded = m_TriggersLoaded = false;
}

CString CServicesView::GetTitle() const {
	return L"Services";
}

LRESULT CServicesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the filter box goes in a rebar of the view's own, above the list
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);

	static_assert(_countof(s_Columns) == (int)Column::_Count);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().ServiceColumns(), s_Columns));

	int width = 360;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, width, L"Filter: name, description, process or path (Ctrl+F)", m_List,
		[this](auto&) {
			ShowItems();
			UpdateFilterStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	for (auto& svc : ServiceManager::EnumServices(ServiceEnumType::AllServices)) {
		auto item = std::make_shared<ServiceItem>(svc);
		m_ItemsByName.insert({ svc.GetName(), item });
		m_All.push_back(item);
		m_Items.push_back(std::move(item));
	}
	m_List.SetItemCount((int)m_Items.size());

	return 0;
}

LRESULT CServicesView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

void CServicesView::OnActivate(bool active) {
	// the Processes view handles it too
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	if (active && m_ShowPending)
		ShowItems();
	// the status line's count is this view's
	if (!m_Filter.GetText().empty())
		active ? UpdateFilterStatus() : Frame()->SetStatusText(0, L"");
	if (active)
		m_List.SetFocus();
}

std::vector<ColumnLayout::ShownColumn> CServicesView::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CServicesView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CServicesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ServiceColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CServicesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
	}
	return 0;
}

LRESULT CServicesView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	// the header's NM_RCLICK doesn't get here, but its WM_CONTEXTMENU does; the rows have their own menu (OnRightClickList)
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

LRESULT CServicesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;

	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

void CServicesView::OnFontChanged(HFONT font) {
	m_List.SetFont(font);
	m_Filter.SetFont(font);
}

void CServicesView::OnRefresh(bool active) {
	auto services = ServiceManager::EnumServices(ServiceEnumType::AllServices);
	if (services.empty())
		return;	// keep what we have if enumeration failed

	auto selected = GetSelectedService();
	auto selectedName = selected ? selected->Info.GetName() : L"";

	bool added = false;
	std::unordered_set<std::wstring> current;
	current.reserve(services.size());
	for (auto& svc : services) {
		current.insert(svc.GetName());
		if (auto it = m_ItemsByName.find(svc.GetName()); it != m_ItemsByName.end()) {
			auto& item = *it->second;
			auto& before = item.Info.GetStatusProcess();
			auto& now = svc.GetStatusProcess();
			if (before.CurrentState != now.CurrentState || before.ProcessId != now.ProcessId)
				item.ChangedTicks = HighlightTicks;
			else if (item.ChangedTicks > 0)
				item.ChangedTicks--;
			if (item.NewTicks > 0)
				item.NewTicks--;
			item.Info = std::move(svc);
		}
		else {
			auto item = std::make_shared<ServiceItem>(std::move(svc));
			item->NewTicks = HighlightTicks;
			m_ItemsByName.insert({ item->Info.GetName(), item });
			m_All.push_back(std::move(item));
			added = true;
		}
	}

	// services that were deleted
	bool removed = std::erase_if(m_All, [&](auto& item) { return !current.contains(item->Info.GetName()); }) > 0;
	if (removed)
		std::erase_if(m_ItemsByName, [&](auto& pair) { return !current.contains(pair.first); });

	// the rows of a hidden list are chosen and sorted when it's shown
	if (!active) {
		m_ShowPending = true;
		return;
	}
	// with a filter, a service's process may have changed whether it matches
	if (added || removed || !m_Filter.GetText().empty()) {
		ShowItems();
		if (active)
			UpdateFilterStatus();
	}
	else if (GetSortInfo(m_List))
		// states change, so the order may too; DoSort keeps the selection
		Sort(m_List);

	if (active)
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

bool CServicesView::Matches(ServiceItem& item) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	// the description and the binary path are loaded once (a call to the SCM each) and cached
	for (auto column : { Column::Name, Column::DisplayName, Column::ProcessId, Column::ProcessName, Column::Description, Column::BinaryPath })
		if (CFilterBox::Contains(GetText(item, column), filter))
			return true;
	return false;
}

void CServicesView::ShowItems() {
	m_ShowPending = false;
	auto selected = GetSelectedService();
	auto selectedName = selected ? selected->Info.GetName() : L"";
	m_Items.clear();
	for (auto& item : m_All)
		if (Matches(*item))
			m_Items.push_back(item);
	m_List.SetItemCountEx((int)m_Items.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	// DoSort keeps the selection
	if (GetSortInfo(m_List))
		Sort(m_List);
	else
		SelectService(selectedName);
	m_List.Invalidate();
}

void CServicesView::UpdateFilterStatus() {
	if (m_Filter.GetText().empty())
		Frame()->SetStatusText(0, L"");
	else
		Frame()->SetStatusText(0, std::format(L"{} of {} services match the filter", m_Items.size(), m_All.size()).c_str());
}

LRESULT CServicesView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CServicesView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

CString CServicesView::GetColumnText(HWND, int row, int col) const {
	if (row < 0 || row >= (int)m_Items.size())
		return L"";
	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(col);
	return GetText(*m_Items[row], column).c_str();
}

std::wstring CServicesView::GetText(ServiceItem& item, Column column) const {
	auto& status = item.Info.GetStatusProcess();
	switch (column) {
		case Column::Name: return item.Info.GetName();
		case Column::DisplayName: return item.Info.GetDisplayName();
		case Column::State: return FormatHelper::ServiceStateToString(status.CurrentState);
		case Column::StartType:
			if (auto config = item.GetConfig(); config)
				return FormatHelper::ServiceStartTypeToString(*config);
			return L"";
		case Column::Type: return FormatHelper::ServiceTypeToString(status.Type);
		case Column::ProcessId: return status.ProcessId ? std::to_wstring(status.ProcessId) : L"";
		case Column::ProcessName: return Frame()->GetProcessManager().GetProcessNameById(status.ProcessId);
		case Column::Account:
			if (auto config = item.GetConfig(); config)
				return config->AccountName;
			return L"";
		case Column::BinaryPath:
			if (auto config = item.GetConfig(); config)
				return config->BinaryPathName;
			return L"";
		case Column::Description: return item.GetDescription();
		case Column::ErrorControl:
			if (auto config = item.GetConfig(); config)
				return FormatHelper::ServiceErrorControlToString(config->ErrorControl);
			return L"";
		case Column::Dependencies:
			if (auto config = item.GetConfig(); config)
				return Join(config->Dependencies, [](auto& d) { return d; });
			return L"";
		case Column::Privileges: return item.GetPrivileges();
		case Column::Triggers: return item.GetTriggers();
	}
	return L"";
}

int CServicesView::GetRowImage(HWND, int row, int) const {
	if (row < 0 || row >= (int)m_Items.size())
		return -1;
	switch (m_Items[row]->Info.GetStatusProcess().CurrentState) {
		case ServiceState::Running: return Frame()->GetIconIndex(IDI_RUN);
		case ServiceState::Stopped: return Frame()->GetIconIndex(IDI_KILL);	// stop.ico
	}
	return Frame()->GetIconIndex(IDI_SERVICES);
}

int CServicesView::GetSaveColumnRange(HWND, int&) const {
	// DoSort keeps the selection (by name), so CVirtualListView doesn't need to
	return -1;
}

void CServicesView::DoSort(SortInfo const* si) {
	auto selected = GetSelectedService();
	auto selectedName = selected ? selected->Info.GetName() : L"";

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	std::ranges::stable_sort(m_Items, [&](auto& s1, auto& s2) {
		auto result = CompareServices(*s1, *s2, column);
		return si->SortAscending ? result < 0 : result > 0;
		});

	SelectService(selectedName);
}

int CServicesView::CompareServices(ServiceItem& s1, ServiceItem& s2, Column column) const {
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	auto compareText = [](std::wstring const& a, std::wstring const& b) { return ::_wcsicmp(a.c_str(), b.c_str()); };
	auto& st1 = s1.Info.GetStatusProcess();
	auto& st2 = s2.Info.GetStatusProcess();

	switch (column) {
		case Column::State: return compare(st1.CurrentState, st2.CurrentState);
		case Column::ProcessId: return compare(st1.ProcessId, st2.ProcessId);
		case Column::Type: return compare(st1.Type, st2.Type);
		case Column::StartType:
		{
			auto c1 = s1.GetConfig(), c2 = s2.GetConfig();
			if (!c1 || !c2)
				return compare(c1 != nullptr, c2 != nullptr);
			if (auto result = compare(c1->StartType, c2->StartType); result)
				return result;
			return compare(c1->DelayedAutoStart, c2->DelayedAutoStart);
		}
		case Column::ErrorControl:
		{
			auto c1 = s1.GetConfig(), c2 = s2.GetConfig();
			if (!c1 || !c2)
				return compare(c1 != nullptr, c2 != nullptr);
			return compare(c1->ErrorControl, c2->ErrorControl);
		}
	}
	// the rest are text
	return compareText(GetText(s1, column), GetText(s2, column));
}

LRESULT CServicesView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			if (index < 0 || index >= (int)m_Items.size())
				break;
			auto& item = *m_Items[index];
			bool dark = WTLHelper::IsDarkMode();
			if (item.NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (item.ChangedTicks > 0) {
				cd->clrTextBk = dark ? RGB(112, 88, 0) : RGB(255, 236, 140);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

CServicesView::ServiceItem* CServicesView::GetSelectedService() const {
	int index = m_List.GetSelectedIndex();
	return index >= 0 && index < (int)m_Items.size() ? m_Items[index].get() : nullptr;
}

void CServicesView::SelectService(std::wstring const& name) {
	// the list view keeps the selection by index, which sorting and changes to the list shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (name.empty())
		return;
	auto it = std::ranges::find_if(m_Items, [&](auto& item) { return item->Info.GetName() == name; });
	if (it != m_Items.end()) {
		int index = (int)(it - m_Items.begin());
		m_List.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
}

bool CServicesView::OnRightClickList(HWND, int row, int, POINT const& pt) {
	if (row < 0 || row >= (int)m_Items.size())
		return false;

	auto& item = *m_Items[row];
	auto& status = item.Info.GetStatusProcess();
	auto state = status.CurrentState;
	auto accepted = status.ControlsAccepted;
	bool pauseContinue = (accepted & ServiceControlsAccepted::PauseContinue) == ServiceControlsAccepted::PauseContinue;
	bool canStop = (accepted & ServiceControlsAccepted::Stop) == ServiceControlsAccepted::Stop;

	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	CMenuHandle popup = menu.GetSubMenu(1);
	MenuItemData const icons[] = {
		{ ID_SERVICE_START, IDI_RUN },
		{ ID_SERVICE_STOP, IDI_KILL },
		{ ID_SERVICE_PAUSE, IDI_PAUSE },
		{ ID_SERVICE_CONTINUE, IDI_CONTINUE },
		{ ID_SERVICE_RESTART, IDI_REFRESH },
	};
	WTLHelper::InitMenu(popup, icons, _countof(icons), DpiHelper::SmallIconSize(m_hWnd));

	auto enable = [&](UINT id, bool enabled) {
		popup.EnableMenuItem(id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
	};
	enable(ID_SERVICE_START, state == ServiceState::Stopped);
	enable(ID_SERVICE_STOP, canStop && state != ServiceState::Stopped);
	enable(ID_SERVICE_PAUSE, pauseContinue && state == ServiceState::Running);
	enable(ID_SERVICE_CONTINUE, pauseContinue && state == ServiceState::Paused);
	enable(ID_SERVICE_RESTART, canStop && state == ServiceState::Running && m_RestartName.empty());
	enable(ID_SERVICE_GOTOPROCESS, status.ProcessId != 0);
	// the current start type, checked in its submenu
	if (auto config = item.GetConfig()) {
		auto id = config->StartType == ServiceStartType::Auto ? (config->DelayedAutoStart ? ID_SERVICE_START_DELAYED : ID_SERVICE_START_AUTO)
			: config->StartType == ServiceStartType::Demand ? ID_SERVICE_START_MANUAL
			: config->StartType == ServiceStartType::Disabled ? ID_SERVICE_START_DISABLED : 0;
		for (int i = 0; i < popup.GetMenuItemCount(); i++)
			if (CMenuHandle sub = popup.GetSubMenu(i); sub && id)
				sub.CheckMenuRadioItem(ID_SERVICE_START_AUTO, ID_SERVICE_START_DISABLED, id, MF_BYCOMMAND);
	}

	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CServicesView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	if (auto pid = m_Items[row]->Info.GetStatusProcess().ProcessId; pid)
		Frame()->GoToProcess(pid);
	return true;
}

void CServicesView::ShowError(PCWSTR operation, DWORD error) const {
	auto text = std::format(L"{} failed", operation);
	if (error)
		text += L": " + Helpers::GetErrorText(error);
	if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
		text += L"\nControlling most services requires running as administrator (File > Run as Administrator).";
	AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

bool CServicesView::Control(ServiceAccessMask access, PCWSTR operation, std::function<bool(WinSys::Service&)> const& action) {
	auto item = GetSelectedService();
	if (!item)
		return false;

	auto service = WinSys::Service::Open(item->Info.GetName(), access);
	if (!service) {
		ShowError(operation, ::GetLastError());
		return false;
	}
	if (!action(*service)) {
		ShowError(operation, ::GetLastError());
		return false;
	}
	return true;
}

LRESULT CServicesView::OnStart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Control(ServiceAccessMask::Start, L"Starting the service", [](auto& svc) { return svc.Start(); });
	return 0;
}

LRESULT CServicesView::OnStop(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Control(ServiceAccessMask::Stop, L"Stopping the service", [](auto& svc) { return svc.Stop(); });
	return 0;
}

LRESULT CServicesView::OnPause(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Control(ServiceAccessMask::PauseContinue, L"Pausing the service", [](auto& svc) { return svc.Pause(); });
	return 0;
}

LRESULT CServicesView::OnContinue(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Control(ServiceAccessMask::PauseContinue, L"Continuing the service", [](auto& svc) { return svc.Continue(); });
	return 0;
}

LRESULT CServicesView::OnRestart(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedService();
	if (!item || !m_RestartName.empty())
		return 0;

	if (!Control(ServiceAccessMask::Stop | ServiceAccessMask::Start, L"Stopping the service", [](auto& svc) { return svc.Stop(); }))
		return 0;

	m_RestartName = item->Info.GetName();
	m_RestartDeadline = ::GetTickCount64() + RestartTimeout;
	SetTimer(RestartTimerId, 250);
	return 0;
}

LRESULT CServicesView::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != RestartTimerId) {
		bHandled = FALSE;
		return 0;
	}

	auto state = ServiceManager::GetServiceState(m_RestartName);
	if (state != ServiceState::Stopped && ::GetTickCount64() < m_RestartDeadline)
		return 0;

	KillTimer(RestartTimerId);
	auto name = std::move(m_RestartName);
	m_RestartName.clear();
	if (state != ServiceState::Stopped) {
		AtlMessageBox(m_hWnd, std::format(L"Service {} did not stop in time; not restarting it.", name).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return 0;
	}

	auto service = WinSys::Service::Open(name, ServiceAccessMask::Start);
	if (!service || !service->Start())
		ShowError(L"Starting the service", ::GetLastError());
	return 0;
}

void CServicesView::OnFrameIdle() {
	Frame()->GetUI().UIEnable(ID_VIEW_PROPERTIES, GetSelectedService() != nullptr);
}

LRESULT CServicesView::OnStartType(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedService();
	if (!item)
		return 0;
	auto type = wID == ID_SERVICE_START_MANUAL ? ServiceStartType::Demand
		: wID == ID_SERVICE_START_DISABLED ? ServiceStartType::Disabled : ServiceStartType::Auto;
	bool changed = Control(ServiceAccessMask::ChangeConfig | ServiceAccessMask::QueryConfig, L"Changing the start type", [&](WinSys::Service& svc) {
		ServiceConfigChange change;
		change.StartType = type;
		return svc.ChangeConfig(change) && (type != ServiceStartType::Auto || svc.SetDelayedAutoStart(wID == ID_SERVICE_START_DELAYED));
		});
	if (changed) {
		item->Reload();
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}
	return 0;
}

LRESULT CServicesView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedService();
	if (!item)
		return 0;
	CServiceSheet sheet(item->Info.GetName());
	sheet.DoModal(m_hWnd);
	if (sheet.Applied()) {
		item->Reload();
		m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
	}
	return 0;
}

LRESULT CServicesView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto item = GetSelectedService(); item && item->Info.GetStatusProcess().ProcessId)
		Frame()->GoToProcess(item->Info.GetStatusProcess().ProcessId);
	return 0;
}

LRESULT CServicesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedService();
	if (!item)
		return 0;

	// the shown columns in display order, tab separated
	std::wstring text;
	for (auto& c : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetText(*item, (Column)c.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
