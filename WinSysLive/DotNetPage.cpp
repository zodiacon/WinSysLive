#include "pch.h"
#include "DotNetPage.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

namespace {
	std::wstring AssemblyFlagsText(uint32_t flags) {
		static const struct {
			DotNetAssemblyFlags Flag;
			PCWSTR Name;
		} names[] = {
			{ DotNetAssemblyFlags::Dynamic, L"Dynamic" }, { DotNetAssemblyFlags::Native, L"Native image" },
			{ DotNetAssemblyFlags::ReadyToRun, L"ReadyToRun" }, { DotNetAssemblyFlags::DomainNeutral, L"Domain neutral" },
			{ DotNetAssemblyFlags::Collectible, L"Collectible" },
		};
		std::wstring text;
		for (auto& n : names)
			if (flags & (uint32_t)n.Flag)
				text += (text.empty() ? L"" : L", ") + std::wstring(n.Name);
		return text;
	}

	std::wstring AppDomainText(DotNetAppDomain const& d) {
		auto text = d.Name;
		if (d.Flags & (uint32_t)DotNetAppDomainFlags::Shared)
			text += L" (shared)";
		else if (d.Flags & (uint32_t)DotNetAppDomainFlags::Default)
			text += L" (default)";
		return text;
	}
}

CProcDotNetPage::CProcDotNetPage(ProcessProperties const& props) : m_Props(props) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_DOTNET);
	m_Cancel.create(wil::EventOptions::ManualReset);
}

CProcDotNetPage::~CProcDotNetPage() {
	// the query gives up at once; m_Worker joins
	m_Cancel.SetEvent();
}

LRESULT CProcDotNetPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_DN_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Assembly", 220, LVCFMT_LEFT }, { L"Version", 90, LVCFMT_LEFT }, { L"AppDomain", 110, LVCFMT_LEFT },
		{ L"Flags", 120, LVCFMT_LEFT }, { L"Path", 360, LVCFMT_LEFT }, { L"Native Image", 300, LVCFMT_LEFT },
		{ L"Culture", 60, LVCFMT_LEFT }, { L"Public Key Token", 120, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, ::MulDiv(columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	::SendDlgItemMessage(m_hWnd, IDC_DN_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name, version, AppDomain or path");
	// the garbage collections from now on, for as long as the page exists
	if (m_Gc.Start(m_Props.Id, L"WinSysLive GC "))
		m_GcStarted = ::GetTickCount64();
	else
		m_GcError = ::GetLastError();
	ShowGc();
	SetTimer(GcTimerId, 1000);
	CRect rc;
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	return TRUE;
}

int CProcDotNetPage::OnSetActive() {
	if (!m_Loaded) {
		m_Loaded = true;
		StartQuery();
	}
	return 0;
}

void CProcDotNetPage::Layout(int cx, int cy) {
	if (!m_List)
		return;
	CRect margin(7, 7, 7, 7), row(0, 0, 50, 14), line(0, 0, 0, 10);
	MapDialogRect(&margin);
	MapDialogRect(&row);
	MapDialogRect(&line);
	int x = margin.left, y = margin.top, width = cx - margin.left - margin.right;
	// the runtime line, the managed heap line, the filter with Refresh beside it, the list, the status line
	// a line per runtime (two side by side in some .NET Framework processes)
	int lines = 1 + (int)std::ranges::count(m_Runtimes, L'\n');
	GetDlgItem(IDC_DN_RUNTIME).MoveWindow(x, y, width, line.Height() * lines);
	y += line.Height() * lines;
	GetDlgItem(IDC_DN_GC).MoveWindow(x, y, width, line.Height());
	y += line.Height() + margin.top / 2;
	GetDlgItem(IDC_DN_FILTER).MoveWindow(x, y, width - row.Width() - margin.left, row.Height());
	GetDlgItem(IDC_DN_REFRESH).MoveWindow(cx - margin.right - row.Width(), y, row.Width(), row.Height());
	y += row.Height() + margin.top / 2;
	int statusTop = cy - margin.bottom - line.Height();
	m_List.MoveWindow(x, y, width, std::max(0, (int)(statusTop - margin.top / 2 - y)));
	GetDlgItem(IDC_DN_STATUS).MoveWindow(x, statusTop, width, line.Height());
}

LRESULT CProcDotNetPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	Layout(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	bHandled = FALSE;
	return 0;
}

void CProcDotNetPage::StartQuery() {
	if (m_Querying)
		return;
	m_Querying = true;
	GetDlgItem(IDC_DN_REFRESH).EnableWindow(FALSE);
	UpdateStatus();
	// the previous query is done (m_Querying was off): joining it doesn't wait
	m_Worker = std::jthread([this, hWnd = m_hWnd, pid = m_Props.Id] {
		// the diagnostics pipe first: no elevation, .NET Core and later only
		auto core = DotNetInfo::QueryCoreProcessInfo(pid);
		auto result = DotNetInfo::QueryProcess(pid, L"WinSysLive DotNet ", 5000, m_Cancel.get());
		auto error = result ? 0 : ::GetLastError();
		{
			std::lock_guard lock(m_ResultLock);
			m_Result = std::move(result);
			m_ResultError = error;
			m_ResultCore = std::move(core);
		}
		::PostMessage(hWnd, WM_DOTNET_DONE, 0, 0);
		});
}

LRESULT CProcDotNetPage::OnDone(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Querying = false;
	GetDlgItem(IDC_DN_REFRESH).EnableWindow(TRUE);
	std::optional<DotNetProcessDetails> result;
	{
		std::lock_guard lock(m_ResultLock);
		result = std::move(m_Result);
		m_Error = m_ResultError;
		m_Result.reset();
		m_CoreInfo = std::move(m_ResultCore);
	}
	if (result)
		BuildRows(*result);
	else if (m_CoreInfo) {
		// no rundown (no elevation): the runtime line from the diagnostics pipe
		auto& c = *m_CoreInfo;
		m_Runtimes = std::format(L"CoreCLR {}{}, {}", c.ClrVersion.empty() ? L"(version not said: before .NET 6)" : c.ClrVersion,
			c.EntryAssembly.empty() ? L"" : L", entry assembly " + c.EntryAssembly, c.Architecture);
		SetDlgItemText(IDC_DN_RUNTIME, m_Runtimes.c_str());
		CRect rc;
		GetClientRect(&rc);
		Layout(rc.Width(), rc.Height());
	}
	ShowRows();
	return 0;
}

void CProcDotNetPage::BuildRows(DotNetProcessDetails const& details) {
	m_Complete = details.Complete;
	m_AppDomains = details.AppDomains.size();
	m_Runtimes.clear();
	for (auto& rt : details.Runtimes) {
		if (!m_Runtimes.empty())
			m_Runtimes += L"\r\n";
		m_Runtimes += std::format(L"{} {}, {}", rt.Core ? L"CoreCLR" : L".NET Framework CLR", rt.Version, rt.RuntimePath);
	}
	if (m_Runtimes.empty())
		m_Runtimes = L"No runtime answered";
	// the entry assembly, which only the diagnostics pipe says
	else if (m_CoreInfo && !m_CoreInfo->EntryAssembly.empty())
		m_Runtimes += L" (entry assembly " + m_CoreInfo->EntryAssembly + L")";
	SetDlgItemText(IDC_DN_RUNTIME, m_Runtimes.c_str());
	CRect rc;
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());

	std::unordered_map<uint64_t, std::wstring> domains;
	for (auto& d : details.AppDomains)
		domains[d.Id] = AppDomainText(d);
	// each assembly's manifest module (else its first)
	std::unordered_map<uint64_t, DotNetModule const*> modules;
	for (auto& m : details.Modules) {
		auto& entry = modules[m.AssemblyId];
		if (!entry || (m.Flags & (uint32_t)DotNetModuleFlags::Manifest))
			entry = &m;
	}

	m_Rows.clear();
	m_Rows.reserve(details.Assemblies.size());
	for (auto& a : details.Assemblies) {
		Row row;
		// "Name, Version=1.2.3.4, Culture=neutral, PublicKeyToken=..."
		std::wstring_view full(a.FullName);
		auto comma = full.find(L", ");
		row.Name = full.substr(0, comma);
		while (comma != std::wstring_view::npos) {
			auto start = comma + 2;
			comma = full.find(L", ", start);
			auto part = full.substr(start, comma == std::wstring_view::npos ? std::wstring_view::npos : comma - start);
			auto eq = part.find(L'=');
			if (eq == std::wstring_view::npos)
				continue;
			auto key = part.substr(0, eq);
			std::wstring value(part.substr(eq + 1));
			if (key == L"Version")
				row.Version = value;
			else if (key == L"Culture")
				row.Culture = value;
			else if (key == L"PublicKeyToken")
				row.PublicKeyToken = value == L"null" ? L"" : value;
		}
		if (auto it = domains.find(a.AppDomainId); it != domains.end())
			row.AppDomain = it->second;
		row.Flags = AssemblyFlagsText(a.Flags);
		if (auto it = modules.find(a.Id); it != modules.end()) {
			// a dynamic module's "path" is its name
			if (!(it->second->Flags & (uint32_t)DotNetModuleFlags::Dynamic))
				row.Path = it->second->ILPath;
			row.NativeImage = it->second->NativePath;
		}
		m_Rows.push_back(std::move(row));
	}
}

std::wstring const& CProcDotNetPage::GetText(Row const& row, Column column) const {
	switch (column) {
		case Column::Name: return row.Name;
		case Column::Version: return row.Version;
		case Column::AppDomain: return row.AppDomain;
		case Column::Flags: return row.Flags;
		case Column::Path: return row.Path;
		case Column::NativeImage: return row.NativeImage;
		case Column::Culture: return row.Culture;
		case Column::PublicKeyToken: return row.PublicKeyToken;
	}
	static const std::wstring empty;
	return empty;
}

void CProcDotNetPage::ShowRows() {
	std::vector<int> rows;
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		if (!m_Filter.empty()) {
			bool match = false;
			for (auto column : { Column::Name, Column::Version, Column::AppDomain, Column::Path, Column::NativeImage }) {
				auto text = GetText(m_Rows[i], column);
				::CharLowerBuff(text.data(), (DWORD)text.size());
				if (text.find(m_Filter) != std::wstring::npos) {
					match = true;
					break;
				}
			}
			if (!match)
				continue;
		}
		rows.push_back(i);
	}
	// the runtime's order (load order, mostly) until a column is clicked
	if (m_SortColumn >= 0) {
		auto column = (Column)m_SortColumn;
		std::ranges::stable_sort(rows, [&](int a, int b) {
			int result = ::_wcsicmp(GetText(m_Rows[a], column).c_str(), GetText(m_Rows[b], column).c_str());
			return m_SortAscending ? result < 0 : result > 0;
			});
	}

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (int i = 0; i < (int)rows.size(); i++) {
		auto& row = m_Rows[rows[i]];
		m_List.InsertItem(i, row.Name.c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, GetText(row, (Column)c).c_str());
		m_List.SetItemData(i, rows[i]);
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate();
	UpdateStatus();
}

LRESULT CProcDotNetPage::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != GcTimerId) {
		bHandled = FALSE;
		return 0;
	}
	ShowGc();
	return 0;
}

void CProcDotNetPage::ShowGc() {
	std::wstring text;
	if (!m_Gc.IsRunning())
		text = m_GcError == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated()
			? L"Managed heap: following the garbage collections needs elevation (the runtime reports them through ETW)"
			: L"Managed heap: the garbage collections can't be followed: " + Helpers::GetErrorText(m_GcError);
	else {
		auto s = m_Gc.GetStats();
		if (s.Collections == 0 && !s.HasHeap)
			text = L"Managed heap: waiting for a garbage collection (none since the page opened)";
		else {
			auto bytes = [](uint64_t n) { return n ? FormatHelper::FormatBytes(n) : std::wstring(L"0 B"); };
			auto ms = [](uint64_t ticks) { return std::format(L"{:.1f} ms", ticks / 10000.0); };
			auto elapsed = (::GetTickCount64() - m_GcStarted) * 10000;	// 100 ns
			text = std::format(L"Managed heap {}", s.HasHeap ? bytes(s.HeapSize()) : std::wstring(L"(sizes after the next collection)"));
			if (s.HasHeap)
				text += std::format(L" (gen 0 {}, gen 1 {}, gen 2 {}, LOH {}{})", bytes(s.GenerationSize[0]),
					bytes(s.GenerationSize[1]), bytes(s.GenerationSize[2]), bytes(s.LohSize),
					s.PohSize ? L", POH " + bytes(s.PohSize) : L"");
			text += std::format(L"; {} collections ({}/{}/{})", s.Collections, s.ByGeneration[0], s.ByGeneration[1], s.ByGeneration[2]);
			if (s.MaxPause)
				text += std::format(L", pauses: last {}, longest {}, {:.2f}% of the time", ms(s.LastPause), ms(s.MaxPause),
					elapsed ? 100.0 * s.TotalPause / elapsed : 0.0);
		}
	}
	SetDlgItemText(IDC_DN_GC, text.c_str());
}

void CProcDotNetPage::UpdateStatus() {
	std::wstring text;
	if (m_Querying)
		text = L"Asking the runtime for its assemblies...";
	else if (m_Error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
		text = m_CoreInfo ? L"The runtime from its diagnostics pipe; listing the assemblies needs elevation (the runtime's rundown is an ETW provider)"
			: L"Listing the assemblies needs elevation (the runtime's rundown is an ETW provider)";
	else if (m_Error)
		text = L"The runtime couldn't be asked: " + Helpers::GetErrorText(m_Error);
	else if (m_Rows.empty())
		text = L"No runtime answered in time (it may have diagnostics disabled, or not have started yet)";
	else {
		text = std::format(L"{} assemblies in {} AppDomain{}", m_Rows.size(), m_AppDomains, m_AppDomains == 1 ? L"" : L"s");
		if (!m_Filter.empty())
			text += std::format(L"; {} shown", m_List.GetItemCount());
		if (!m_Complete)
			text += L" (the runtime didn't finish in time: the list may be incomplete)";
	}
	SetDlgItemText(IDC_DN_STATUS, text.c_str());
}

CProcDotNetPage::Row const* CProcDotNetPage::GetSelectedRow() const {
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(selected);
	return index < m_Rows.size() ? &m_Rows[index] : nullptr;
}

LRESULT CProcDotNetPage::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = column;
		m_SortAscending = true;
	}
	ShowRows();
	return 0;
}

LRESULT CProcDotNetPage::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_DN_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CProcDotNetPage::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	StartQuery();
	return 0;
}

LRESULT CProcDotNetPage::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List || m_List.GetItemCount() == 0) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		if (selected >= 0)
			m_List.GetItemRect(selected, &rc, LVIR_LABEL);
		else
			m_List.GetClientRect(&rc);
		pt = selected >= 0 ? rc.CenterPoint() : rc.TopLeft();
		m_List.ClientToScreen(&pt);
	}
	auto row = GetSelectedRow();
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_DOTNET_COPY, m_List.GetSelectedCount() ? L"&Copy" : L"&Copy All");
	menu.AppendMenu(MF_STRING | (row && !row->Path.empty() ? 0 : MF_GRAYED), ID_DOTNET_OPENLOCATION, L"Open File &Location");
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CProcDotNetPage::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text = L"Assembly\tVersion\tAppDomain\tFlags\tPath\tNative Image\tCulture\tPublic Key Token\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& row = m_Rows[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(row, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CProcDotNetPage::OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelectedRow(); row && !row->Path.empty())
		::ShellExecute(m_hWnd, L"open", L"explorer.exe", std::format(L"/select,\"{}\"", row->Path).c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}
