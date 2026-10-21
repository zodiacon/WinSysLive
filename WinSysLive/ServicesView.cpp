#include "pch.h"
#include <DpiHelper.h>
#include "resource.h"
#include "ServicesView.h"
#include "ServiceSheet.h"
#include "ServiceInstallDlg.h"
#include "FormatHelper.h"
#include "AppSettings.h"
#include <WinSys/KernelModules.h>
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ClipboardHelper.h>
#include <SortHelper.h>
#include <unordered_set>

using namespace WinSys;

namespace {
	// highlighting lasts this many updates
	const int HighlightTicks = 2;

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

	// the Drivers tab's: the same columns (and keys, in the DriverServiceColumns setting); a driver has no process or account
	const ColumnLayout::ColumnDef s_DriverColumns[] = {
		{ L"Name", L"Name", 180, LVCFMT_LEFT, true, true },
		{ L"Display Name", L"DisplayName", 260, LVCFMT_LEFT, true },
		{ L"State", L"State", 80, LVCFMT_LEFT, true, true },
		{ L"Start Type", L"StartType", 120, LVCFMT_LEFT, true },
		{ L"Type", L"Type", 120, LVCFMT_LEFT, true },
		{ L"PID", L"ProcessId", 60, LVCFMT_RIGHT, false },
		{ L"Process", L"ProcessName", 120, LVCFMT_LEFT, false },
		{ L"Account", L"Account", 160, LVCFMT_LEFT, false },
		{ L"Binary Path", L"BinaryPath", 350, LVCFMT_LEFT, true },
		{ L"Description", L"Description", 400, LVCFMT_LEFT, true },
		{ L"Error Control", L"ErrorControl", 90, LVCFMT_LEFT, true },
		{ L"Dependencies", L"Dependencies", 250, LVCFMT_LEFT, false },
		{ L"Required Privileges", L"Privileges", 350, LVCFMT_LEFT, false },
		{ L"Triggers", L"Triggers", 300, LVCFMT_LEFT, false },
	};
	static_assert(_countof(s_DriverColumns) == _countof(s_Columns));

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
	return m_Kind == Kind::Drivers ? L"Drivers" : L"Services";
}

std::span<ColumnLayout::ColumnDef const> CServicesView::Columns() const {
	if (m_Kind == Kind::Drivers)
		return s_DriverColumns;
	return s_Columns;
}

WinSys::ServiceEnumType CServicesView::EnumType() const {
	return m_Kind == Kind::Drivers ? ServiceEnumType::AllDrivers : ServiceEnumType::AllServices;
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
	auto& settings = AppSettings::Get();
	SetColumns(ColumnLayout::Parse(m_Kind == Kind::Drivers ? settings.DriverServiceColumns() : settings.ServiceColumns(), Columns()));

	int width = 360;	// at 96 DPI
	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, width, m_Kind == Kind::Drivers ? L"Filter: name, description or path (Ctrl+F)"
		: L"Filter: name, description, process or path (Ctrl+F)", m_List,
		[this](auto&) {
			ShowItems();
			UpdateFilterStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());

	for (auto& svc : ServiceManager::EnumServices(EnumType())) {
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
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, Columns());
}

void CServicesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	auto layout = ColumnLayout::Format(GetShownColumns(), Columns());
	m_Kind == Kind::Drivers ? settings.DriverServiceColumns(layout) : settings.ServiceColumns(layout);
	settings.Save();
}

LRESULT CServicesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), Columns())) {
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
	Frame()->ShowContextMenu(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, Columns()), pt, TPM_RIGHTBUTTON);
	return 0;
}

LRESULT CServicesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || Columns()[m_MenuColumn].Mandatory)
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
	auto services = ServiceManager::EnumServices(EnumType());
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
		Frame()->SetStatusText(0, std::format(L"{} of {} {} match the filter", m_Items.size(), m_All.size(),
			m_Kind == Kind::Drivers ? L"drivers" : L"services").c_str());
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
	if (row < 0 || row >= (int)m_Items.size()) {
		// below the rows: only New Service
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, ID_SERVICE_INSTALL, m_Kind == Kind::Drivers ? L"&New Driver..." : L"&New Service...");
		Frame()->ShowContextMenu(menu.m_hMenu, pt, TPM_RIGHTBUTTON);
		return true;
	}

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
	enable(ID_SERVICE_STOP, canStop && state != ServiceState::Stopped && !m_Control.IsBusy());
	enable(ID_SERVICE_PAUSE, pauseContinue && state == ServiceState::Running);
	enable(ID_SERVICE_CONTINUE, pauseContinue && state == ServiceState::Paused);
	enable(ID_SERVICE_RESTART, canStop && state == ServiceState::Running && !m_Control.IsBusy());
	enable(ID_SERVICE_GOTOPROCESS, status.ProcessId != 0);
	// a driver has no process: its loaded module instead
	if (m_Kind == Kind::Drivers) {
		for (int i = 0; i < popup.GetMenuItemCount(); i++)
			if (popup.GetMenuItemID(i) == ID_SERVICE_GOTOPROCESS) {
				popup.DeleteMenu(i, MF_BYPOSITION);
				popup.InsertMenu(i, MF_BYPOSITION | MF_STRING | (state == ServiceState::Running ? 0 : MF_GRAYED), ID_SERVICE_GOTOKMODULE,
					L"Go to &Kernel Module");
				break;
			}
	}
	// the Start Type submenu
	CMenuHandle startType;
	for (int i = 0; i < popup.GetMenuItemCount() && !startType; i++)
		if (CMenuHandle sub = popup.GetSubMenu(i); sub && sub.GetMenuState(ID_SERVICE_START_AUTO, MF_BYCOMMAND) != (UINT)-1)
			startType = sub;
	// a driver starts with the boot loader or the kernel's initialization too, and never delayed
	if (startType && m_Kind == Kind::Drivers) {
		startType.DeleteMenu(ID_SERVICE_START_DELAYED, MF_BYCOMMAND);
		startType.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_SERVICE_START_BOOT, L"&Boot");
		startType.InsertMenu(1, MF_BYPOSITION | MF_STRING, ID_SERVICE_START_SYSTEM, L"&System");
	}
	// the current start type, checked
	if (auto config = item.GetConfig(); config && startType) {
		auto id = config->StartType == ServiceStartType::Auto ? (config->DelayedAutoStart ? ID_SERVICE_START_DELAYED : ID_SERVICE_START_AUTO)
			: config->StartType == ServiceStartType::Demand ? ID_SERVICE_START_MANUAL
			: config->StartType == ServiceStartType::Disabled ? ID_SERVICE_START_DISABLED
			: config->StartType == ServiceStartType::Boot ? ID_SERVICE_START_BOOT
			: config->StartType == ServiceStartType::System ? ID_SERVICE_START_SYSTEM : 0;
		// by position: the drivers' IDs aren't next to the others
		for (int i = 0; i < startType.GetMenuItemCount(); i++)
			if (id && startType.GetMenuItemID(i) == id)
				startType.CheckMenuRadioItem(0, startType.GetMenuItemCount() - 1, i, MF_BYPOSITION);
	}

	Frame()->ShowContextMenu(popup, pt, TPM_RIGHTBUTTON);
	return true;
}

bool CServicesView::OnDoubleClickList(HWND, int row, int, POINT const&) {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	// a service's process, a driver's loaded module
	if (m_Kind == Kind::Drivers) {
		if (auto path = DriverPath(*m_Items[row]); !path.empty())
			Frame()->GoToKernelModule(path);
	}
	else if (auto pid = m_Items[row]->Info.GetStatusProcess().ProcessId; pid)
		Frame()->GoToProcess(pid);
	return true;
}

std::wstring CServicesView::DriverPath(ServiceItem& item) const {
	auto config = item.GetConfig();
	return config ? WinSys::KernelModules::DriverImagePath(item.Info.GetName(), config->BinaryPathName) : std::wstring();
}

bool CServicesView::ShowDriver(std::wstring const& path) {
	if (m_Kind != Kind::Drivers)
		return false;
	// the configurations are loaded (once, cached) as the services are compared
	auto it = std::ranges::find_if(m_All, [&](auto& item) { return ::_wcsicmp(DriverPath(*item).c_str(), path.c_str()) == 0; });
	if (it == m_All.end())
		return false;
	auto name = (*it)->Info.GetName();
	// a filter that hides it goes
	if (!Matches(**it))
		m_Filter.Clear();
	SelectService(name);
	if (int selected = m_List.GetSelectedIndex(); selected >= 0)
		m_List.EnsureVisible(selected, FALSE);
	m_List.SetFocus();
	return true;
}

LRESULT CServicesView::OnGoToKernelModule(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto item = GetSelectedService()) {
		if (auto path = DriverPath(*item); !path.empty())
			Frame()->GoToKernelModule(path);
	}
	return 0;
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
	auto item = GetSelectedService();
	if (item && m_Control.Plan(m_hWnd, item->Info.GetName(), item->Info.GetDisplayName(), false))
		RunSteps();
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
	if (item && m_Control.Plan(m_hWnd, item->Info.GetName(), item->Info.GetDisplayName(), true))
		RunSteps();
	return 0;
}

void CServicesView::RunSteps() {
	if (m_Control.Run(m_hWnd))
		SetTimer(StepTimerId, 250);
	else
		KillTimer(StepTimerId);
}

LRESULT CServicesView::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != StepTimerId) {
		bHandled = FALSE;
		return 0;
	}
	RunSteps();
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
		: wID == ID_SERVICE_START_DISABLED ? ServiceStartType::Disabled
		: wID == ID_SERVICE_START_BOOT ? ServiceStartType::Boot
		: wID == ID_SERVICE_START_SYSTEM ? ServiceStartType::System : ServiceStartType::Auto;
	bool changed = Control(ServiceAccessMask::ChangeConfig | ServiceAccessMask::QueryConfig, L"Changing the start type", [&](WinSys::Service& svc) {
		ServiceConfigChange change;
		change.StartType = type;
		// delayed start is a Win32 service's only
		return svc.ChangeConfig(change) && (type != ServiceStartType::Auto || m_Kind == Kind::Drivers
			|| svc.SetDelayedAutoStart(wID == ID_SERVICE_START_DELAYED));
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

LRESULT CServicesView::OnInstall(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CServiceInstallDlg dlg(m_Kind == Kind::Drivers);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	OnRefresh(true);
	auto it = m_ItemsByName.find(dlg.GetName());
	if (it == m_ItemsByName.end())
		return 0;
	// a filter that hides it goes
	if (!Matches(*it->second))
		m_Filter.Clear();
	SelectService(dlg.GetName());
	if (int selected = m_List.GetSelectedIndex(); selected >= 0)
		m_List.EnsureVisible(selected, FALSE);
	m_List.SetFocus();
	return 0;
}

LRESULT CServicesView::OnDelete(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = GetSelectedService();
	if (!item)
		return 0;
	auto name = item->Info.GetName();
	bool running = item->Info.GetStatusProcess().CurrentState != ServiceState::Stopped;
	auto text = std::format(L"Delete {} ({})?\n\nIts configuration is removed from the registry; the file stays.", item->Info.GetDisplayName(), name);
	if (running)
		text += L" It's running, so it goes once it stops and every handle to it is closed.";
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return 0;
	if (!ServiceManager::Uninstall(name)) {
		ShowError(L"Deleting the service", ::GetLastError());
		return 0;
	}
	OnRefresh(true);
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
