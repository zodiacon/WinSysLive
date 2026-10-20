#include "pch.h"
#include "ProcessInfoEx.h"
#include "Interfaces.h"
#include "HandlesPage.h"
#include "AccessMask.h"
#include "AppSettings.h"
#include "ViewBase.h"
#include <WinSys/Helpers.h>
#include <WinSys/Sid.h>
#include <WTLHelper.h>

using namespace WinSys;

namespace {
	// in CHandlesPage::Column order; the keys are stored in the HandleColumns setting
	const ColumnLayout::ColumnDef s_Columns[] = {
		{ L"Handle", L"Handle", 80, LVCFMT_RIGHT, true, true },
		{ L"Type", L"Type", 120, LVCFMT_LEFT, true },
		{ L"Name / Details", L"Name", 400, LVCFMT_LEFT, true },
		{ L"Address", L"Address", 140, LVCFMT_RIGHT, false },	// of the kernel object; the system handle table: costly
		{ L"Access", L"Access", 90, LVCFMT_RIGHT, true },
		{ L"Decoded Access", L"DecodedAccess", 300, LVCFMT_LEFT, true },
		{ L"Attributes", L"Attributes", 110, LVCFMT_LEFT, true },
	};

	// highlighting of opened handles lasts this many refreshes
	const int NewHandleTicks = 2;
	// when this many name queries have hung (their workers are stuck), files aren't queried anymore
	const int MaxHungQueries = 4;

	std::wstring RegistryName(std::wstring name) {
		// \REGISTRY\MACHINE\... and \REGISTRY\USER\<SID>\... as the registry editor shows them
		static const std::wstring currentUser = [] {
			std::wstring sid;
			HANDLE hToken;
			if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
				BYTE buffer[256];
				DWORD len;
				if (NT_SUCCESS(::NtQueryInformationToken(hToken, TokenUser, buffer, sizeof(buffer), &len)))
					sid = L"\\REGISTRY\\USER\\" + Sid(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid).AsString();
				::CloseHandle(hToken);
			}
			return sid;
		}();
		auto replace = [&](std::wstring const& prefix, PCWSTR with) {
			if (prefix.empty() || ::_wcsnicmp(name.c_str(), prefix.c_str(), prefix.size()) != 0)
				return false;
			// the whole prefix or up to a backslash, not part of a longer name
			if (name.size() > prefix.size() && name[prefix.size()] != L'\\')
				return false;
			name = with + name.substr(prefix.size());
			return true;
		};
		// the first that matches
		(void)(replace(currentUser + L"_Classes", L"HKCU\\Software\\Classes") || replace(currentUser, L"HKCU")
			|| replace(L"\\REGISTRY\\MACHINE", L"HKLM") || replace(L"\\REGISTRY\\USER", L"HKU"));
		return name;
	}
}

void CHandlesPage::SetFrame(IMainFrame* frame) {
	m_Frame = frame;
}

LRESULT CHandlesPage::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
		LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP | LVS_EX_SUBITEMIMAGES);
	// the type icons, in the leftmost column
	if (m_Frame)
		m_List.SetImageList(m_Frame->GetImageList(), LVSIL_SMALL);
	SuspendHookWhilePainting(m_List);
	SetColumns(ColumnLayout::Parse(AppSettings::Get().HandleColumns(), s_Columns));
	return 0;
}

LRESULT CHandlesPage::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the widths and the order also change by dragging in the header
	SaveColumns();
	bHandled = FALSE;
	return 0;
}

LRESULT CHandlesPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_List)
		m_List.MoveWindow(0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

LRESULT CHandlesPage::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.SetFocus();
	return 0;
}

LRESULT CHandlesPage::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// the list covers the page
}

void CHandlesPage::SetProcess(std::shared_ptr<ProcessInfoEx> process, bool enumerate) {
	if (process == m_Process)
		return;
	m_Process = std::move(process);
	// the Idle process has no handles
	m_hProcess.reset(m_Process && m_Process->Id ? ::OpenProcess(PROCESS_DUP_HANDLE, FALSE, m_Process->Id) : nullptr);
	m_Handles.clear();
	m_FirstEnum = true;
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (enumerate)
		Refresh();
	else {
		m_Rows.clear();
		m_List.SetItemCount(0);
	}
	if (m_List.GetItemCount() > 0)
		m_List.EnsureVisible(0, FALSE);
}

bool CHandlesPage::ShowsAddresses() const {
	return std::ranges::contains(GetShownColumns(), (int)Column::Address, &ColumnLayout::ShownColumn::Id);
}

void CHandlesPage::Refresh() {
	int selected = m_List.GetSelectedIndex();
	uint64_t selectedValue = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : 0;

	m_Rows.clear();
	if (m_Process && m_Process->Id) {
		auto handles = ShowsAddresses() ? m_Enumerator.EnumProcessHandlesWithAddresses(m_Process->Id)
			: HandleEnumerator::EnumProcessHandles(m_Process->Id);
		if (handles) {
			decltype(m_Handles) current;
			current.reserve(handles->size());
			m_Rows.reserve(handles->size());
			for (auto& h : *handles) {
				// the same handle as before, unless the value was reused (for another object, or with other access)
				std::shared_ptr<HandleData> data;
				if (auto it = m_Handles.find(h.Value); it != m_Handles.end() && it->second->Entry.TypeIndex == h.TypeIndex
					&& it->second->Entry.Access == h.Access && (!h.Object || !it->second->Entry.Object || h.Object == it->second->Entry.Object)) {
					data = std::move(it->second);
					m_Handles.erase(it);
					data->Entry.Attributes = h.Attributes;
					if (h.Object)
						data->Entry.Object = h.Object;
				}
				else {
					data = std::make_shared<HandleData>(h);
					if (!m_FirstEnum)
						data->NewTicks = NewHandleTicks;
				}
				HandleRow row{ data };
				if (data->NewTicks > 0) {
					row.New = true;
					data->NewTicks--;
				}
				current.insert({ h.Value, data });
				m_Rows.push_back(std::move(row));
			}
			// the handles left were closed: shown once more
			for (auto& [value, data] : m_Handles)
				m_Rows.push_back({ data, false, true });
			m_Handles = std::move(current);
			m_FirstEnum = false;
		}
	}

	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectHandle(selectedValue);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}

void CHandlesPage::SelectHandle(uint64_t value) {
	// the list view keeps the selection by index, which the new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	if (value == 0)
		return;
	auto it = std::ranges::find_if(m_Rows, [=](auto& r) { return r.Data->Entry.Value == value && !r.Closed; });
	if (it != m_Rows.end())
		m_List.SetItemState((int)(it - m_Rows.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

std::wstring const& CHandlesPage::GetName(HandleData& data, bool closed) const {
	if (!data.Name)
		data.Name = closed ? L"" : QueryName(data.Entry);
	return *data.Name;
}

std::wstring CHandlesPage::QueryName(HandleEntry const& entry) const {
	if (!m_hProcess)
		return L"";
	auto& type = ObjectTypes::GetName(entry.TypeIndex);
	if (type == L"File" && m_NameQuery.GetAbandonedCount() >= MaxHungQueries)
		return L"";

	// a copy of the handle in this process. Processes and threads are queried for their IDs, which the handle's own
	// access may not allow, so they're duplicated with the access that needs first
	wil::unique_handle h;
	auto source = (HANDLE)(ULONG_PTR)entry.Value;
	DWORD queryAccess = type == L"Process" ? PROCESS_QUERY_LIMITED_INFORMATION : type == L"Thread" ? THREAD_QUERY_LIMITED_INFORMATION : 0;
	if ((!queryAccess || !::DuplicateHandle(m_hProcess.get(), source, ::GetCurrentProcess(), h.addressof(), queryAccess, FALSE, 0))
		&& !::DuplicateHandle(m_hProcess.get(), source, ::GetCurrentProcess(), h.addressof(), 0, FALSE, DUPLICATE_SAME_ACCESS))
		return L"";

	// processes and threads have no names: their IDs and image names are more useful
	auto processText = [&](DWORD pid, HANDLE hProcess) {
		std::wstring name = m_Frame ? m_Frame->GetProcessManager().GetProcessNameById(pid) : L"";
		if (name.empty() && hProcess) {
			// not in the snapshot (it may have exited): the image path from the handle
			WCHAR path[MAX_PATH];
			DWORD size = _countof(path);
			if (::QueryFullProcessImageName(hProcess, 0, path, &size)) {
				auto slash = wcsrchr(path, L'\\');
				name = slash ? slash + 1 : path;
			}
		}
		return name.empty() ? std::format(L"PID: {}", pid) : std::format(L"PID: {}, {}", pid, name);
	};
	if (type == L"Process") {
		if (auto pid = ::GetProcessId(h.get()))
			return processText(pid, h.get());
		return L"";
	}
	if (type == L"Thread") {
		if (auto tid = ::GetThreadId(h.get()))
			return std::format(L"TID: {}, {}", tid, processText(::GetProcessIdOfThread(h.get()), nullptr));
		return L"";
	}
	if (type == L"Token") {
		// the user, and the logon session
		BYTE buffer[256];
		DWORD len;
		TOKEN_STATISTICS stats;
		if (NT_SUCCESS(::NtQueryInformationToken(h.get(), TokenUser, buffer, sizeof(buffer), &len))
			&& NT_SUCCESS(::NtQueryInformationToken(h.get(), TokenStatistics, &stats, sizeof(stats), &len)))
			return std::format(L"{}:{:x}", Sid(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid).UserName(), stats.AuthenticationId.LowPart);
		return L"";
	}

	auto name = m_NameQuery.GetName(h.get());
	if (!name || name->empty())
		return L"";
	if (type == L"File")
		return Helpers::GetDosNameFromNtName(name->c_str());
	if (type == L"Key")
		return RegistryName(std::move(*name));
	return std::move(*name);
}

CString CHandlesPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

std::wstring CHandlesPage::GetText(HandleRow const& row, Column column) const {
	auto& h = row.Data->Entry;
	switch (column) {
		case Column::Handle: return std::format(L"0x{:X}", h.Value);
		case Column::Type: return ObjectTypes::GetName(h.TypeIndex);
		case Column::Name: return GetName(*row.Data, row.Closed);
		case Column::Address: return h.Object ? std::format(L"0x{:X}", (ULONG_PTR)h.Object) : L"";
		case Column::Access: return std::format(L"0x{:08X}", h.Access);
		case Column::DecodedAccess: return AccessMask::Decode(ObjectTypes::GetName(h.TypeIndex), h.Access);
		case Column::Attributes:
		{
			std::wstring text;
			if (h.Attributes & OBJ_INHERIT)
				text = L"Inherit";
			if (h.Attributes & 0x1)	// OBJ_PROTECT_CLOSE
				text += text.empty() ? L"Protect" : L", Protect";
			return text;
		}
	}
	return L"";
}

int CHandlesPage::GetRowImage(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size() || !m_Frame || !ColumnLayout::IsLeftmostColumn(m_List, col))
		return -1;
	auto& r = m_Rows[row];
	auto& type = ObjectTypes::GetName(r.Data->Entry.TypeIndex);
	if (type == L"File") {
		// devices and pipes keep their NT names; files on drives get DOS paths
		auto& name = GetName(*r.Data, r.Closed);
		return m_Frame->GetIconIndex(name.starts_with(L"\\") ? IDI_DEVICE : IDI_FILE);
	}
	return m_Frame->GetIconIndex(GetTypeIcon(type));
}

UINT CHandlesPage::GetTypeIcon(std::wstring const& type) {
	static const std::unordered_map<std::wstring, UINT> icons{
		{ L"Event", IDI_EVENT }, { L"Mutant", IDI_MUTEX }, { L"Semaphore", IDI_SEMAPHORE },
		{ L"Timer", IDI_TIMER }, { L"IRTimer", IDI_TIMER }, { L"Key", IDI_KEY }, { L"ALPC Port", IDI_ALPC },
		{ L"Desktop", IDI_DESKTOP }, { L"WindowStation", IDI_WINSTA }, { L"Composition", IDI_GRAPHICS },
		{ L"Directory", IDI_DIRECTORY }, { L"Section", IDI_SECTION }, { L"Session", IDI_SESSION }, { L"Token", IDI_TOKEN }, { L"Thread", IDI_THREADS },
		{ L"Job", IDI_JOB },
		{ L"EtwRegistration", IDI_ETW }, { L"EtwConsumer", IDI_ETW }, { L"EtwSessionDemuxEntry", IDI_ETW },
		{ L"FilterConnectionPort", IDI_PORT }, { L"FilterCommunicationPort", IDI_PORT },
	};
	if (auto it = icons.find(type); it != icons.end())
		return it->second;
	// DxgkSharedResource, DxgkCompositionObject...
	return type.starts_with(L"Dxgk") ? IDI_DIRECTX : IDI_OBJECT;
}

int CHandlesPage::GetSaveColumnRange(HWND, int&) const {
	// Refresh restores the selection (by handle value)
	return -1;
}

void CHandlesPage::DoSort(SortInfo const* si) {
	int selected = m_List.GetSelectedIndex();
	uint64_t selectedValue = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Data->Entry.Value : 0;

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](HandleRow const& r1, HandleRow const& r2) {
		auto& h1 = r1.Data->Entry;
		auto& h2 = r2.Data->Entry;
		int result;
		switch (column) {
			case Column::Handle: result = compare(h1.Value, h2.Value); break;
			case Column::Address: result = compare((ULONG_PTR)h1.Object, (ULONG_PTR)h2.Object); break;
			case Column::Access: result = compare(h1.Access, h2.Access); break;
			case Column::Attributes: result = compare(h1.Attributes, h2.Attributes); break;
			default: result = ::_wcsicmp(GetText(r1, column).c_str(), GetText(r2, column).c_str()); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectHandle(selectedValue);
}

LRESULT CHandlesPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
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
			if (row.Closed) {
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

std::vector<ColumnLayout::ShownColumn> CHandlesPage::GetShownColumns() const {
	return ColumnLayout::GetShownColumns(m_List, GetColumnManager(m_List));
}

void CHandlesPage::SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns) {
	ClearSort(m_List.m_hWnd);
	ColumnLayout::SetColumns(GetColumnManager(m_List), columns, s_Columns);
}

void CHandlesPage::SaveColumns() const {
	auto& settings = AppSettings::Get();
	settings.HandleColumns(ColumnLayout::Format(GetShownColumns(), s_Columns));
	settings.Save();
}

LRESULT CHandlesPage::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
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

LRESULT CHandlesPage::OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_MenuColumn < 0 || s_Columns[m_MenuColumn].Mandatory)
		return 0;
	auto columns = GetShownColumns();
	std::erase_if(columns, [&](auto& c) { return c.Id == m_MenuColumn; });
	m_MenuColumn = -1;
	SetColumns(columns);
	SaveColumns();
	return 0;
}

LRESULT CHandlesPage::OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto columns = ColumnLayout::Select(m_hWnd, GetShownColumns(), s_Columns)) {
		SetColumns(*columns);
		SaveColumns();
		// the addresses come from another source
		Refresh();
	}
	return 0;
}
