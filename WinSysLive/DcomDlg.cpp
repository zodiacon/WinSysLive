#include "pch.h"
#include "DcomDlg.h"
#include "ComPermissionInfo.h"
#include "RegistryEditor.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <thread>

using namespace WinSys;

namespace {
	constexpr uint32_t ErrorDays = 30;

	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	std::wstring Upper(std::wstring text) {
		::CharUpperBuff(text.data(), (DWORD)text.size());
		return text;
	}

	PCWSTR ViewText(bool wow64) {
		return wow64 ? L"32-bit" : L"64-bit";
	}

	// Run As values that aren't a stored account: the interactive user, the launching user, the service accounts
	bool IsStoredAccount(std::wstring const& runAs) {
		if (runAs.empty())
			return false;
		static const PCWSTR builtIn[] = { L"Interactive User", L"nt authority\\system", L"nt authority\\localservice",
			L"nt authority\\networkservice", L"nt authority\\local service", L"nt authority\\network service", L"SYSTEM" };
		for (auto b : builtIn)
			if (::_wcsicmp(runAs.c_str(), b) == 0)
				return false;
		return true;
	}

	// a launch permission grants a remote right (launch, activation) to anyone who can log on; an access permission
	// grants remote access to Anonymous (Windows' own machine-wide default gives Everyone remote access, so that alone
	// says little)
	bool GrantsBroadRemote(std::vector<uint8_t> const& sd, bool launch) {
		if (sd.empty())
			return false;
		for (auto& e : ComSecurity::ParsePermission(sd.data()))
			if (ComSecurity::GrantsRemote(e) && (launch ? ComSecurity::IsBroadAccount(e.Sid) : e.Sid == L"S-1-5-7"))
				return true;
		return false;
	}

	// the {...} GUIDs in a text, upper case
	std::vector<std::wstring> GuidsIn(std::wstring const& text) {
		std::vector<std::wstring> guids;
		for (size_t at = text.find(L'{'); at != std::wstring::npos; at = text.find(L'{', at + 1)) {
			if (at + 38 > text.size() || text[at + 37] != L'}')
				continue;
			auto guid = text.substr(at, 38);
			bool valid = true;
			for (int i = 1; i < 37 && valid; i++)
				valid = (i == 9 || i == 14 || i == 19 || i == 24) ? guid[i] == L'-' : iswxdigit(guid[i]) != 0;
			if (valid)
				guids.push_back(Upper(guid));
		}
		return guids;
	}

	std::wstring AccountOf(std::wstring const& sidText) {
		PSID sid = nullptr;
		if (!::ConvertStringSidToSid(sidText.c_str(), &sid))
			return L"";
		WCHAR name[256], domain[256];
		DWORD nameSize = _countof(name), domainSize = _countof(domain);
		SID_NAME_USE use;
		std::wstring result;
		if (::LookupAccountSid(nullptr, sid, name, &nameSize, domain, &domainSize, &use))
			result = *domain ? std::wstring(domain) + L"\\" + name : std::wstring(name);
		::LocalFree(sid);
		return result;
	}
}

LRESULT CDcomDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_COM, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_COM, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_Tabs.Attach(GetDlgItem(IDC_DC_TABS));
	int index = 0;
	for (auto name : { L"Machine", L"App IDs", L"Elevation", L"DCOM Errors", L"Running Objects" })
		m_Tabs.InsertItem(index++, name);
	m_List.Attach(GetDlgItem(IDC_DC_LIST));
	m_Details.Attach(GetDlgItem(IDC_DC_DETAILS));
	// above the tab control, which would otherwise paint over them (see the Network Information dialog)
	for (auto id : { IDC_DC_FILTER, IDC_DC_LIST, IDC_DC_DETAILS })
		GetDlgItem(id).SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	for (auto list : { &m_List, &m_Details }) {
		list->SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		ListViewHelper::EnableColumnDpiScaling(*list);
	}
	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_Details.InsertColumn(0, L"Property", LVCFMT_LEFT, ::MulDiv(300, dpi, 96));
	m_Details.InsertColumn(1, L"Value", LVCFMT_LEFT, ::MulDiv(640, dpi, 96));
	::SendDlgItemMessage(m_hWnd, IDC_DC_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by any column");

	using K = Table::Kind;
	m_Tables[(int)Tab::Machine].Columns = { { L"Setting", 230 }, { L"Value", 260 }, { L"Description", 520 } };
	m_Tables[(int)Tab::Machine].SortColumn = -1;	// dcomcnfg's order
	m_Tables[(int)Tab::AppIds].Columns = { { L"Name", 220 }, { L"App ID", 270 }, { L"Registered", 75 },
		{ L"Run As", 140 }, { L"Service", 130 }, { L"Surrogate", 80 }, { L"Authentication", 120 }, { L"Launch", 75 },
		{ L"Access", 75 }, { L"Classes", 60, K::Number }, { L"Executables", 160 }, { L"Remote Server", 100 },
		{ L"Last Changed", 140, K::Time }, { L"Concern", 300 } };
	m_Tables[(int)Tab::Elevation].Columns = { { L"Name", 260 }, { L"CLSID", 270 }, { L"View", 55 }, { L"Without a Prompt", 110 },
		{ L"Type", 120 }, { L"Server", 360 }, { L"App ID", 200 }, { L"Concern", 260 } };
	m_Tables[(int)Tab::Errors].Columns = { { L"Time", 140, K::Time }, { L"Event ID", 60, K::Number }, { L"Level", 70 },
		{ L"Class", 220 }, { L"App ID", 200 }, { L"User", 160 }, { L"Message", 700 } };
	m_Tables[(int)Tab::Errors].SortColumn = 0;
	m_Tables[(int)Tab::Errors].SortAscending = false;	// the newest first
	m_Tables[(int)Tab::RunningObjects].Columns = { { L"Display Name", 520 }, { L"Moniker", 90 }, { L"Last Change", 140, K::Time },
		{ L"Custom Moniker's Class", 320 } };
	m_CopyList = m_List;

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	ShowTab();
	Load();
	return TRUE;
}

void CDcomDlg::Load() {
	if (m_Load)
		return;
	GetDlgItem(IDC_DC_REFRESH).EnableWindow(FALSE);
	SetDlgItemText(IDC_DC_STATUS, L"Reading the DCOM settings, the app IDs, the classes and the System log...");
	// the thread holds the state, not the dialog, so closing needn't wait for it
	m_Load = std::make_shared<LoadState>();
	std::thread([state = m_Load, hWnd = m_hWnd] {
		auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		auto settings = ComSecurity::GetMachineSettings();
		auto appIds = ComRegistry::EnumAppIds();
		auto classes = ComRegistry::EnumClasses(false);
		auto wow64 = ComRegistry::EnumClasses(true);
		// a loop: a range insert's exception handling warns (C4530) where the app is built without exceptions
		classes.reserve(classes.size() + wow64.size());
		for (auto& c : wow64)
			classes.push_back(std::move(c));
		auto errors = EventLog::Query(L"System", 4, ErrorDays * 24, 2000, EventDataRead::All, L"Microsoft-Windows-DistributedCOM");
		auto errorsError = errors ? 0 : ::GetLastError();
		auto objects = ComSecurity::EnumRunningObjects();
		auto objectsError = objects ? 0 : ::GetLastError();
		if (SUCCEEDED(com))
			::CoUninitialize();
		{
			std::lock_guard lock(state->Lock);
			state->Settings = std::move(settings);
			state->AppIds = std::move(appIds);
			state->Classes = std::move(classes);
			state->Errors = std::move(errors);
			state->ErrorsError = errorsError;
			state->Objects = std::move(objects);
			state->ObjectsError = objectsError;
		}
		if (::IsWindow(hWnd))
			::PostMessage(hWnd, WM_DCOM_READ, 0, 0);
		}).detach();
}

LRESULT CDcomDlg::OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (!m_Load)
		return 0;
	{
		std::lock_guard lock(m_Load->Lock);
		m_Settings = std::move(m_Load->Settings);
		m_AppIds = std::move(m_Load->AppIds);
		m_Classes = std::move(m_Load->Classes);
		m_Errors = m_Load->Errors ? std::move(*m_Load->Errors) : std::vector<EventLogEntry>();
		m_ErrorsError = m_Load->ErrorsError;
		m_Objects = m_Load->Objects ? std::move(*m_Load->Objects) : std::vector<RunningObject>();
		m_ObjectsError = m_Load->ObjectsError;
	}
	m_Load.reset();
	m_Loaded = true;
	GetDlgItem(IDC_DC_REFRESH).EnableWindow(TRUE);
	BuildRows();
	ShowRows();
	return 0;
}

void CDcomDlg::BuildRows() {
	// the names by ID: the 64-bit view's, else the 32-bit's (the classes come in that order); HKCR's (the user's over
	// the machine's)
	m_ClassByClsid.clear();
	m_ClassesByAppId.clear();
	for (int i = 0; i < (int)m_Classes.size(); i++) {
		auto& c = m_Classes[i];
		if (c.OverriddenByUser)
			continue;
		m_ClassByClsid.try_emplace(c.Clsid, i);
		// a class in both views once
		if (!c.AppId.empty()) {
			auto& list = m_ClassesByAppId[c.AppId];
			if (std::ranges::none_of(list, [&](int other) { return m_Classes[other].Clsid == c.Clsid; }))
				list.push_back(i);
		}
	}
	m_AppIdById.clear();
	for (int i = 0; i < (int)m_AppIds.size(); i++) {
		auto& a = m_AppIds[i];
		if (a.PerUser || !m_AppIdById.contains(a.AppId))
			m_AppIdById[a.AppId] = i;
	}
	BuildMachine();
	BuildAppIds();
	BuildElevation();
	BuildErrors();
	BuildObjects();
}

void CDcomDlg::BuildMachine() {
	auto& t = m_Tables[(int)Tab::Machine];
	t.Rows.clear();
	t.Concerns.clear();
	t.Times.clear();
	for (auto& s : m_Settings) {
		t.Rows.push_back({ s.Name, s.Value, s.Description });
		t.Concerns.push_back(s.Weak ? Concern::Warning : Concern::None);
		t.Times.push_back(0);
	}
}

void CDcomDlg::BuildAppIds() {
	auto& t = m_Tables[(int)Tab::AppIds];
	t.Rows.clear();
	t.Concerns.clear();
	t.Times.clear();
	for (auto& a : m_AppIds) {
		std::wstring concern;
		auto level = Concern::None;
		auto add = [&](PCWSTR text, Concern c) {
			concern += (concern.empty() ? L"" : L"; ") + std::wstring(text);
			level = (std::max)(level, c);
		};
		if (a.AuthenticationLevel == 1)
			add(L"No authentication (level None)", Concern::Danger);
		if (GrantsBroadRemote(a.LaunchPermission, true))
			add(L"Remote launch or activation for anyone who can log on", Concern::Warning);
		if (GrantsBroadRemote(a.AccessPermission, false))
			add(L"Remote access for Anonymous", Concern::Warning);
		if (IsStoredAccount(a.RunAs))
			add(L"Runs as a stored account", Concern::Note);
		if (a.PerUser)
			add(a.OverridesMachine ? L"Per user, over the machine's" : L"Per user", Concern::Note);
		auto classes = m_ClassesByAppId.find(a.AppId);
		std::wstring executables;
		for (auto& e : a.Executables)
			executables += (executables.empty() ? L"" : L", ") + e;
		t.Rows.push_back({ a.Name, a.AppId, a.PerUser ? L"User" : L"Machine", a.RunAs, a.ServiceName,
			a.Surrogate ? (a.DllSurrogate.empty() ? L"dllhost" : a.DllSurrogate) : L"",
			a.AuthenticationLevel < 0 ? L"" : ComSecurity::AuthenticationLevelText(a.AuthenticationLevel),
			a.LaunchPermission.empty() ? L"Default" : L"Set", a.AccessPermission.empty() ? L"Default" : L"Set",
			classes == m_ClassesByAppId.end() ? L"" : std::to_wstring(classes->second.size()), executables, a.RemoteServerName,
			FormatHelper::FormatDateTime(a.LastWrite), concern });
		t.Concerns.push_back(level);
		t.Times.push_back(a.LastWrite);
	}
}

void CDcomDlg::BuildElevation() {
	auto& t = m_Tables[(int)Tab::Elevation];
	t.Rows.clear();
	t.Concerns.clear();
	t.Times.clear();
	m_Elevated.clear();
	for (int i = 0; i < (int)m_Classes.size(); i++) {
		auto& c = m_Classes[i];
		if (!c.ElevationEnabled || c.OverriddenByUser)
			continue;
		std::wstring concern;
		auto level = Concern::None;
		if (c.ServerWritable) {
			concern = c.ServerMissing ? L"Missing server a standard user can create" : L"A standard user can replace the server";
			level = Concern::Danger;
		}
		else if (c.ServerMissing)
			concern = L"Missing server";
		if (c.PerUser) {
			concern += (concern.empty() ? L"" : L"; ") + std::wstring(L"Per user");
			level = (std::max)(level, Concern::Note);
		}
		auto appId = c.AppId.empty() ? L"" : !c.AppIdName.empty() ? c.AppIdName + L" " + c.AppId : c.AppId;
		t.Rows.push_back({ c.Name, c.Clsid, ViewText(c.Wow64), c.AutoApproved ? L"Yes" : L"", ComRegistry::TypeText(c.Type),
			c.Server, appId, concern });
		t.Concerns.push_back(level);
		t.Times.push_back(0);
		m_Elevated.push_back(i);
	}
}

void CDcomDlg::BuildErrors() {
	auto& t = m_Tables[(int)Tab::Errors];
	t.Rows.clear();
	t.Concerns.clear();
	t.Times.clear();
	for (auto& e : m_Errors) {
		// the class and the app ID among the event's data (10016: the CLSID, then the APPID); else in the message
		std::wstring clsid, appId, user;
		auto take = [&](std::wstring const& text) {
			for (auto& guid : GuidsIn(text)) {
				if (clsid.empty() && m_ClassByClsid.contains(guid))
					clsid = guid;
				else if (appId.empty() && m_AppIdById.contains(guid))
					appId = guid;
				else if (clsid.empty())
					clsid = guid;	// an unregistered class (a failed activation's usual cause)
			}
		};
		for (auto& d : e.Data) {
			take(d.Value);
			if (user.empty() && d.Value.starts_with(L"S-1-"))
				user = AccountOf(d.Value);
		}
		if (clsid.empty() && appId.empty())
			take(e.Message);
		auto named = [](std::wstring const& id, std::wstring const& name) {
			return id.empty() ? std::wstring() : name.empty() ? id : name + L" " + id;
		};
		auto message = e.Message;
		std::ranges::replace(message, L'\r', L' ');
		std::ranges::replace(message, L'\n', L' ');
		// an event logged without its insertion strings: the message is the template ("The %1 permission settings...")
		if (e.Data.empty() || std::ranges::all_of(e.Data, [](auto& d) { return d.Value.empty(); }))
			message = L"(logged without its details) " + message;
		t.Rows.push_back({ FormatHelper::FormatDateTime(e.Time), std::to_wstring(e.EventId), EventLog::LevelText(e),
			named(clsid, ClassName(clsid)), named(appId, AppIdName(appId)), user, message });
		t.Concerns.push_back(Concern::None);
		t.Times.push_back(e.Time);
	}
}

void CDcomDlg::BuildObjects() {
	auto& t = m_Tables[(int)Tab::RunningObjects];
	t.Rows.clear();
	t.Concerns.clear();
	t.Times.clear();
	for (auto& o : m_Objects) {
		// a system moniker's class says nothing more than its type
		auto custom = o.MonikerType == L"Custom" ? o.MonikerClass : L"";
		if (!custom.empty())
			if (auto name = ClassName(Upper(custom)); !name.empty())
				custom = name + L" " + custom;
		t.Rows.push_back({ o.DisplayName, o.MonikerType, FormatHelper::FormatDateTime(o.LastChange), custom });
		t.Concerns.push_back(Concern::None);
		t.Times.push_back(o.LastChange);
	}
}

std::wstring CDcomDlg::ClassName(std::wstring const& clsid) const {
	auto it = m_ClassByClsid.find(clsid);
	return it == m_ClassByClsid.end() ? L"" : m_Classes[it->second].Name;
}

std::wstring CDcomDlg::AppIdName(std::wstring const& appId) const {
	auto it = m_AppIdById.find(appId);
	return it == m_AppIdById.end() ? L"" : m_AppIds[it->second].Name;
}

CDcomDlg::Tab CDcomDlg::CurrentTab() const {
	auto sel = m_Tabs.GetCurSel();
	return sel >= 0 && sel < (int)Tab::_Count ? (Tab)sel : Tab::Machine;
}

void CDcomDlg::InitColumns(CListViewCtrl& list, Table const& table) {
	list.SetRedraw(FALSE);
	list.DeleteAllItems();
	while (list.DeleteColumn(0))
		;
	auto dpi = ::GetDpiForWindow(list);
	for (int i = 0; i < (int)table.Columns.size(); i++) {
		auto& c = table.Columns[i];
		list.InsertColumn(i, c.Name, c.Sort == Table::Kind::Number ? LVCFMT_RIGHT : LVCFMT_LEFT, ::MulDiv(c.Width, dpi, 96));
	}
	list.SetRedraw(TRUE);
}

void CDcomDlg::ShowTab() {
	m_Filling = true;
	InitColumns(m_List, m_Tables[(int)CurrentTab()]);
	m_Filling = false;
	ShowRows();
}

void CDcomDlg::ShowRows() {
	auto& table = m_Tables[(int)CurrentTab()];
	table.Shown.clear();
	for (int i = 0; i < (int)table.Rows.size(); i++) {
		bool keep = m_Filter.empty();
		for (auto& cell : table.Rows[i])
			if (!keep && Lowercase(cell).find(m_Filter) != std::wstring::npos)
				keep = true;
		if (keep)
			table.Shown.push_back(i);
	}
	m_Filling = true;
	Fill(m_List, table);
	if (m_List.GetItemCount() > 0 && m_List.GetNextItem(-1, LVNI_SELECTED) < 0)
		m_List.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_Filling = false;
	ShowDetails();
}

void CDcomDlg::AddPermissionRows(std::vector<std::pair<std::wstring, std::wstring>>& rows, PCWSTR title,
	std::vector<uint8_t> const& sd, bool launch) {
	auto entries = ComSecurity::ParsePermission(sd.data());
	if (entries.empty()) {
		rows.push_back({ title, L"No entries: no one" });
		return;
	}
	for (auto& e : entries) {
		auto rights = ComSecurity::RightsText(e.Rights, launch);
		if (ComSecurity::GrantsRemote(e) && ComSecurity::IsBroadAccount(e.Sid))
			rights += L"  (remote, for anyone who can log on)";
		rows.push_back({ std::wstring(title) + L": " + (e.Allow ? L"Allow " : L"Deny ") + e.Account, rights });
	}
}

void CDcomDlg::ShowDetails() {
	m_DetailRows.clear();
	auto tab = CurrentTab();
	auto& table = m_Tables[(int)tab];
	if (int row = GetSelectedRow(); row >= 0) {
		auto& rows = m_DetailRows;
		switch (tab) {
			case Tab::Machine:
			{
				auto& s = m_Settings[row];
				rows.push_back({ L"Setting", s.Name });
				rows.push_back({ L"Key", L"HKEY_LOCAL_MACHINE\\" + s.Key });
				rows.push_back({ L"Value", s.Value });
				rows.push_back({ L"Description", s.Description });
				if (s.IsPermission && s.Present)
					AddPermissionRows(rows, s.Launch ? L"Launch" : L"Access", s.SecurityDescriptor, s.Launch);
				break;
			}
			case Tab::AppIds:
			{
				auto& a = m_AppIds[row];
				// the upper list's cells, then the permissions' entries and the classes
				for (int c = 0; c < (int)table.Columns.size(); c++)
					if (!table.Rows[row][c].empty())
						rows.push_back({ table.Columns[c].Name, table.Rows[row][c] });
				if (!a.ServiceParameters.empty())
					rows.push_back({ L"Service Parameters", a.ServiceParameters });
				if (a.ActivateAtStorage)
					rows.push_back({ L"Activate at Storage", L"Yes" });
				if (a.AuthenticationLevel < 0)
					rows.push_back({ L"Authentication", L"Not set: the machine's (LegacyAuthenticationLevel), unless the server sets its own" });
				if (a.LaunchPermission.empty())
					rows.push_back({ L"Launch", L"Not set: the machine's DefaultLaunchPermission" });
				else
					AddPermissionRows(rows, L"Launch", a.LaunchPermission, true);
				if (a.AccessPermission.empty())
					rows.push_back({ L"Access", L"Not set: the machine's DefaultAccessPermission, unless the server calls CoInitializeSecurity" });
				else
					AddPermissionRows(rows, L"Access", a.AccessPermission, false);
				if (auto it = m_ClassesByAppId.find(a.AppId); it != m_ClassesByAppId.end())
					for (auto i : it->second)
						rows.push_back({ L"Class", (m_Classes[i].Name.empty() ? L"" : m_Classes[i].Name + L" ") + m_Classes[i].Clsid });
				break;
			}
			case Tab::Elevation:
			{
				auto& c = m_Classes[m_Elevated[row]];
				for (int col = 0; col < (int)table.Columns.size(); col++)
					if (!table.Rows[row][col].empty())
						rows.push_back({ table.Columns[col].Name, table.Rows[row][col] });
				if (!c.ServerFile.empty())
					rows.push_back({ L"Server File", c.ServerFile });
				rows.push_back({ L"Created Elevated", c.AutoApproved
					? L"Without a prompt: in UAC's COMAutoApprovalList"
					: L"With a consent prompt, through the elevation moniker (Elevation:Administrator!new:...)" });
				if (!c.RunAs.empty())
					rows.push_back({ L"Run As", c.RunAs });
				break;
			}
			case Tab::Errors:
			{
				auto& e = m_Errors[row];
				for (int col = 0; col < (int)table.Columns.size() - 1; col++)
					if (!table.Rows[row][col].empty())
						rows.push_back({ table.Columns[col].Name, table.Rows[row][col] });
				// the message a line at a time
				std::wstring line;
				for (auto ch : e.Message + L"\n") {
					if (ch == L'\r')
						continue;
					if (ch == L'\n') {
						if (!line.empty())
							rows.push_back({ rows.back().first == L"Message" || rows.back().first.empty() ? L"" : L"Message", line });
						line.clear();
					}
					else
						line += ch;
				}
				for (auto& d : e.Data)
					rows.push_back({ d.Name.empty() ? L"Data" : d.Name, d.Value });
				break;
			}
			case Tab::RunningObjects:
			{
				for (int col = 0; col < (int)table.Columns.size(); col++)
					rows.push_back({ table.Columns[col].Name, table.Rows[row][col] });
				break;
			}
		}
	}
	m_Details.SetRedraw(FALSE);
	m_Details.DeleteAllItems();
	for (int i = 0; i < (int)m_DetailRows.size(); i++) {
		m_Details.InsertItem(i, m_DetailRows[i].first.c_str());
		m_Details.SetItemText(i, 1, m_DetailRows[i].second.c_str());
	}
	m_Details.SetRedraw(TRUE);
	m_Details.Invalidate();
	UpdateStatus();
}

void CDcomDlg::Sort(Table& table) {
	auto column = table.SortColumn;
	if (column < 0 || column >= (int)table.Columns.size())
		return;
	auto kind = table.Columns[column].Sort;
	std::ranges::stable_sort(table.Shown, [&](int a, int b) {
		auto& x = table.Rows[a][column];
		auto& y = table.Rows[b][column];
		// empty values last either way
		if (x.empty() != y.empty())
			return y.empty();
		int result;
		if (kind == Table::Kind::Time)
			result = table.Times[a] < table.Times[b] ? -1 : table.Times[b] < table.Times[a] ? 1 : 0;
		else if (kind == Table::Kind::Number) {
			auto nx = std::wcstoll(x.c_str(), nullptr, 10), ny = std::wcstoll(y.c_str(), nullptr, 10);
			result = nx < ny ? -1 : ny < nx ? 1 : 0;
		}
		else
			result = ::_wcsicmp(x.c_str(), y.c_str());
		return table.SortAscending ? result < 0 : result > 0;
		});
}

void CDcomDlg::Fill(CListViewCtrl& list, Table& table) {
	Sort(table);
	// the selection by its row
	int selectedRow = -1;
	if (int sel = list.GetNextItem(-1, LVNI_SELECTED); sel >= 0)
		selectedRow = (int)list.GetItemData(sel);
	list.SetRedraw(FALSE);
	list.DeleteAllItems();
	int select = -1;
	for (int i = 0; i < (int)table.Shown.size(); i++) {
		auto& cells = table.Rows[table.Shown[i]];
		list.InsertItem(i, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			list.SetItemText(i, c, cells[c].c_str());
		list.SetItemData(i, table.Shown[i]);
		if (table.Shown[i] == selectedRow)
			select = i;
	}
	if (select >= 0) {
		list.SetItemState(select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		list.EnsureVisible(select, FALSE);
	}
	list.SetRedraw(TRUE);
	list.Invalidate();
}

int CDcomDlg::GetSelectedRow() const {
	int sel = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (sel < 0)
		return -1;
	auto row = (int)m_List.GetItemData(sel);
	return row < (int)m_Tables[(int)CurrentTab()].Rows.size() ? row : -1;
}

void CDcomDlg::UpdateStatus() {
	if (!m_Loaded) {
		if (m_Load)
			SetDlgItemText(IDC_DC_STATUS, L"Reading the DCOM settings, the app IDs, the classes and the System log...");
		return;
	}
	auto tab = CurrentTab();
	auto& table = m_Tables[(int)tab];
	auto count = [&](Concern c) { return std::ranges::count(table.Concerns, c); };
	std::wstring text;
	switch (tab) {
		case Tab::Machine:
			text = std::to_wstring(table.Rows.size()) + L" settings";
			if (auto weak = count(Concern::Warning))
				text += L", " + std::to_wstring(weak) + L" weaker than Windows' defaults";
			break;
		case Tab::AppIds:
		{
			auto none = std::ranges::count_if(m_AppIds, [](auto& a) { return a.AuthenticationLevel == 1; });
			auto broad = std::ranges::count_if(m_AppIds, [](auto& a) {
				return GrantsBroadRemote(a.LaunchPermission, true) || GrantsBroadRemote(a.AccessPermission, false);
				});
			auto stored = std::ranges::count_if(m_AppIds, [](auto& a) { return IsStoredAccount(a.RunAs); });
			auto perUser = std::ranges::count_if(m_AppIds, [](auto& a) { return a.PerUser; });
			text = std::to_wstring(m_AppIds.size()) + L" app IDs: " + std::to_wstring(none) + L" without authentication, "
				+ std::to_wstring(broad) + L" with remote rights for anyone, " + std::to_wstring(stored) + L" with a stored account, "
				+ std::to_wstring(perUser) + L" per user";
			break;
		}
		case Tab::Elevation:
		{
			auto quiet = std::ranges::count_if(m_Elevated, [&](int i) { return m_Classes[i].AutoApproved; });
			text = std::to_wstring(table.Rows.size()) + L" classes may be created elevated, " + std::to_wstring(quiet)
				+ L" without a prompt; " + std::to_wstring(count(Concern::Danger)) + L" with a server a standard user can replace";
			break;
		}
		case Tab::Errors:
			if (m_ErrorsError)
				text = L"The System log can't be read: " + Helpers::GetErrorText(m_ErrorsError);
			else
				text = std::to_wstring(table.Rows.size()) + L" DCOM events in the last " + std::to_wstring(ErrorDays)
					+ L" days. Successful activations aren't logged";
			break;
		case Tab::RunningObjects:
			if (m_ObjectsError)
				text = L"The Running Object Table can't be read: " + Helpers::GetErrorText(m_ObjectsError);
			else
				text = std::to_wstring(table.Rows.size()) + L" running objects, as this process sees them"
					+ (SecurityHelper::IsRunningElevated()
						? L" (elevated: those of elevated processes and those registered for any client)"
						: L" (not elevated: those of this user's processes that aren't elevated)");
			break;
	}
	if (table.Shown.size() != table.Rows.size())
		text += L" (" + std::to_wstring(table.Shown.size()) + L" match)";
	SetDlgItemText(IDC_DC_STATUS, text.c_str());
}

std::wstring CDcomDlg::GetSelectedKeyPath() const {
	int row = GetSelectedRow();
	if (row < 0)
		return L"";
	switch (CurrentTab()) {
		case Tab::Machine:
			return L"Computer\\HKEY_LOCAL_MACHINE\\" + m_Settings[row].Key;
		case Tab::AppIds:
		{
			auto& a = m_AppIds[row];
			return std::wstring(L"Computer\\") + (a.PerUser ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE")
				+ L"\\SOFTWARE\\Classes\\AppID\\" + a.AppId;
		}
		case Tab::Elevation:
		{
			auto& c = m_Classes[m_Elevated[row]];
			return std::wstring(L"Computer\\") + (c.PerUser ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE") + L"\\SOFTWARE\\Classes\\"
				+ (c.Wow64 ? L"WOW6432Node\\" : L"") + L"CLSID\\" + c.Clsid + L"\\Elevation";
		}
	}
	return L"";
}

std::wstring CDcomDlg::GetSelectedFile() const {
	int row = GetSelectedRow();
	if (row < 0 || CurrentTab() != Tab::Elevation)
		return L"";
	auto& c = m_Classes[m_Elevated[row]];
	return c.ServerMissing ? L"" : c.ServerFile;
}

void CDcomDlg::ShowPermission(bool launch) {
	int row = GetSelectedRow();
	if (row < 0)
		return;
	std::vector<uint8_t> sd;
	std::wstring name;
	if (CurrentTab() == Tab::Machine) {
		auto& s = m_Settings[row];
		if (!s.IsPermission)
			return;
		launch = s.Launch;
		sd = s.SecurityDescriptor;
		name = L"the Machine's " + s.Name;
		if (sd.empty()) {
			AtlMessageBox(m_hWnd, (s.Name + L" isn't set: " + s.Value.substr(s.Value.find(L':') + 2) + L".").c_str(), IDR_MAINFRAME, MB_ICONINFORMATION);
			return;
		}
	}
	else if (CurrentTab() == Tab::AppIds) {
		auto& a = m_AppIds[row];
		bool isDefault = false;
		sd = launch ? a.LaunchPermission : a.AccessPermission;
		if (sd.empty()) {
			sd = ComRegistry::GetPermission(L"", launch, false, isDefault);
			if (sd.empty()) {
				AtlMessageBox(m_hWnd, launch
					? L"Neither the app ID nor the machine (DefaultLaunchPermission) sets launch permissions: COM's built-in defaults apply."
					: L"Neither the app ID nor the machine (DefaultAccessPermission) sets access permissions: COM's built-in defaults apply.",
					IDR_MAINFRAME, MB_ICONINFORMATION);
				return;
			}
			name = launch ? L"the Machine's Default Launch" : L"the Machine's Default Access";
		}
		else
			name = (a.Name.empty() ? a.AppId : a.Name) + (launch ? L" (Launch)" : L" (Access)");
	}
	else
		return;
	ShowComPermissions(m_hWnd, std::move(sd), name, launch);
}

LRESULT CDcomDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto hWnd = (HWND)wParam;
	if (hWnd != m_List && hWnd != m_Details) {
		bHandled = FALSE;
		return 0;
	}
	CListViewCtrl list(hWnd);
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		if (int sel = list.GetNextItem(-1, LVNI_SELECTED); sel >= 0)
			list.GetItemRect(sel, &rc, LVIR_LABEL);
		pt = rc.CenterPoint();
		list.ClientToScreen(&pt);
	}
	m_CopyList = hWnd;
	enum { Launch = 1, Access, Registry, Location, Copy };
	CMenu menu;
	menu.CreatePopupMenu();
	auto tab = CurrentTab();
	int row = GetSelectedRow();
	if (hWnd == m_List && row >= 0) {
		if (tab == Tab::Machine && m_Settings[row].IsPermission)
			menu.AppendMenu(MF_STRING, Launch, L"&Permissions...");
		else if (tab == Tab::AppIds) {
			menu.AppendMenu(MF_STRING, Launch, L"&Launch Permissions...");
			menu.AppendMenu(MF_STRING, Access, L"&Access Permissions...");
		}
		if (!GetSelectedKeyPath().empty())
			menu.AppendMenu(MF_STRING, Registry, L"Open in &Registry Editor");
		if (tab == Tab::Elevation)
			menu.AppendMenu(MF_STRING | (GetSelectedFile().empty() ? MF_GRAYED : 0), Location, L"Open File &Location");
		if (menu.GetMenuItemCount() > 0)
			menu.AppendMenu(MF_SEPARATOR);
	}
	menu.AppendMenu(MF_STRING, Copy, L"&Copy");
	switch (menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd)) {
		case Launch: ShowPermission(true); break;
		case Access: ShowPermission(false); break;
		case Registry: OpenRegistryEditor(m_hWnd, GetSelectedKeyPath()); break;
		case Location:
			if (auto file = GetSelectedFile(); !file.empty()) {
				// Explorer with the file selected
				auto args = L"/select,\"" + file + L"\"";
				::ShellExecute(m_hWnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
			}
			break;
		case Copy:
		{
			BOOL handled;
			OnCopy(0, IDC_DC_COPY, nullptr, handled);
			break;
		}
	}
	return 0;
}

LRESULT CDcomDlg::OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	m_CopyList = m_List;
	ShowTab();
	return 0;
}

LRESULT CDcomDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (!m_Filling && (lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
		ShowDetails();
	return 0;
}

LRESULT CDcomDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	// a machine permission opens in the ACL editor, the rest in the Registry Editor
	int row = GetSelectedRow();
	if (row >= 0 && CurrentTab() == Tab::Machine && m_Settings[row].IsPermission)
		ShowPermission(m_Settings[row].Launch);
	else if (auto path = GetSelectedKeyPath(); !path.empty())
		OpenRegistryEditor(m_hWnd, path);
	return 0;
}

LRESULT CDcomDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto& table = m_Tables[(int)CurrentTab()];
			auto row = (int)cd->nmcd.lItemlParam;
			if (row < 0 || row >= (int)table.Concerns.size())
				break;
			bool dark = WTLHelper::IsDarkMode();
			// the COM Registration dialog's colors
			switch (table.Concerns[row]) {
				case Concern::Danger: cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160); break;
				case Concern::Warning: cd->clrTextBk = dark ? RGB(140, 70, 0) : RGB(255, 200, 120); break;
				case Concern::Note: cd->clrTextBk = dark ? RGB(100, 90, 0) : RGB(255, 255, 160); break;
				default: return CDRF_DODEFAULT;
			}
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CDcomDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	if (hdr->hwndFrom != m_List)
		return 0;
	auto& table = m_Tables[(int)CurrentTab()];
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == table.SortColumn)
		table.SortAscending = !table.SortAscending;
	else {
		table.SortColumn = column;
		// names A to Z, numbers and times largest first
		table.SortAscending = table.Columns[column].Sort == Table::Kind::Text;
	}
	m_Filling = true;
	Fill(m_List, table);
	m_Filling = false;
	return 0;
}

LRESULT CDcomDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_List || hdr->hwndFrom == m_Details)
		m_CopyList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CDcomDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_DC_FILTER, text);
	m_Filter = Lowercase((PCWSTR)text);
	ShowRows();
	return 0;
}

LRESULT CDcomDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CDcomDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CListViewCtrl list(m_CopyList);
	bool selected = list.GetSelectedCount() > 0;
	std::wstring text;
	if (m_CopyList == m_Details) {
		for (int i = 0; i < (int)m_DetailRows.size(); i++)
			if (!selected || (list.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
				text += m_DetailRows[i].first + L"\t" + m_DetailRows[i].second + L"\r\n";
	}
	else {
		auto& table = m_Tables[(int)CurrentTab()];
		for (int c = 0; c < (int)table.Columns.size(); c++)
			text += (c ? L"\t" : L"") + std::wstring(table.Columns[c].Name);
		text += L"\r\n";
		for (int i = 0; i < list.GetItemCount(); i++) {
			if (selected && !(list.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
				continue;
			auto& cells = table.Rows[list.GetItemData(i)];
			for (int c = 0; c < (int)cells.size(); c++)
				text += (c ? L"\t" : L"") + cells[c];
			text += L"\r\n";
		}
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CDcomDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
