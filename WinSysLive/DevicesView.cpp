#include "pch.h"
#include "resource.h"
#include "DevicesView.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <ListViewhelper.h>
#include <DpiHelper.h>
#include <ClipboardHelper.h>
#include <ToolbarHelper.h>
#include <WTLHelper.h>
#include <WinSys/KernelModules.h>
#include <WinSys/ServiceManager.h>
#include <map>
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

#pragma comment(lib, "setupapi")

using namespace WinSys;

namespace {
	// in CDevicesView::Column order; the keys are stored in the DeviceColumns setting (widths at 96 DPI)
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Device", L"Name", 320, LVCFMT_LEFT, true, true },
		{ L"Class", L"Class", 130, LVCFMT_LEFT, true },
		{ L"Status", L"Status", 150, LVCFMT_LEFT, true },
		{ L"Driver", L"Driver", 110, LVCFMT_LEFT, true },
		{ L"Manufacturer", L"Manufacturer", 160, LVCFMT_LEFT, true },
		{ L"Driver Version", L"DriverVersion", 120, LVCFMT_LEFT, true },
		{ L"Driver Date", L"DriverDate", 90, LVCFMT_LEFT, false },
		{ L"Provider", L"Provider", 140, LVCFMT_LEFT, false },
		{ L"INF", L"Inf", 100, LVCFMT_LEFT, false },
		{ L"Location", L"Location", 200, LVCFMT_LEFT, true },
		{ L"Instance ID", L"InstanceId", 360, LVCFMT_LEFT, true },
		{ L"PDO Name", L"PdoName", 130, LVCFMT_LEFT, false },
		{ L"Enumerator", L"Enumerator", 90, LVCFMT_LEFT, false },
	};

	// the toolbar right of the filter box
	const ToolBarButtonInfo s_BarButtons[] = {
		{ ID_DEVICES_BYCONNECTION, IDI_TREE, BTNS_CHECK | BTNS_AUTOSIZE, L"By Connection" },
		{ ID_DEVICES_PROBLEMS, IDI_ALERT, BTNS_CHECK | BTNS_AUTOSIZE, L"Problem Devices" },
		{ ID_DEVICES_HIDDEN, IDI_PROCESS_HIDE, BTNS_CHECK | BTNS_AUTOSIZE, L"Hidden Devices" },
		{ ID_DEVICES_DISCONNECTED, IDI_DEVICE, BTNS_CHECK | BTNS_AUTOSIZE, L"Disconnected Devices" },
		{ ID_DEVICES_RESCAN, IDI_REFRESH, BTNS_BUTTON | BTNS_AUTOSIZE, L"Scan Again" },
	};
	const int BarImageSize = 16;	// at 96 DPI
	const UINT_PTR ChangeTimerId = 1;
	const UINT ChangeDelay = 1000;	// ms after the last device change before scanning again

	DWORD CALLBACK OnDeviceNotification(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION, PCM_NOTIFY_EVENT_DATA, DWORD) {
		::PostMessage((HWND)context, CDevicesView::WM_DEVICES_CHANGED, 0, 0);
		return ERROR_SUCCESS;
	}

	std::wstring DateText(int64_t time) {
		if (!time)
			return L"";
		FILETIME ft{ (DWORD)time, (DWORD)(time >> 32) };
		SYSTEMTIME st;
		if (!::FileTimeToSystemTime(&ft, &st))
			return L"";
		WCHAR text[64]{};
		::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, text, _countof(text), nullptr);
		return text;
	}

	std::wstring Lower(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}
}

std::wstring CDevicesView::Row::Key() const {
	return Kind == RowKind::Device ? std::wstring() : (Kind == RowKind::Class ? L"class:" : L"group:") + Name;
}

int CDevicesView::ClassImage(std::wstring const& classGuid) {
	if (!m_ClassImages.ImageList)
		return Frame()->GetIconIndex(IDI_DEVICE);
	if (auto it = m_ClassImageIndex.find(classGuid); it != m_ClassImageIndex.end())
		return it->second;
	// GUID_DEVCLASS_UNKNOWN for none (Device Manager's "Other devices" icon)
	GUID guid = { 0x4d36e97e, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
	if (!classGuid.empty())
		::CLSIDFromString(classGuid.c_str(), &guid);
	int index = -1;
	if (!::SetupDiGetClassImageIndex(&m_ClassImages, &guid, &index))
		index = -1;
	m_ClassImageIndex[classGuid] = index;
	return index;
}

CString CDevicesView::GetTitle() const {
	return L"Devices";
}

LRESULT CDevicesView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// a shared list either way: the class image list is destroyed with SetupDiDestroyClassImageList, the frame's by the frame
	m_Tree.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	if (::SetupDiGetClassImageList(&m_ClassImages))
		m_Tree.SetImageList(m_ClassImages.ImageList, LVSIL_SMALL);
	else {
		m_ClassImages.ImageList = nullptr;
		m_Tree.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}
	m_Tree.SetCallbackMode(true);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());
	ListViewHelper::EnableColumnDpiScaling(m_Tree.GetListCtrl());

	auto& settings = AppSettings::Get();
	SetColumns(ColumnLayout::Parse(settings.DeviceColumns(), s_Columns));
	m_ByConnection = settings.DevicesByConnection() != 0;
	m_ShowHidden = settings.DevicesShowHidden() != 0;
	m_ShowDisconnected = settings.DevicesShowDisconnected() != 0;

	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 320, L"Filter: device, class, ID, driver or maker (Ctrl+F)",
		m_Tree.GetListCtrl(), [this](auto&) {
			Build();
			UpdateStatus();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	m_Bar = ToolbarHelper::CreateAndInitToolBar(m_hWndToolBar, s_BarButtons, _countof(s_BarButtons), BarImageSize);
	m_BarFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_Bar)));
	m_Bar.SetFont(m_BarFont);
	m_Bar.AutoSize();
	AddSimpleReBarBand(m_Bar, nullptr, FALSE);
	// two bands would get grippers; the filter keeps its width, the bar's band takes the rest of the row
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_STYLE | RBBIM_CHILD };
		rebar.GetBandInfo(i, &band);
		band.fStyle |= RBBS_NOGRIPPER;
		band.fMask = RBBIM_STYLE;
		if (band.hwndChild == m_Filter.GetHwnd()) {
			band.fMask |= RBBIM_CHILDSIZE;
			band.cxMinChild = m_Filter.GetWidth();
		}
		rebar.SetBandInfo(i, &band);
	}
	UpdateBar();

	// every device instance's arrivals and removals: scanned again a second after the last
	CM_NOTIFY_FILTER filter{ sizeof(filter) };
	filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
	filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
	if (::CM_Register_Notification(&filter, m_hWnd, OnDeviceNotification, &m_Notification) != CR_SUCCESS)
		m_Notification = nullptr;
	StartScan();
	return 0;
}

LRESULT CDevicesView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// waits for a callback in progress
	if (m_Notification)
		::CM_Unregister_Notification(m_Notification);
	m_Notification = nullptr;
	if (m_Worker.joinable())
		m_Worker.join();
	if (m_ClassImages.ImageList) {
		m_Tree.SetImageList(nullptr, LVSIL_SMALL);
		::SetupDiDestroyClassImageList(&m_ClassImages);
		m_ClassImages.ImageList = nullptr;
	}
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

void CDevicesView::UpdateLayout(BOOL resizeBars) {
	CViewBase::UpdateLayout(resizeBars);
	if (m_Bar) {
		CReBarCtrl rebar(m_hWndToolBar);
		rebar.MaximizeBand(rebar.GetBandCount() - 1, FALSE);
	}
}

void CDevicesView::LayoutBar() {
	if (!m_Bar)
		return;
	ToolbarHelper::UpdateToolBarImages(m_Bar, s_BarButtons, _countof(s_BarButtons), BarImageSize);
	if (m_BarFont)
		m_BarFont.DeleteObject();
	m_BarFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_Bar)));
	m_Bar.SetFont(m_BarFont);
	m_Bar.AutoSize();
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_CHILD };
		if (rebar.GetBandInfo(i, &band) && band.hwndChild == m_Bar) {
			SIZE size{};
			m_Bar.GetMaxSize(&size);
			band.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
			band.cxMinChild = band.cxIdeal = size.cx;
			band.cyMinChild = size.cy;
			rebar.SetBandInfo(i, &band);
			break;
		}
	}
	UpdateLayout();
}

void CDevicesView::UpdateBar() {
	if (!m_Bar)
		return;
	m_Bar.CheckButton(ID_DEVICES_BYCONNECTION, m_ByConnection);
	m_Bar.CheckButton(ID_DEVICES_PROBLEMS, m_ProblemsOnly);
	m_Bar.CheckButton(ID_DEVICES_HIDDEN, m_ShowHidden);
	m_Bar.CheckButton(ID_DEVICES_DISCONNECTED, m_ShowDisconnected);
	m_Bar.EnableButton(ID_DEVICES_RESCAN, !m_Scanning);
}

void CDevicesView::OnActivate(bool active) {
	Frame()->GetUI().UIEnable(ID_VIEW_SELECTCOLUMNS, active);
	m_Active = active;
	if (active) {
		UpdateStatus();
		m_Tree.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CDevicesView::OnThemeChanged(bool dark) {
	m_Tree.SetBkColor(dark ? DarkMode::getViewBackgroundColor() : ::GetSysColor(COLOR_WINDOW));
	m_Tree.SetTextColor(dark ? DarkMode::getViewTextColor() : ::GetSysColor(COLOR_WINDOWTEXT));
	m_Tree.Invalidate();
}

void CDevicesView::OnFontChanged(HFONT font) {
	m_Tree.SetFont(font);
	m_Filter.SetFont(font);
}

void CDevicesView::StartScan() {
	if (m_Scanning) {
		m_ScanAgain = true;
		return;
	}
	m_Scanning = true;
	m_ScanAgain = false;
	UpdateBar();
	UpdateStatus();
	// the previous scan is done (m_Scanning was off): joining it doesn't wait
	m_Worker = std::jthread([this, hWnd = m_hWnd, disconnected = m_ShowDisconnected] {
		auto devices = Devices::Enum(disconnected);
		{
			std::lock_guard lock(m_ResultLock);
			m_Result = std::move(devices);
		}
		::PostMessage(hWnd, WM_DEVICES_SCANNED, 0, 0);
		});
}

LRESULT CDevicesView::OnScanned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Scanning = false;
	std::optional<std::vector<DeviceInfo>> result;
	{
		std::lock_guard lock(m_ResultLock);
		result = std::move(m_Result);
		m_Result.reset();
	}
	if (result) {
		m_Devices = std::move(*result);
		FILETIME ft;
		::GetSystemTimeAsFileTime(&ft);
		m_ScanTime = (int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart;
		Build();
	}
	UpdateBar();
	UpdateStatus();
	if (m_ScanAgain)
		StartScan();
	return 0;
}

LRESULT CDevicesView::OnDevicesChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// changes come in bursts: one scan a second after the last
	SetTimer(ChangeTimerId, ChangeDelay);
	return 0;
}

LRESULT CDevicesView::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != ChangeTimerId) {
		bHandled = FALSE;
		return 0;
	}
	KillTimer(ChangeTimerId);
	StartScan();
	return 0;
}

bool CDevicesView::MatchesFilter(DeviceInfo const& d) const {
	auto& filter = m_Filter.GetText();
	if (filter.empty())
		return true;
	for (auto text : { &d.Name, &d.Class, &d.ClassName, &d.InstanceId, &d.Service, &d.Manufacturer, &d.DriverProvider })
		if (CFilterBox::Contains(*text, filter))
			return true;
	return false;
}

bool CDevicesView::IsShown(DeviceInfo const& d) const {
	if (!d.Present && !m_ShowDisconnected)
		return false;
	if (d.IsHidden() && !m_ShowHidden && d.Present)
		return false;
	if (m_ProblemsOnly && !d.HasProblem() && !d.IsDisabled())
		return false;
	return MatchesFilter(d);
}

void CDevicesView::Build() {
	// what's selected and expanded, by key (a device's instance ID)
	auto keyOf = [&](Row const& r) { return r.Kind == RowKind::Device ? m_Devices.size() > (size_t)r.Device ? m_Devices[r.Device].InstanceId : std::wstring() : r.Key(); };
	std::wstring selected;
	if (auto row = GetSelectedRow())
		selected = keyOf(*row);
	if (m_Built) {
		m_Expanded.clear();
		for (auto& r : m_Rows)
			if (r.Item && m_Tree.IsExpanded(r.Item))
				m_Expanded.insert(keyOf(r));
	}

	m_Tree.BeginUpdate();
	m_Tree.DeleteAllItems();
	m_Rows.clear();
	// the computer has the Computer class's icon (the root device has no class)
	auto imageOf = [&](DeviceInfo const& d) {
		return ClassImage(d.Depth == 0 ? std::wstring(L"{4d36e966-e325-11ce-bfc1-08002be10318}") : d.ClassGuid);
	};
	int count = (int)m_Devices.size();
	std::vector<char> shown(count);
	for (int i = 0; i < count; i++)
		shown[i] = IsShown(m_Devices[i]);

	if (m_ByConnection) {
		// a shown device's ancestors stay, so that it has its place in the tree
		std::vector<char> keep(count);
		for (int i = 0; i < count; i++)
			if (shown[i])
				for (int p = i; p >= 0 && !keep[p]; p = m_Devices[p].Parent)
					keep[p] = 1;
		std::vector<HTLITEM> items(count);
		HTLITEM group = nullptr;
		for (int i = 0; i < count; i++) {
			if (!keep[i])
				continue;
			auto& d = m_Devices[i];
			HTLITEM parent = d.Parent >= 0 ? items[d.Parent] : TLV_ROOT;
			if (!d.Present) {
				if (!group) {
					auto& g = m_Rows.emplace_back();
					g.Kind = RowKind::Disconnected;
					g.Name = L"Disconnected devices";
					g.Item = group = m_Tree.InsertItem(TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, ClassImage(L""), reinterpret_cast<LPARAM>(&g));
				}
				parent = group;
			}
			auto& row = m_Rows.emplace_back();
			row.Device = i;
			// the computer is never a mere ancestor
			row.Match = shown[i] != 0 || m_Devices[i].Depth == 0;
			row.Item = items[i] = m_Tree.InsertItem(parent ? parent : TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, imageOf(d), reinterpret_cast<LPARAM>(&row));
		}
		if (group)
			for (auto& r : m_Rows)
				if (r.Kind == RowKind::Disconnected)
					r.Count = (int)std::ranges::count_if(m_Devices, [](auto& d) { return !d.Present; });
	}
	else {
		// by class, the classes and their devices by name (the computer itself isn't a device of a class)
		std::map<std::wstring, std::pair<std::wstring, std::vector<int>>> classes;
		for (int i = 0; i < count; i++) {
			if (!shown[i] || m_Devices[i].Depth == 0)
				continue;
			auto name = m_Devices[i].ClassName.empty() ? std::wstring(L"Other devices") : m_Devices[i].ClassName;
			auto& entry = classes[Lower(name)];
			entry.first = name;
			entry.second.push_back(i);
		}
		for (auto& [key, entry] : classes) {
			auto& [name, devices] = entry;
			std::ranges::sort(devices, [&](int a, int b) { return ::_wcsicmp(m_Devices[a].Name.c_str(), m_Devices[b].Name.c_str()) < 0; });
			auto& c = m_Rows.emplace_back();
			c.Kind = RowKind::Class;
			c.Name = name;
			c.Count = (int)devices.size();
			c.Problems = (int)std::ranges::count_if(devices, [&](int i) { return m_Devices[i].HasProblem(); });
			c.Item = m_Tree.InsertItem(TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, imageOf(m_Devices[devices.front()]), reinterpret_cast<LPARAM>(&c));
			for (int i : devices) {
				auto& row = m_Rows.emplace_back();
				row.Device = i;
				row.Item = m_Tree.InsertItem(c.Item, TLV_LAST, LPSTR_TEXTCALLBACK, imageOf(m_Devices[i]), reinterpret_cast<LPARAM>(&row));
			}
		}
	}

	// expanded as before; at first, the connection tree's first two levels and the classes with problems; all while filtering
	bool filtering = !m_Filter.GetText().empty() || m_ProblemsOnly;
	HTLITEM hSelect = nullptr;
	for (auto& r : m_Rows) {
		auto key = keyOf(r);
		if (!selected.empty() && key == selected)
			hSelect = r.Item;
		if (!m_Tree.HasChildren(r.Item))
			continue;
		bool expand;
		if (filtering)
			expand = true;
		else if (!m_Built)
			expand = m_ByConnection ? m_Tree.GetItemLevel(r.Item) < 1 : r.Problems > 0;
		else
			expand = m_Expanded.contains(key);
		m_Tree.Expand(r.Item, expand);
	}
	if (m_SortColumn >= 0)
		Sort();
	m_Tree.EndUpdate();
	if (hSelect)
		m_Tree.SelectItem(hSelect, false);
	m_Built = true;
}

std::wstring CDevicesView::ComputerName() const {
	WCHAR name[MAX_COMPUTERNAME_LENGTH + 1]{};
	DWORD size = _countof(name);
	return ::GetComputerNameW(name, &size) ? name : L"Computer";
}

std::wstring CDevicesView::StatusText(DeviceInfo const& d) const {
	if (!d.Present)
		return L"Not connected";
	if (d.IsDisabled())
		return L"Disabled";
	if (d.HasProblem())
		return Devices::ProblemText(d.Problem);
	return (d.Status & DN_STARTED) ? L"OK" : L"Not started";
}

std::wstring CDevicesView::GetColumnText(Row const& row, Column column) const {
	if (row.Kind != RowKind::Device) {
		switch (column) {
			case Column::Name: return row.Name;
			case Column::Status:
				return row.Problems ? std::format(L"{} devices, {} with problems", row.Count, row.Problems)
					: std::format(L"{} device{}", row.Count, row.Count == 1 ? L"" : L"s");
		}
		return L"";
	}
	auto& d = m_Devices[row.Device];
	switch (column) {
		case Column::Name: return d.Depth == 0 && d.Present ? ComputerName() : d.Name.empty() ? d.InstanceId : d.Name;
		case Column::Class: return d.ClassName;
		case Column::Status: return StatusText(d);
		case Column::Driver: return d.Service;
		case Column::Manufacturer: return d.Manufacturer;
		case Column::DriverVersion: return d.DriverVersion;
		case Column::DriverDate: return DateText(d.DriverDate);
		case Column::Provider: return d.DriverProvider;
		case Column::Inf: return d.InfPath;
		case Column::Location: return d.Location;
		case Column::InstanceId: return d.InstanceId;
		case Column::PdoName: return d.PdoName;
		case Column::Enumerator: return d.Enumerator;
	}
	return L"";
}

LRESULT CDevicesView::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto& row = *reinterpret_cast<Row*>(m_Tree.GetItemData(di->hItem));
	if (di->iSubItem < (int)m_Columns.size())
		wcsncpy_s(di->pszText, di->cchTextMax, GetColumnText(row, m_Columns[di->iSubItem]).c_str(), _TRUNCATE);
	return 0;
}

LRESULT CDevicesView::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Tree) {
		bHandled = FALSE;
		return 0;
	}
	auto cd = reinterpret_cast<NMTLVCUSTOMDRAW*>(hdr);
	if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
		return CDRF_NOTIFYITEMDRAW;
	if ((cd->nmcd.dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT && cd->hItem) {
		auto& row = *reinterpret_cast<Row*>(m_Tree.GetItemData(cd->hItem));
		bool dark = WTLHelper::IsDarkMode();
		auto gray = dark ? RGB(128, 128, 128) : RGB(140, 140, 140);
		if (row.Kind == RowKind::Device) {
			auto& d = m_Devices[row.Device];
			if (d.Present && d.HasProblem()) {
				cd->clrTextBk = dark ? RGB(110, 70, 20) : RGB(255, 222, 170);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (!d.Present || d.IsDisabled() || !row.Match)
				cd->clrText = gray;
		}
		else if (row.Problems)
			cd->clrText = dark ? RGB(255, 190, 110) : RGB(170, 80, 0);
		return CDRF_NEWFONT;
	}
	return CDRF_DODEFAULT;
}

void CDevicesView::UpdateStatus() {
	if (!m_Active)
		return;
	std::wstring text;
	if (m_Scanning && m_Devices.empty())
		text = L"Scanning...";
	else {
		auto present = std::ranges::count_if(m_Devices, [](auto& d) { return d.Present; });
		auto problems = std::ranges::count_if(m_Devices, [](auto& d) { return d.Present && d.HasProblem(); });
		auto disabled = std::ranges::count_if(m_Devices, [](auto& d) { return d.Present && d.IsDisabled(); });
		text = std::format(L"{} devices", present);
		if (m_ShowDisconnected)
			text += std::format(L" and {} disconnected", m_Devices.size() - present);
		text += problems ? std::format(L"; {} with problems", problems) : L"; no problems";
		if (disabled)
			text += std::format(L", {} disabled", disabled);
		if (m_Scanning)
			text += L"; scanning again...";
		else if (m_ScanTime)
			text += L"; scanned at " + FormatHelper::FormatDateTime(m_ScanTime);
		if (!m_Notification)
			text += L" (changes aren't watched: Scan Again)";
	}
	Frame()->SetStatusText(0, text.c_str());
}

CDevicesView::Row* CDevicesView::GetSelectedRow() const {
	auto hItem = m_Tree.GetSelectedItem();
	return hItem ? reinterpret_cast<Row*>(m_Tree.GetItemData(hItem)) : nullptr;
}

DeviceInfo const* CDevicesView::GetSelectedDevice() const {
	auto row = GetSelectedRow();
	return row && row->Kind == RowKind::Device && row->Device < (int)m_Devices.size() ? &m_Devices[row->Device] : nullptr;
}

LRESULT CDevicesView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CDevicesView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CDevicesView::OnBarCommand(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto& settings = AppSettings::Get();
	switch (id) {
		case ID_DEVICES_BYCONNECTION:
			m_ByConnection = !m_ByConnection;
			settings.DevicesByConnection(m_ByConnection);
			// the other view's expansion starts over
			m_Built = false;
			Build();
			break;
		case ID_DEVICES_PROBLEMS:
			m_ProblemsOnly = !m_ProblemsOnly;
			Build();
			break;
		case ID_DEVICES_HIDDEN:
			m_ShowHidden = !m_ShowHidden;
			settings.DevicesShowHidden(m_ShowHidden);
			Build();
			break;
		case ID_DEVICES_DISCONNECTED:
			m_ShowDisconnected = !m_ShowDisconnected;
			settings.DevicesShowDisconnected(m_ShowDisconnected);
			// they're read only when shown
			StartScan();
			break;
		case ID_DEVICES_RESCAN:
			StartScan();
			break;
	}
	settings.Save();
	UpdateBar();
	UpdateStatus();
	return 0;
}

LRESULT CDevicesView::OnSort(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ns = reinterpret_cast<NMTLVSORT*>(hdr);
	m_SortColumn = (int)m_Columns[ns->iSubItem];
	m_SortAscending = ns->iDirection > 0;
	auto selected = m_Tree.GetSelectedItem();
	Sort();
	if (selected)
		m_Tree.SelectItem(selected, false);
	return 0;
}

void CDevicesView::Sort() {
	m_Tree.SortChildren(TLV_ROOT, CompareItems, reinterpret_cast<LPARAM>(this), true);
}

int CALLBACK CDevicesView::CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam) {
	auto view = reinterpret_cast<CDevicesView*>(lParam);
	auto& r1 = *reinterpret_cast<Row*>(view->m_Tree.GetItemData(h1));
	auto& r2 = *reinterpret_cast<Row*>(view->m_Tree.GetItemData(h2));
	auto column = (Column)view->m_SortColumn;
	int result;
	if (column == Column::DriverDate && r1.Kind == RowKind::Device && r2.Kind == RowKind::Device) {
		auto a = view->m_Devices[r1.Device].DriverDate, b = view->m_Devices[r2.Device].DriverDate;
		result = a < b ? -1 : a > b ? 1 : 0;
	}
	else
		result = ::_wcsicmp(view->GetColumnText(r1, column).c_str(), view->GetColumnText(r2, column).c_str());
	return view->m_SortAscending ? result : -result;
}

LRESULT CDevicesView::OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto ia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
	if (ia->iItem < 0 || !GetSelectedRow())
		return 0;
	CPoint pt(ia->ptAction);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowContextMenu(pt);
	return 0;
}

LRESULT CDevicesView::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	// the header's: told from a row's by the position
	CPoint ptHeader(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	CRect rcHeader;
	m_Tree.GetHeader().GetWindowRect(&rcHeader);
	if (rcHeader.PtInRect(ptHeader)) {
		HDHITTESTINFO hit{};
		hit.pt = ptHeader;
		m_Tree.GetHeader().ScreenToClient(&hit.pt);
		m_Tree.GetHeader().HitTest(&hit);
		m_MenuColumn = hit.iItem >= 0 && hit.iItem < (int)m_Columns.size() ? (int)m_Columns[hit.iItem] : -1;
		CMenu menu;
		Frame()->ShowContextMenu(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), ptHeader, TPM_RIGHTBUTTON);
		return 0;
	}
	// from the keyboard the position is -1, -1; a mouse click has its menu from NM_RCLICK
	auto hItem = m_Tree.GetSelectedItem();
	if (GET_X_LPARAM(lParam) != -1 || GET_Y_LPARAM(lParam) != -1 || !hItem) {
		bHandled = FALSE;
		return 0;
	}
	CRect rc;
	m_Tree.GetItemRect(hItem, &rc);
	CPoint pt(rc.left + 40, rc.bottom);
	m_Tree.GetListCtrl().ClientToScreen(&pt);
	ShowContextMenu(pt);
	return 0;
}

void CDevicesView::ShowContextMenu(POINT pt) {
	auto d = GetSelectedDevice();
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (d && d->Depth > 0 ? 0 : MF_GRAYED), ID_DEVICE_PROPERTIES, L"&Properties...");
	// a present device's state can change; the computer's can't
	bool changeable = d && d->Present && d->Depth > 0;
	menu.AppendMenu(MF_STRING | (changeable ? 0 : MF_GRAYED), ID_DEVICE_ENABLE,
		d && d->IsDisabled() ? L"&Enable Device" : L"Disa&ble Device...");
	menu.AppendMenu(MF_STRING | (d && !d->Service.empty() ? 0 : MF_GRAYED), ID_DEVICE_GOTODRIVER, L"Go to &Driver");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (d ? 0 : MF_GRAYED), ID_DEVICE_COPYID, L"Copy &Instance ID");
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_DEVICE_SCANHARDWARE, L"&Scan for Hardware Changes");
	menu.SetMenuDefaultItem(ID_DEVICE_PROPERTIES);
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
}

LRESULT CDevicesView::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& bHandled) {
	// a device's properties; a class row expands (the TreeListView does that)
	if (auto d = GetSelectedDevice(); d && d->Depth > 0) {
		BOOL handled;
		OnProperties(0, 0, nullptr, handled);
	}
	else
		bHandled = FALSE;
	return 0;
}

LRESULT CDevicesView::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto d = GetSelectedDevice();
	if (!d || d->InstanceId.empty())
		return 0;
	// Device Manager's property sheet for the device
	auto args = std::format(L"devmgr.dll,DeviceProperties_RunDLL /DeviceID {}", d->InstanceId);
	::ShellExecute(m_hWnd, nullptr, L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CDevicesView::OnGoToDriver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto d = GetSelectedDevice();
	if (!d || d->Service.empty())
		return 0;
	auto config = ServiceManager::GetServiceConfiguration(d->Service);
	auto path = KernelModules::DriverImagePath(d->Service, config ? config->BinaryPathName : std::wstring());
	Frame()->GoToDriver(path);
	return 0;
}

LRESULT CDevicesView::OnCopyId(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto d = GetSelectedDevice())
		ClipboardHelper::CopyText(m_hWnd, d->InstanceId.c_str());
	return 0;
}

LRESULT CDevicesView::OnEnable(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto d = GetSelectedDevice();
	if (!d || !d->Present || d->Depth == 0)
		return 0;
	auto device = *d;	// a scan may replace m_Devices meanwhile
	bool enable = device.IsDisabled();
	if (!enable) {
		// the classes without which the machine may not run, or can't be used to enable the device again
		static const PCWSTR critical[] = {
			L"System", L"Computer", L"Processor", L"DiskDrive", L"HDC", L"SCSIAdapter", L"Volume", L"Display", L"Keyboard",
			L"Mouse", L"HIDClass", L"USB", L"Net", L"Firmware", L"SecurityDevices",
		};
		bool risky = std::ranges::any_of(critical, [&](auto c) { return ::_wcsicmp(c, device.Class.c_str()) == 0; });
		auto text = std::format(L"Disable {}?\n\nIt stops working until it's enabled again, also after a restart.", device.Name);
		if (risky)
			text += std::format(L"\n\nIt's one of the {}, which Windows or your input may depend on: disabling it can leave the computer "
				L"unusable, or unable to start, until it's enabled again.", device.ClassName);
		if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | (risky ? MB_ICONWARNING : MB_ICONQUESTION) | MB_DEFBUTTON2) != IDOK)
			return 0;
	}
	CWaitCursor wait;
	auto result = Devices::SetEnabled(device.InstanceId, enable);
	if (result == DeviceChangeResult::Failed) {
		auto error = ::GetLastError();
		auto text = std::format(L"{} {} failed: {}", enable ? L"Enabling" : L"Disabling", device.Name, Helpers::GetErrorText(error));
		if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
			text += L"\n\nChanging a device needs administrator rights.";
		AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
	}
	else if (result == DeviceChangeResult::RestartNeeded)
		AtlMessageBox(m_hWnd, std::format(L"{} is {} after a restart: it can't be changed while it's in use.", device.Name,
			enable ? L"enabled" : L"disabled").c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
	StartScan();
	return 0;
}

LRESULT CDevicesView::OnScanHardware(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!Devices::ScanForChanges()) {
		auto error = ::GetLastError();
		auto text = L"Scanning for hardware changes failed: " + Helpers::GetErrorText(error);
		if (!SecurityHelper::IsRunningElevated())
			text += L"\n\nIt needs administrator rights.";
		AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
	}
	// what it finds comes as device notifications, which scan the tree again
	return 0;
}

LRESULT CDevicesView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row)
		return 0;
	std::wstring text;
	for (auto& column : GetShownColumns()) {
		if (!text.empty())
			text += L'\t';
		text += GetColumnText(*row, (Column)column.Id);
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

std::vector<ColumnLayout::ShownColumn> CDevicesView::GetShownColumns() const {
	int count = (int)m_Columns.size();
	std::vector<int> order(count);
	m_Tree.GetHeader().GetOrderArray(count, order.data());
	std::vector<ColumnLayout::ShownColumn> columns;
	auto dpi = DpiHelper::GetDpi(m_Tree);
	for (int index : order)
		columns.push_back({ (int)m_Columns[index], DpiHelper::Scale(m_Tree.GetColumnWidth(index), USER_DEFAULT_SCREEN_DPI, dpi) });
	return columns;
}

void CDevicesView::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	m_Tree.SetSortColumn(-1, 0);
	while (m_Tree.GetColumnCount() > 1)
		m_Tree.DeleteColumn(m_Tree.GetColumnCount() - 1);
	m_Columns = { Column::Name };
	for (auto& c : columns)
		if ((Column)c.Id != Column::Name)
			m_Columns.push_back((Column)c.Id);

	for (int i = 0; i < (int)m_Columns.size(); i++) {
		auto& def = s_Columns[(int)m_Columns[i]];
		auto shown = std::ranges::find(columns, (int)m_Columns[i], &ColumnLayout::ShownColumn::Id);
		int width = DpiHelper::Scale(shown != columns.end() ? shown->Width : def.Width, m_Tree.m_hWnd);
		if (i == 0 && m_Tree.GetColumnCount() == 1)
			m_Tree.SetColumnWidth(0, width);
		else
			m_Tree.InsertColumn(i, def.Name, width, def.Format);
		m_Tree.SetColumnSortable(i, true);
	}

	std::vector<int> order;
	for (auto& c : columns)
		order.push_back((int)(std::ranges::find(m_Columns, (Column)c.Id) - m_Columns.begin()));
	m_Tree.GetHeader().SetOrderArray((int)order.size(), order.data());

	auto sorted = std::ranges::find(m_Columns, (Column)m_SortColumn);
	if (m_SortColumn >= 0 && sorted != m_Columns.end())
		m_Tree.SetSortColumn((int)(sorted - m_Columns.begin()), m_SortAscending ? 1 : -1);
	else
		m_SortColumn = -1;
	m_Tree.Invalidate();
}

void CDevicesView::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.DeviceColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CDevicesView::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
	}
	return 0;
}

LRESULT CDevicesView::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CDevicesView::OnResetColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto columns = ColumnLayout::Parse(L"", s_Columns);
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}
