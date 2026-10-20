#include "pch.h"
#include "MemoryPage.h"
#include "FormatHelper.h"
#include "ViewBase.h"
#include "ObjectNames.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
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

	// the hex view shows no more of a region than this; a vector<bool> of the shown size tracks modified bytes
	const int64_t MaxShownBytes = 256 << 20;

	//
	// A range of another process's memory, read as the hex view asks for it, in chunks kept until the next one is read
	//
	class ProcessMemoryBuffer : public IBufferManager {
	public:
		ProcessMemoryBuffer(HANDLE hProcess, uintptr_t address, int64_t size) : m_hProcess(hProcess), m_Address(address), m_Size(size) {
		}

		uint32_t GetData(int64_t offset, uint8_t* buffer, uint32_t count) override {
			if (offset < 0 || offset >= m_Size)
				return 0;
			count = (uint32_t)(std::min)((int64_t)count, m_Size - offset);
			uint32_t copied = 0;
			while (copied < count) {
				auto at = offset + copied;
				auto chunk = at / ChunkSize * ChunkSize;
				if (chunk != m_ChunkOffset) {
					m_ChunkOffset = chunk;
					SIZE_T read = 0;
					auto size = (SIZE_T)(std::min)((int64_t)ChunkSize, m_Size - chunk);
					// a partial read leaves the rest of the chunk unreadable
					::ReadProcessMemory(m_hProcess, (void const*)(m_Address + chunk), m_Chunk, size, &read);
					m_ChunkValid = (uint32_t)read;
				}
				auto inChunk = (uint32_t)(at - chunk);
				if (inChunk >= m_ChunkValid)
					break;
				auto n = (std::min)(count - copied, m_ChunkValid - inChunk);
				::memcpy(buffer + copied, m_Chunk + inChunk, n);
				copied += n;
			}
			return copied;
		}

		bool Insert(int64_t, const uint8_t*, uint32_t) override {
			return false;
		}
		bool Delete(int64_t, size_t) override {
			return false;
		}
		bool SetData(int64_t, const uint8_t*, uint32_t) override {
			return false;
		}
		bool SetData(int64_t, uint8_t, uint32_t) override {
			return false;
		}
		int64_t GetSize() const override {
			return m_Size;
		}
		uint8_t* GetRawData(int64_t) override {
			return nullptr;
		}
		bool IsReadOnly() const override {
			return true;
		}

	private:
		static constexpr uint32_t ChunkSize = 1 << 16;

		HANDLE m_hProcess;
		uintptr_t m_Address;
		int64_t m_Size;
		int64_t m_ChunkOffset{ -1 };
		uint32_t m_ChunkValid{ 0 };
		uint8_t m_Chunk[ChunkSize];
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

	std::wstring ProtectionToString(DWORD protect) {
		if (protect == 0)
			return L"";
		std::wstring text;
		switch (protect & 0xff) {
			case PAGE_NOACCESS: text = L"No Access"; break;
			case PAGE_READONLY: text = L"Read"; break;
			case PAGE_READWRITE: text = L"Read/Write"; break;
			case PAGE_WRITECOPY: text = L"Write Copy"; break;
			case PAGE_EXECUTE: text = L"Execute"; break;
			case PAGE_EXECUTE_READ: text = L"Execute/Read"; break;
			case PAGE_EXECUTE_READWRITE: text = L"Execute/Read/Write"; break;
			case PAGE_EXECUTE_WRITECOPY: text = L"Execute/Write Copy"; break;
			default: text = std::format(L"0x{:X}", protect & 0xff); break;
		}
		if (protect & PAGE_GUARD)
			text += L", Guard";
		if (protect & PAGE_NOCACHE)
			text += L", No Cache";
		if (protect & PAGE_WRITECOMBINE)
			text += L", Write Combine";
		return text;
	}

	// the bytes of a region can be read without side effects (a guard page's read would clear its guard)
	bool IsReadable(MEMORY_BASIC_INFORMATION const& mbi) {
		return mbi.State == MEM_COMMIT && mbi.Protect != 0 && (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
	}
}

int CMemoryHexView::GetWidthFor(int bytesPerLine) {
	if (m_CharWidth == 0) {
		// as the control measures it
		CFont font;
		font.CreatePointFont(FontSize, FontName);
		CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(font);
		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		m_CharWidth = tm.tmAveCharWidth;
	}
	// the address, the bytes (two digits and a space each), the characters, and a little room
	int chars = 16 + 1 + bytesPerLine * 3 + 1 + bytesPerLine + 2;
	auto dpi = ::GetDpiForWindow(m_hWnd);
	return chars * m_CharWidth + ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) + 2 * ::GetSystemMetricsForDpi(SM_CXEDGE, dpi);
}

LRESULT CMemoryHexView::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	CRect rc;
	GetWindowRect(&rc);
	int bytesPerLine = rc.Width() >= GetWidthFor(16) ? 16 : 8;
	if (bytesPerLine != GetBytesPerLine())
		SetBytesPerLine(bytesPerLine);
	// the control lays itself out too
	bHandled = FALSE;
	return 0;
}

CProcMemoryPage::CProcMemoryPage(ProcessProperties const& props) : m_Props(props) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_MEMORY);
}

LRESULT CProcMemoryPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	static_assert(_countof(s_Columns) == (int)Column::_Count);

	// the splitter forwards the tree's notifications to the page
	m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT);
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP, 0);
	m_Tree.SetCallbackMode(true);
	m_Tree.SetExtendedStyle(m_Tree.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	SuspendHookWhilePainting(m_Tree.GetListCtrl());
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _countof(s_Columns); i++)
		m_Tree.InsertColumn(i, s_Columns[i].Name, ::MulDiv(s_Columns[i].Width, dpi, 96), s_Columns[i].Format);

	m_Hex.SetFont(CMemoryHexView::FontName, CMemoryHexView::FontSize);
	m_Hex.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER, 0);
	m_Hex.SetReadOnly(true);
	m_Hex.SetOptions(HexControlOptions::DisableUndoRedo | HexControlOptions::GrayOutZeros);
	auto& colors = m_Hex.GetColors();
	colors.Text = ::GetSysColor(COLOR_WINDOWTEXT);
	colors.Background = ::GetSysColor(COLOR_WINDOW);
	if (WTLHelper::IsDarkMode()) {
		// the defaults are dark red and blue, for a light background
		colors.Ascii = RGB(206, 145, 120);
		colors.Offset = colors.Ruler = RGB(86, 156, 214);
	}

	m_Splitter.SetSplitterPanes(m_Tree, m_Hex);
	// the hex view starts hidden (the Hex View check box)
	m_Splitter.SetSinglePaneMode(SPLIT_PANE_LEFT);

	CRect rc;
	GetDlgItem(IDC_MEM_REFRESH).GetWindowRect(&rc);
	m_ButtonSize = rc.Size();
	GetDlgItem(IDC_MEM_HEXVIEW).GetWindowRect(&rc);
	m_CheckSize = rc.Size();
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	return TRUE;
}

LRESULT CProcMemoryPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	Layout(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

void CProcMemoryPage::Layout(int cx, int cy) {
	if (!m_Splitter.IsWindow())
		return;
	// the other pages' margins; the button at the bottom right, the check box and the status line at its left, the panes
	// above them
	CRect units(0, 0, 7, 4);
	MapDialogRect(&units);
	int margin = units.right, gap = units.bottom;
	int bottom = cy - margin;
	GetDlgItem(IDC_MEM_REFRESH).SetWindowPos(nullptr, cx - margin - m_ButtonSize.cx, bottom - m_ButtonSize.cy, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	int checkLeft = cx - margin - m_ButtonSize.cx - 2 * gap - m_CheckSize.cx;
	GetDlgItem(IDC_MEM_HEXVIEW).SetWindowPos(nullptr, checkLeft, bottom - (m_ButtonSize.cy + m_CheckSize.cy) / 2, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	CWindow status(GetDlgItem(IDC_MEM_STATUS));
	CRect rc;
	status.GetWindowRect(&rc);
	status.SetWindowPos(nullptr, margin, bottom - (m_ButtonSize.cy + rc.Height()) / 2,
		(std::max)(0, int(checkLeft - margin - gap)), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	m_Splitter.SetWindowPos(nullptr, margin, margin, (std::max)(0, cx - 2 * margin),
		(std::max)(0, int(bottom - m_ButtonSize.cy - gap - margin)), SWP_NOZORDER | SWP_NOACTIVATE);
}

int CProcMemoryPage::OnSetActive() {
	// read when first shown: a large address space takes a while
	if (!m_Loaded) {
		m_Loaded = true;
		// the hex view as wide as 16 bytes a line take if that leaves the map 35% of the width, else as 8 bytes take.
		// Not in OnInitDialog: the page has its template's size then, and the sheet grows it to its largest page's
		CRect rc;
		m_Splitter.GetClientRect(&rc);
		int hexWidth = m_Hex.GetWidthFor(16);
		if (hexWidth > rc.Width() * 65 / 100)
			hexWidth = m_Hex.GetWidthFor(8);
		m_Splitter.SetSplitterPos(std::clamp(rc.Width() - hexWidth - m_Splitter.m_cxySplitBar, rc.Width() * 35 / 100, rc.Width() * 80 / 100));
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
	m_Hex.SetBufferManager(nullptr);
	m_Bytes.reset();
	m_Tree.DeleteAllItems();
	m_Regions.clear();

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
	DosNames dosNames;
	MEMORY_BASIC_INFORMATION mbi;
	BYTE const* address = nullptr;
	uint64_t committed = 0, reserved = 0;
	while (::VirtualQueryEx(m_hProcess.get(), address, &mbi, sizeof(mbi)) == sizeof(mbi)) {
		Region region{ mbi };
		if (mbi.BaseAddress == mbi.AllocationBase && (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED)) {
			WCHAR name[MAX_PATH];
			if (::GetMappedFileName(m_hProcess.get(), mbi.BaseAddress, name, _countof(name))) {
				region.Details = dosNames.Convert(name);
			}
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
		if (!child) {
			parent = item;
			parentBase = info.State == MEM_FREE ? nullptr : info.AllocationBase;
		}
		if (select && info.BaseAddress == select)
			selected = item;
	}
	m_Tree.CollapseAll();
	m_Tree.EndUpdate();

	m_Summary = std::format(L"{} regions    Committed: {}    Reserved: {}", FormatHelper::FormatNumber(m_Regions.size()),
		FormatHelper::FormatBytes(committed), FormatHelper::FormatBytes(reserved));
	if (!m_CanRead)
		m_Summary += L"    (the memory can't be read)";
	SetDlgItemText(IDC_MEM_STATUS, m_Summary.c_str());

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
		for (auto& r : m_Regions)
			if (r.Info.State != MEM_FREE && r.Info.AllocationBase == top->Info.AllocationBase)
				AddDetails(r, text);
	};

	PROCESS_BASIC_INFORMATION pbi;
	if (NT_SUCCESS(::NtQueryInformationProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr)) && pbi.PebBaseAddress)
		label((ULONG_PTR)pbi.PebBaseAddress, Kind::Peb, L"PEB");
	// a WOW64 process also has a 32-bit PEB, and its threads 32-bit TEBs and stacks
	ULONG_PTR peb32 = 0;
	if (NT_SUCCESS(::NtQueryInformationProcess(hProcess, ProcessWow64Information, &peb32, sizeof(peb32), nullptr)) && peb32)
		label(peb32, Kind::Peb32, L"PEB (32-bit)");

	// the threads' TEBs, in thread ID order
	std::vector<std::pair<ULONG, ULONG_PTR>> tebs;
	HANDLE hThread = nullptr;
	for (;;) {
		// NtGetNextThread continues from the previous handle, so that one closes only after the call
		HANDLE hNext;
		auto status = ::NtGetNextThread(hProcess, hThread, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &hNext);
		if (hThread)
			::NtClose(hThread);
		if (!NT_SUCCESS(status))
			break;
		hThread = hNext;
		THREAD_BASIC_INFORMATION tbi;
		if (NT_SUCCESS(::NtQueryInformationThread(hThread, ThreadBasicInformation, &tbi, sizeof(tbi), nullptr)) && tbi.TebBaseAddress)
			tebs.push_back({ HandleToULong(tbi.ClientId.UniqueThread), (ULONG_PTR)tbi.TebBaseAddress });
	}
	std::ranges::sort(tebs);

	for (auto [tid, teb] : tebs) {
		label(teb, Kind::Teb, std::format(L"TEB (thread {})", tid));
		// the stack's bounds are at the start of the TEB (NT_TIB); reading them needs PROCESS_VM_READ
		NT_TIB tib;
		if (m_CanRead && ::ReadProcessMemory(hProcess, (void const*)teb, &tib, sizeof(tib), nullptr) && tib.StackBase)
			labelStack((ULONG_PTR)tib.StackBase, std::format(L"Stack (thread {})", tid));
		if (peb32) {
			// WOW64 puts the 32-bit TEB two pages after the 64-bit one
			auto teb32 = teb + 0x2000;
			label(teb32, Kind::Teb32, std::format(L"TEB (32-bit, thread {})", tid));
			NT_TIB32 tib32;
			if (m_CanRead && ::ReadProcessMemory(hProcess, (void const*)teb32, &tib32, sizeof(tib32), nullptr) && tib32.StackBase)
				labelStack(tib32.StackBase, std::format(L"Stack (32-bit, thread {})", tid));
		}
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
		case Column::Protection: return info.State == MEM_COMMIT ? ProtectionToString(info.Protect) : L"";
		case Column::AllocationProtection: return info.State == MEM_FREE ? L"" : ProtectionToString(info.AllocationProtect);
		case Column::Details: return region.Details;
	}
	return L"";
}

LRESULT CProcMemoryPage::OnToggleHexView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_ShowHex = IsDlgButtonChecked(IDC_MEM_HEXVIEW) == BST_CHECKED;
	m_Splitter.SetSinglePaneMode(m_ShowHex ? SPLIT_PANE_NONE : SPLIT_PANE_LEFT);
	ShowBytes(GetSelectedRegion());
	return 0;
}

CProcMemoryPage::Region const* CProcMemoryPage::GetSelectedRegion() const {
	auto item = m_Tree.GetSelectedItem();
	auto index = item ? (size_t)m_Tree.GetItemData(item) : SIZE_MAX;
	return index < m_Regions.size() ? &m_Regions[index] : nullptr;
}

LRESULT CProcMemoryPage::OnSelChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto item = reinterpret_cast<NMTLVITEMCHANGE*>(hdr)->hItemNew;
	auto index = item ? (size_t)m_Tree.GetItemData(item) : SIZE_MAX;
	ShowBytes(index < m_Regions.size() ? &m_Regions[index] : nullptr);
	return 0;
}

void CProcMemoryPage::ShowBytes(Region const* region) {
	m_Hex.SetBufferManager(nullptr);
	m_Bytes.reset();
	std::wstring note;
	// nothing is read while the hex view is hidden
	if (region && m_ShowHex) {
		auto& info = region->Info;
		if (!IsReadable(info))
			note = info.State != MEM_COMMIT ? L"Not committed: no bytes to show" : L"Not readable (no access or guard pages)";
		else if (!m_CanRead)
			note = L"The process's memory can't be read";
		else {
			auto size = (std::min)((int64_t)info.RegionSize, MaxShownBytes);
			m_Bytes = std::make_unique<ProcessMemoryBuffer>(m_hProcess.get(), (uintptr_t)info.BaseAddress, size);
			m_Hex.SetBufferManager(m_Bytes.get());
			// the offsets shown are the addresses
			m_Hex.SetBiasOffset((int64_t)info.BaseAddress);
			if (size < (int64_t)info.RegionSize)
				note = std::format(L"Showing the first {} of the region", FormatHelper::FormatBytes(size));
		}
	}
	SetDlgItemText(IDC_MEM_STATUS, (note.empty() ? m_Summary : m_Summary + L"    " + note).c_str());
}
