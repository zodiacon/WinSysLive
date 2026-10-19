#include "pch.h"
#include "ProcessInfoEx.h"
#include "ModulesPage.h"
#include "FormatHelper.h"
#include "VersionInfo.h"
#include "AppSettings.h"
#include "ViewBase.h"
#include <WTLHelper.h>

using namespace WinSys;

namespace {
	// in CModulesPage::Column order; the keys are stored in the ModuleColumns setting
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Name", L"Name", 200, LVCFMT_LEFT, true, true },
		{ L"Base Address", L"BaseAddress", 140, LVCFMT_RIGHT, true },
		{ L"Size", L"Size", 100, LVCFMT_RIGHT, true },
		{ L"Type", L"Type", 60, LVCFMT_LEFT, true },
		{ L"Image Base", L"ImageBase", 140, LVCFMT_RIGHT, false },
		{ L"Description", L"Description", 250, LVCFMT_LEFT, true },
		{ L"Company", L"Company", 180, LVCFMT_LEFT, true },
		{ L"Version", L"Version", 140, LVCFMT_LEFT, false },
		{ L"Characteristics", L"Characteristics", 200, LVCFMT_LEFT, true },
		{ L"Path", L"Path", 450, LVCFMT_LEFT, true },
	};

	// highlighting of loaded modules lasts this many refreshes
	const int NewModuleTicks = 2;

	std::wstring CharacteristicsToString(DllCharacteristics c) {
		static const struct {
			DllCharacteristics Flag;
			PCWSTR Name;
		} flags[] = {
			{ DllCharacteristics::DynamicBase, L"ASLR" },
			{ DllCharacteristics::HighEntropyVA, L"High Entropy VA" },
			{ DllCharacteristics::NxCompat, L"DEP" },
			{ DllCharacteristics::ControlFlowGuard, L"CFG" },
			{ DllCharacteristics::ForceIntegrity, L"Force Integrity" },
			{ DllCharacteristics::AppContainer, L"AppContainer" },
			{ DllCharacteristics::NoSEH, L"No SEH" },
		};
		std::wstring text;
		for (auto& f : flags) {
			if ((c & f.Flag) == f.Flag) {
				if (!text.empty())
					text += L", ";
				text += f.Name;
			}
		}
		return text;
	}

	FileVersionInfo const& VersionOf(ModuleInfo const& m) {
		// data files (.mui, fonts...) aren't images; their version resource isn't worth the file I/O
		static FileVersionInfo const none;
		return m.Type == MapType::Image ? GetCachedVersionInfo(m.Path) : none;
	}
}

LRESULT CModulesPage::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP);
	SuspendHookWhilePainting(m_List);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().ModuleColumns(), s_Columns));
	return 0;
}

LRESULT CModulesPage::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

LRESULT CModulesPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_List)
		m_List.MoveWindow(0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

LRESULT CModulesPage::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.SetFocus();
	return 0;
}

LRESULT CModulesPage::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// the list covers the page
}

void CModulesPage::SetProcess(std::shared_ptr<ProcessInfoEx> process) {
	if (process == m_Process)
		return;
	m_Process = std::move(process);
	// Idle and System have no user-mode address space
	m_Tracker = m_Process && m_Process->Id > 4 ? std::make_unique<ProcessModuleTracker>(m_Process->Id) : nullptr;
	m_NewTicks.clear();
	m_FirstEnum = true;
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	Refresh();
	if (m_List.GetItemCount() > 0)
		m_List.EnsureVisible(0, FALSE);
}

void CModulesPage::Refresh() {
	int selected = m_List.GetSelectedIndex();
	void* selectedBase = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Module->Base : nullptr;

	m_Rows.clear();
	if (m_Tracker) {
		m_Tracker->EnumModules();
		if (!m_FirstEnum)
			for (auto& m : m_Tracker->GetNewModules())
				m_NewTicks[m.get()] = NewModuleTicks;
		m_FirstEnum = false;

		auto& modules = m_Tracker->GetModules();
		m_Rows.reserve(modules.size());
		for (auto& m : modules) {
			// mapped sections without a file (backed by the page file) aren't modules
			if (m->Path.empty())
				continue;
			ModuleRow row{ m };
			if (auto it = m_NewTicks.find(m.get()); it != m_NewTicks.end()) {
				row.New = true;
				if (--it->second == 0)
					m_NewTicks.erase(it);
			}
			m_Rows.push_back(std::move(row));
		}
		for (auto& m : m_Tracker->GetUnloadedModules()) {
			m_NewTicks.erase(m.get());
			if (!m->Path.empty())
				m_Rows.push_back({ m, false, true });
		}
	}

	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectModule(selectedBase);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

void CModulesPage::SelectModule(void* base) {
	// the list view keeps the selection by index, which the new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (base == nullptr)
		return;
	auto it = std::ranges::find_if(m_Rows, [=](auto& r) { return r.Module->Base == base && !r.Unloaded; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

CString CModulesPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

std::wstring CModulesPage::GetText(ModuleRow const& row, Column column) const {
	auto& m = *row.Module;
	switch (column) {
		case Column::Name: return m.Name;
		case Column::BaseAddress: return std::format(L"0x{:X}", (ULONG_PTR)m.Base);
		case Column::Size: return FormatHelper::FormatSize(m.ModuleSize);
		case Column::Type: return m.Type == MapType::Image ? L"Image" : L"Data";
		case Column::ImageBase: return m.ImageBase ? std::format(L"0x{:X}", (ULONG_PTR)m.ImageBase) : L"";
		case Column::Description: return VersionOf(m).Description;
		case Column::Company: return VersionOf(m).Company;
		case Column::Version: return VersionOf(m).Version;
		case Column::Characteristics: return m.Type == MapType::Image ? CharacteristicsToString(m.Characteristics) : L"";
		case Column::Path: return m.Path;
	}
	return L"";
}

int CModulesPage::GetSaveColumnRange(HWND, int&) const {
	// Refresh restores the selection (by base address)
	return -1;
}

void CModulesPage::DoSort(SortInfo const* si) {
	int selected = m_List.GetSelectedIndex();
	void* selectedBase = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Module->Base : nullptr;

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](ModuleRow const& r1, ModuleRow const& r2) {
		auto& m1 = *r1.Module;
		auto& m2 = *r2.Module;
		int result;
		switch (column) {
			case Column::BaseAddress: result = compare((ULONG_PTR)m1.Base, (ULONG_PTR)m2.Base); break;
			case Column::Size: result = compare(m1.ModuleSize, m2.ModuleSize); break;
			case Column::ImageBase: result = compare((ULONG_PTR)m1.ImageBase, (ULONG_PTR)m2.ImageBase); break;
			default: result = ::_wcsicmp(GetText(r1, column).c_str(), GetText(r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectModule(selectedBase);
}

LRESULT CModulesPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto& row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and exited processes in the process list
			if (row.Unloaded) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.New) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

std::vector<ColumnLayout::ShownColumn> CModulesPage::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CModulesPage::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CModulesPage::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.ModuleColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CModulesPage::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	auto column = ColumnLayout::HeaderColumnAt(m_List, GetColumnManager(m_List), pt);
	if (!column) {
		bHandled = FALSE;
		return 0;
	}
	m_MenuColumn = *column;
	CMenu menu;
	// the commands are posted to this page (see CThreadsPage::OnContextMenu)
	::TrackPopupMenuEx(ColumnLayout::PrepareHeaderMenu(menu, m_MenuColumn, s_Columns), TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd, nullptr);
	return 0;
}

LRESULT CModulesPage::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CModulesPage::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
	}
	return 0;
}
