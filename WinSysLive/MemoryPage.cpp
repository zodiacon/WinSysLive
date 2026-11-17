#include "pch.h"
#include "MemoryPage.h"
#include <unordered_set>
#include <ListViewhelper.h>
#include "FormatHelper.h"
#include "ViewBase.h"
#include <WinSys/Handles.h>
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>
#include <WinSys/ProcessMemory.h>
#include <functional>
#include <WTLHelper.h>
#include <DpiHelper.h>
#include <IconHelper.h>
#include <map>

namespace {
	// in Column order
	struct ColumnDef {
		PCWSTR Name;
		int Width;
		int Format;
	};
	const ColumnDef s_Columns[] = {
		{ L"Address", 150, LVCFMT_LEFT },
		{ L"Size", 90, LVCFMT_RIGHT },
		{ L"State", 80, LVCFMT_LEFT },
		{ L"Type", 70, LVCFMT_LEFT },
		{ L"Protection", 130, LVCFMT_LEFT },
		{ L"Allocation Protection", 130, LVCFMT_LEFT },
		{ L"Details", 300, LVCFMT_LEFT },
	};

	PCWSTR StateToString(DWORD state) {
		switch (state) {
			case MEM_COMMIT: return L"Committed";
			case MEM_RESERVE: return L"Reserved";
			case MEM_FREE: return L"Free";
		}
		return L"";
	}

	PCWSTR TypeToString(DWORD type) {
		switch (type) {
			case MEM_IMAGE: return L"Image";
			case MEM_MAPPED: return L"Mapped";
			case MEM_PRIVATE: return L"Private";
		}
		return L"";
	}
}

CProcMemoryPage::CProcMemoryPage(ProcessProperties const& props) : m_Props(props) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_MEMORY);
}

LRESULT CProcMemoryPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP, 0);
	// first in the tab order, before the buttons
	m_Tree.SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	m_Tree.SetCallbackMode(true);
	m_Tree.SetExtendedStyle(m_Tree.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _countof(s_Columns); i++)
		m_Tree.InsertColumn(i, s_Columns[i].Name, ::MulDiv(s_Columns[i].Width, dpi, 96), s_Columns[i].Format);
	// the TreeListView doesn't scale its column widths to a new DPI
	ListViewHelper::EnableColumnDpiScaling(m_Tree.GetListCtrl());

	CRect rc;
	GetDlgItem(IDC_MEM_REFRESH).GetWindowRect(&rc);
	m_ButtonSize = rc.Size();
	EnableButtons();
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	return TRUE;
}

LRESULT CProcMemoryPage::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// they use the page's handles; each removes itself from the list as it goes
	auto dialogs = m_RegionDlgs;
	for (auto dlg : dialogs)
		dlg->DestroyWindow();
	bHandled = FALSE;
	return 0;
}

LRESULT CProcMemoryPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	Layout(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

void CProcMemoryPage::Layout(int cx, int cy) {
	if (!m_Tree.IsWindow())
		return;
	// the other pages' margins; Refresh and View at the bottom right, the status line at their left; Find, Find Next,
	// Go To, Save and Strings in a row above them; the tree above
	CRect units(0, 0, 7, 4);
	MapDialogRect(&units);
	int margin = units.right, gap = units.bottom;
	int bottom = cy - margin;
	int x = cx - margin - m_ButtonSize.cx;
	for (UINT id : { IDC_MEM_REFRESH, IDC_MEM_VIEW }) {
		GetDlgItem(id).SetWindowPos(nullptr, x, bottom - m_ButtonSize.cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		x -= m_ButtonSize.cx + gap;
	}
	CWindow status(GetDlgItem(IDC_MEM_STATUS));
	CRect rc;
	status.GetWindowRect(&rc);
	status.SetWindowPos(nullptr, margin, bottom - (m_ButtonSize.cy + rc.Height()) / 2,
		(std::max)(0, int(x + m_ButtonSize.cx - margin)), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	int row = bottom - m_ButtonSize.cy - gap - m_ButtonSize.cy;
	x = margin;
	for (UINT id : { IDC_MEM_FIND, IDC_MEM_FINDNEXT, IDC_MEM_GOTO, IDC_MEM_SAVE, IDC_MEM_STRINGS }) {
		GetDlgItem(id).SetWindowPos(nullptr, x, row, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		x += m_ButtonSize.cx + gap;
	}
	m_Tree.SetWindowPos(nullptr, margin, margin, (std::max)(0, cx - 2 * margin),
		(std::max)(0, int(row - gap - margin)), SWP_NOZORDER | SWP_NOACTIVATE);
}

int CProcMemoryPage::OnSetActive() {
	// read when first shown: a large address space takes a while
	if (!m_Loaded) {
		m_Loaded = true;
		CWaitCursor wait;
		Load();
	}
	return 0;
}

LRESULT CProcMemoryPage::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto item = m_Tree.GetSelectedItem();
	void const* selected = item ? m_Regions[m_Tree.GetItemData(item)].Info.BaseAddress : nullptr;
	CWaitCursor wait;
	Load(selected);
	return 0;
}

void CProcMemoryPage::Load(void const* select) {
	// the allocations expanded (by their base) stay expanded on a Refresh
	std::unordered_set<void const*> expanded;
	for (auto item : m_Items)
		if (!m_Tree.GetParentItem(item) && m_Tree.GetChildItem(item) && m_Tree.IsExpanded(item))
			expanded.insert(m_Regions[m_Tree.GetItemData(item)].Info.AllocationBase);

	m_Tree.DeleteAllItems();
	m_Regions.clear();
	m_Items.clear();
	m_Found = 0;

	if (!m_hProcess) {
		// reading the bytes needs more than querying the map; protected processes allow neither
		m_hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ);
		m_CanRead = m_hProcess != nullptr;
		if (!m_hProcess)
			m_hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_INFORMATION);
		if (!m_hProcess) {
			m_Summary = L"Can't open the process: " + WinSys::Helpers::GetErrorText(::GetLastError());
			SetDlgItemText(IDC_MEM_STATUS, m_Summary.c_str());
			return;
		}
	}

	// not WinSys's Helpers::GetDosNameFromNtName: its table isn't safe to build on the sheet's thread while the UI thread
	// uses it
	WinSys::DosNames dosNames;
	MEMORY_BASIC_INFORMATION mbi;
	BYTE const* address = nullptr;
	uint64_t committed = 0, reserved = 0;
	while (::VirtualQueryEx(m_hProcess.get(), address, &mbi, sizeof(mbi)) == sizeof(mbi)) {
		Region region{ mbi };
		if (mbi.BaseAddress == mbi.AllocationBase && (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED)) {
			// whatever its length (GetMappedFileName's buffer cut long paths)
			if (auto name = WinSys::ProcessMemory::GetMappedFileName(m_hProcess.get(), mbi.BaseAddress); !name.empty())
				region.Details = dosNames.Convert(name);
		}
		if (mbi.State == MEM_COMMIT)
			committed += mbi.RegionSize;
		else if (mbi.State == MEM_RESERVE)
			reserved += mbi.RegionSize;
		m_Regions.push_back(std::move(region));
		address = (BYTE const*)mbi.BaseAddress + mbi.RegionSize;
	}
	LabelProcessStructures();

	// an allocation's first region is a top-level item, its other regions (which follow it) its children
	m_Tree.BeginUpdate();
	HTLITEM parent = nullptr, selected = nullptr;
	void const* parentBase = nullptr;
	for (size_t i = 0; i < m_Regions.size(); i++) {
		auto& info = m_Regions[i].Info;
		bool child = info.State != MEM_FREE && parent && info.AllocationBase == parentBase && info.BaseAddress != info.AllocationBase;
		auto item = m_Tree.InsertItem(child ? parent : TLV_ROOT, TLV_LAST, LPSTR_TEXTCALLBACK, -1, (LPARAM)i);
		m_Items.push_back(item);
		if (!child) {
			parent = item;
			parentBase = info.State == MEM_FREE ? nullptr : info.AllocationBase;
		}
		if (select && info.BaseAddress == select)
			selected = item;
	}
	m_Tree.CollapseAll();
	if (!expanded.empty())
		for (auto item : m_Items)
			if (!m_Tree.GetParentItem(item) && m_Tree.GetChildItem(item)
				&& expanded.contains(m_Regions[m_Tree.GetItemData(item)].Info.AllocationBase))
				m_Tree.Expand(item);
	m_Tree.EndUpdate();

	m_Summary = std::format(L"{} regions    Committed: {}    Reserved: {}", FormatHelper::FormatNumber(m_Regions.size()),
		FormatHelper::FormatBytes(committed), FormatHelper::FormatBytes(reserved));
	if (!m_CanRead)
		m_Summary += L"    (the memory can't be read)";
	SetNote(L"");
	EnableButtons();

	if (selected) {
		if (auto p = m_Tree.GetParentItem(selected))
			m_Tree.Expand(p);
		m_Tree.SelectItem(selected);
	}
}

CProcMemoryPage::Region* CProcMemoryPage::FindRegion(ULONG_PTR address) {
	// the regions are in address order, and contiguous
	auto it = std::ranges::upper_bound(m_Regions, address, {}, [](Region const& r) { return (ULONG_PTR)r.Info.BaseAddress; });
	if (it == m_Regions.begin())
		return nullptr;
	auto& region = *--it;
	return address < (ULONG_PTR)region.Info.BaseAddress + region.Info.RegionSize ? &region : nullptr;
}

void CProcMemoryPage::AddDetails(Region& region, std::wstring const& text) {
	if (!region.Details.empty())
		region.Details += L", ";
	region.Details += text;
}

void CProcMemoryPage::LabelProcessStructures() {
	auto hProcess = m_hProcess.get();
	// what an allocation's other regions hold, for its first region (the tree's collapsed item): the PEB and the
	// TEBs share an allocation
	struct Contents {
		bool Peb{ false }, Peb32{ false };
		int Tebs{ 0 }, Tebs32{ 0 };
	};
	std::map<void const*, Contents> contents;
	enum class Kind { Peb, Peb32, Teb, Teb32 };
	auto label = [&](ULONG_PTR address, Kind kind, std::wstring const& text) {
		auto region = FindRegion(address);
		if (!region)
			return;
		AddDetails(*region, text);
		if (region->Info.BaseAddress != region->Info.AllocationBase) {
			auto& c = contents[region->Info.AllocationBase];
			switch (kind) {
				case Kind::Peb: c.Peb = true; break;
				case Kind::Peb32: c.Peb32 = true; break;
				case Kind::Teb: c.Tebs++; break;
				case Kind::Teb32: c.Tebs32++; break;
			}
		}
	};
	// a stack's whole reservation: the committed part, the guard page and the rest the stack can grow into
	auto labelStack = [&](ULONG_PTR stackBase, std::wstring const& text) {
		auto top = FindRegion(stackBase - 1);
		if (!top || top->Info.State == MEM_FREE)
			return;
		// an allocation's regions are contiguous: outward from the top's, not the whole map for every thread
		auto index = top - m_Regions.data();
		auto allocation = top->Info.AllocationBase;
		auto first = index, last = index;
		while (first > 0 && m_Regions[first - 1].Info.State != MEM_FREE && m_Regions[first - 1].Info.AllocationBase == allocation)
			first--;
		while (last + 1 < (ptrdiff_t)m_Regions.size() && m_Regions[last + 1].Info.State != MEM_FREE
			&& m_Regions[last + 1].Info.AllocationBase == allocation)
			last++;
		for (auto i = first; i <= last; i++)
			AddDetails(m_Regions[i], text);
	};

	// the PEB, and each thread's TEB and stack (WinSys; the stacks only with PROCESS_VM_READ), the 32-bit ones of a
	// WOW64 process too
	auto structures = WinSys::Process::GetStructures(hProcess);
	if (structures.Peb)
		label(structures.Peb, Kind::Peb, L"PEB");
	if (structures.Peb32)
		label(structures.Peb32, Kind::Peb32, L"PEB (32-bit)");
	for (auto& t : structures.Threads) {
		label(t.Teb, Kind::Teb, std::format(L"TEB (thread {})", t.ThreadId));
		if (t.StackBase)
			labelStack(t.StackBase, std::format(L"Stack (thread {})", t.ThreadId));
		if (t.Teb32)
			label(t.Teb32, Kind::Teb32, std::format(L"TEB (32-bit, thread {})", t.ThreadId));
		if (t.Stack32Base)
			labelStack(t.Stack32Base, std::format(L"Stack (32-bit, thread {})", t.ThreadId));
	}

	for (auto& [base, c] : contents) {
		auto first = FindRegion((ULONG_PTR)base);
		if (!first)
			continue;
		auto tebs = [](int count, PCWSTR bitness) {
			return count == 1 ? std::format(L"TEB{}", bitness) : std::format(L"TEBs{} of {} threads", bitness, count);
		};
		if (c.Peb)
			AddDetails(*first, L"PEB");
		if (c.Peb32)
			AddDetails(*first, L"PEB (32-bit)");
		if (c.Tebs)
			AddDetails(*first, tebs(c.Tebs, L""));
		if (c.Tebs32)
			AddDetails(*first, tebs(c.Tebs32, L" (32-bit)"));
	}
}

LRESULT CProcMemoryPage::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMTLVDISPINFO*>(hdr);
	auto index = (size_t)m_Tree.GetItemData(di->hItem);
	if (index < m_Regions.size() && di->iSubItem < (int)Column::_Count)
		::StringCchCopy(di->pszText, di->cchTextMax, GetText(m_Regions[index], (Column)di->iSubItem).c_str());
	return 0;
}

std::wstring CProcMemoryPage::GetText(Region const& region, Column column) const {
	auto& info = region.Info;
	switch (column) {
		case Column::Address: return std::format(L"0x{:012X}", (ULONG_PTR)info.BaseAddress);
		case Column::Size: return FormatHelper::FormatNumber(info.RegionSize >> 10) + L" KB";
		case Column::State: return StateToString(info.State);
		case Column::Type: return info.State == MEM_FREE ? L"" : TypeToString(info.Type);
		case Column::Protection: return info.State == MEM_COMMIT ? FormatHelper::PageProtectionToString(info.Protect) : L"";
		case Column::AllocationProtection: return info.State == MEM_FREE ? L"" : FormatHelper::PageProtectionToString(info.AllocationProtect);
		case Column::Details: return region.Details;
	}
	return L"";
}

CProcMemoryPage::Region const* CProcMemoryPage::GetSelectedRegion() const {
	auto item = m_Tree.GetSelectedItem();
	auto index = item ? (size_t)m_Tree.GetItemData(item) : SIZE_MAX;
	return index < m_Regions.size() ? &m_Regions[index] : nullptr;
}

void CProcMemoryPage::SetNote(std::wstring const& note) {
	SetDlgItemText(IDC_MEM_STATUS, (note.empty() ? m_Summary : m_Summary + L"    " + note).c_str());
}

LRESULT CProcMemoryPage::OnSelChanged(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	EnableButtons();
	return 0;
}

LRESULT CProcMemoryPage::OnActivate(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// a double-click on a region without children (an allocation with them expands or collapses)
	auto item = reinterpret_cast<NMTLVACTIVATE*>(hdr)->hItem;
	auto index = item ? (size_t)m_Tree.GetItemData(item) : SIZE_MAX;
	if (index < m_Regions.size())
		OpenRegion(m_Regions[index]);
	return 0;
}

void CProcMemoryPage::EnableButtons() {
	auto region = GetSelectedRegion();
	GetDlgItem(IDC_MEM_VIEW).EnableWindow(region && m_CanRead && IsReadableRegion(region->Info));
}

LRESULT CProcMemoryPage::OnView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto region = GetSelectedRegion())
		OpenRegion(*region);
	return 0;
}

CMemoryRegionDlg* CProcMemoryPage::FindRegionDlg(uint64_t base) const {
	for (auto dlg : m_RegionDlgs)
		if (dlg->GetBase() == base)
			return dlg;
	return nullptr;
}

HWND CProcMemoryPage::GetDialogParent(HWND from) const {
	for (auto dlg : m_RegionDlgs)
		if (dlg->m_hWnd == from)
			return from;
	return m_hWnd;
}

HANDLE CProcMemoryPage::OpenForWriting(HWND parent) {
	if (m_hWrite)
		return m_hWrite.get();
	if (AtlMessageBox(parent, L"Changes made in the hex view are written to the process's memory as they're made, which can "
		L"make it fail or behave differently.\n\nEdit its memory?", IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
		return nullptr;
	m_hWrite = OpenSheetProcess(m_Props, PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_LIMITED_INFORMATION);
	if (!m_hWrite)
		AtlMessageBox(parent, std::format(L"The process can't be opened for writing: {}",
			WinSys::Helpers::GetErrorText(::GetLastError())).c_str(), IDR_MAINFRAME, MB_ICONERROR);
	return m_hWrite.get();
}

CMemoryRegionDlg* CProcMemoryPage::OpenRegion(Region const& region) {
	auto& info = region.Info;
	if (!IsReadableRegion(info)) {
		SetNote(info.State != MEM_COMMIT ? L"Not committed: no bytes to show" : L"Not readable (no access or guard pages)");
		return nullptr;
	}
	if (!m_CanRead) {
		SetNote(L"The process's memory can't be read");
		return nullptr;
	}
	if (auto dlg = FindRegionDlg((uint64_t)info.BaseAddress)) {
		if (dlg->IsIconic())
			dlg->ShowWindow(SW_RESTORE);
		dlg->SetActiveWindow();
		return dlg;
	}
	auto dlg = new CMemoryRegionDlg(m_hProcess.get(), info, m_Props.ImageName, m_hWnd,
		[this](HWND parent) { return OpenForWriting(parent); },
		[this](CMemoryRegionDlg* closed) { std::erase(m_RegionDlgs, closed); });
	// owned by the sheet: above it, and destroyed with it
	if (!dlg->Create(GetParent())) {
		delete dlg;
		return nullptr;
	}
	if (!m_RegionDlgs.empty()) {
		// cascaded from the centered one, so they don't hide each other
		int offset = ::GetSystemMetricsForDpi(SM_CYCAPTION, ::GetDpiForWindow(dlg->m_hWnd)) * (int)(m_RegionDlgs.size() % 8);
		CRect rc;
		dlg->GetWindowRect(&rc);
		dlg->SetWindowPos(nullptr, rc.left + offset, rc.top + offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	m_RegionDlgs.push_back(dlg);
	dlg->ShowWindow(SW_SHOW);
	return dlg;
}

uint64_t CProcMemoryPage::GetCurrentAddress() const {
	auto region = GetSelectedRegion();
	if (!region)
		return 0;
	auto base = (uint64_t)region->Info.BaseAddress;
	auto dlg = FindRegionDlg(base);
	return dlg ? dlg->GetCurrentAddress() : base;
}

bool CProcMemoryPage::Reveal(uint64_t address, size_t length, std::wstring const& note) {
	auto region = FindRegion((ULONG_PTR)address);
	if (!region)
		return false;
	auto item = m_Items[region - m_Regions.data()];
	if (auto parent = m_Tree.GetParentItem(item))
		m_Tree.Expand(parent);
	m_Tree.SelectItem(item);
	auto dlg = OpenRegion(*region);
	if (!dlg)
		return true;	// the note says why
	if (!dlg->Reveal(address, length)) {
		auto text = std::format(L"0x{:X} is past the part of the region shown", address);
		SetNote(text);
		dlg->SetNote(text);
		return true;
	}
	SetNote(note);
	dlg->SetNote(note);
	return true;
}

void CProcMemoryPage::Search(bool next) {
	if (!m_CanRead) {
		SetNote(L"The process's memory can't be read");
		return;
	}
	auto pattern = m_Search.GetPattern();
	if (pattern.empty())
		return;
	auto region = GetSelectedRegion();
	bool whole = m_Search.WholeSpace || !region;
	uint64_t begin = whole ? 0 : (uint64_t)region->Info.BaseAddress;
	uint64_t to = whole ? WinSys::ProcessMemory::MaxAddress() : (uint64_t)region->Info.BaseAddress + region->Info.RegionSize;
	// after the last match, if it's in the range (another region may have been selected since); else from the caret
	uint64_t from = next && m_Found && m_Found + 1 >= begin && m_Found + 1 < to ? m_Found + 1 : GetCurrentAddress();
	auto find = [&](uint64_t start, uint64_t end) {
		return WinSys::ProcessMemory::Find(m_hProcess.get(), pattern.data(), pattern.size(), start, end,
			m_Search.IgnoreCase && m_Search.Type != MemorySearch::Kind::Hex, m_Search.Type == MemorySearch::Kind::Unicode);
	};
	std::optional<uint64_t> found;
	bool wrapped = false;
	{
		CWaitCursor wait;
		found = find(from, to);
		// then from the start of the range, to where this search began
		if (!found && from > begin) {
			found = find(begin, from);
			wrapped = found.has_value();
		}
	}
	if (!found) {
		m_Found = 0;
		::MessageBeep(MB_ICONASTERISK);
		auto note = std::format(L"Not found{}", whole ? L"" : L" in the region");
		SetNote(note);
		if (auto dlg = region ? FindRegionDlg((uint64_t)region->Info.BaseAddress) : nullptr)
			dlg->SetNote(note);
		return;
	}
	m_Found = *found;
	Reveal(*found, pattern.size(), std::format(L"Found at 0x{:X}{} (F3: the next one)", *found, wrapped ? L", from the start again" : L""));
}

LRESULT CProcMemoryPage::OnFind(WORD /*wNotifyCode*/, WORD wID, HWND hWndCtl, BOOL& /*bHandled*/) {
	if (wID == IDC_MEM_FIND || !m_Searched) {
		CMemoryFindDlg dlg(m_Search, GetSelectedRegion() != nullptr);
		if (dlg.DoModal(GetDialogParent(hWndCtl)) != IDOK)
			return 0;
		m_Searched = true;
		Search(false);
	}
	else
		Search(true);
	return 0;
}

std::wstring CProcMemoryPage::DescribeAddress(uint64_t address) {
	auto region = FindRegion((ULONG_PTR)address);
	if (!region)
		return L"";
	auto details = region->Details;
	// a mapping's file is on its allocation's first region
	if (details.empty())
		for (auto r = region; r >= m_Regions.data() && r->Info.AllocationBase == region->Info.AllocationBase; r--)
			if (!r->Details.empty()) {
				details = r->Details;
				break;
			}
	auto type = GetText(*region, Column::Type);
	return details.empty() ? type : type + L": " + details;
}

LRESULT CProcMemoryPage::OnStrings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_CanRead) {
		SetNote(L"The process's memory can't be read");
		return 0;
	}
	std::optional<std::pair<uint64_t, uint64_t>> range;
	if (auto region = GetSelectedRegion())
		range = std::make_pair((uint64_t)region->Info.BaseAddress, (uint64_t)region->Info.BaseAddress + region->Info.RegionSize);
	CMemoryStringsDlg dlg(m_Strings, m_hProcess.get(), range, [this](uint64_t address) { return DescribeAddress(address); });
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	if (!Reveal(dlg.GetAddress(), dlg.GetByteLength(), std::format(L"The string at 0x{:X}", dlg.GetAddress())))
		SetNote(std::format(L"0x{:X} isn't in the map anymore (Refresh reads it again)", dlg.GetAddress()));
	return 0;
}

LRESULT CProcMemoryPage::OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& /*bHandled*/) {
	// from a region's dialog (Ctrl+G): at its caret
	auto parent = GetDialogParent(hWndCtl);
	uint64_t current = GetCurrentAddress();
	for (auto dlg : m_RegionDlgs)
		if (dlg->m_hWnd == parent)
			current = dlg->GetCurrentAddress();
	CMemoryGoToDlg dlg(current);
	if (dlg.DoModal(parent) != IDOK)
		return 0;
	if (!Reveal(dlg.GetAddress(), 0))
		SetNote(std::format(L"0x{:X} isn't in the address space", dlg.GetAddress()));
	return 0;
}

LRESULT CProcMemoryPage::OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto region = GetSelectedRegion();
	if (!region || region->Info.State != MEM_COMMIT) {
		AtlMessageBox(m_hWnd, L"Select a committed region to save.", IDR_MAINFRAME, MB_ICONINFORMATION);
		return 0;
	}
	if (!m_CanRead) {
		AtlMessageBox(m_hWnd, L"The process's memory can't be read.", IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	auto base = (uint64_t)region->Info.BaseAddress;
	auto size = (uint64_t)region->Info.RegionSize;
	auto name = m_Props.ImageName;
	if (auto dot = name.rfind(L'.'); dot != std::wstring::npos)
		name.erase(dot);
	auto fileName = std::format(L"{}_{}_{:X}.bin", name, m_Props.Id, base);

	SuspendResumeHook sr;
	CSimpleFileDialog dlg(FALSE, L"bin", fileName.c_str(), OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_PATHMUSTEXIST,
		L"Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0", m_hWnd);
	if (dlg.DoModal() != IDOK)
		return 0;

	wil::unique_hfile file(::CreateFile(dlg.m_szFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file) {
		AtlMessageBox(m_hWnd, std::format(L"Failed to create the file: {}", WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	CWaitCursor wait;
	const size_t chunk = 1 << 20;
	auto buffer = std::make_unique<uint8_t[]>(chunk);
	uint64_t unreadable = 0;
	for (uint64_t offset = 0; offset < size; offset += chunk) {
		auto count = (size_t)(std::min)((uint64_t)chunk, size - offset);
		unreadable += count - WinSys::ProcessMemory::Read(m_hProcess.get(), base + offset, buffer.get(), count);
		DWORD written;
		if (!::WriteFile(file.get(), buffer.get(), (DWORD)count, &written, nullptr) || written != count) {
			AtlMessageBox(m_hWnd, std::format(L"Failed to write the file: {}", WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
				IDR_MAINFRAME, MB_ICONERROR);
			return 0;
		}
	}
	SetNote(std::format(L"Saved {} to {}{}", FormatHelper::FormatBytes(size), dlg.m_szFileName,
		unreadable ? std::format(L" ({} couldn't be read: zeros)", FormatHelper::FormatBytes(unreadable)) : std::wstring()));
	return 0;
}
