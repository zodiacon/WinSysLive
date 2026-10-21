#include "pch.h"
#include "MemoryPage.h"
#include "FormatHelper.h"
#include "ViewBase.h"
#include <WinSys/Handles.h>
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>
#include <WinSys/ProcessMemory.h>
#include <functional>
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
	// A range of another process's memory, read as the hex view asks for it, in chunks kept until the next one is read;
	// with a handle to write with, the hex view's changes are written to the process at once
	//
	class ProcessMemoryBuffer : public IBufferManager {
	public:
		using WriteFailed = std::function<void(uint64_t address, DWORD error)>;

		// hWrite: PROCESS_VM_WRITE and PROCESS_VM_OPERATION, or null (read-only)
		ProcessMemoryBuffer(HANDLE hProcess, HANDLE hWrite, uintptr_t address, int64_t size, WriteFailed failed) :
			m_hProcess(hProcess), m_hWrite(hWrite), m_Address(address), m_Size(size), m_Failed(std::move(failed)) {
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
		bool SetData(int64_t offset, const uint8_t* data, uint32_t count) override {
			if (!m_hWrite || offset < 0 || offset + count > m_Size)
				return false;
			if (!WinSys::ProcessMemory::Write(m_hWrite, m_Address + offset, data, count)) {
				auto error = ::GetLastError();
				// the bytes as they are
				m_ChunkOffset = -1;
				if (m_Failed)
					m_Failed(m_Address + offset, error);
				return false;
			}
			// the bytes kept from the last read
			for (uint32_t i = 0; i < count; i++) {
				auto at = offset + i;
				if (at >= m_ChunkOffset && at < m_ChunkOffset + m_ChunkValid)
					m_Chunk[at - m_ChunkOffset] = data[i];
			}
			return true;
		}
		bool SetData(int64_t offset, uint8_t value, uint32_t count) override {
			std::vector<uint8_t> data(count, value);
			return SetData(offset, data.data(), count);
		}
		int64_t GetSize() const override {
			return m_Size;
		}
		uint8_t* GetRawData(int64_t) override {
			return nullptr;
		}
		bool IsReadOnly() const override {
			return m_hWrite == nullptr;
		}

	private:
		static constexpr uint32_t ChunkSize = 1 << 16;

		HANDLE m_hProcess, m_hWrite;
		WriteFailed m_Failed;
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

LRESULT CMemoryHexView::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
	if (wParam == VK_INSERT)
		return 0;	// overwriting only: a process's memory doesn't grow
	if (m_Page && ((wParam == 'F' && ctrl) || wParam == VK_F3)) {
		::PostMessage(m_Page, WM_COMMAND, wParam == VK_F3 ? IDC_MEM_FINDNEXT : IDC_MEM_FIND, 0);
		return 0;
	}
	bHandled = FALSE;
	return 0;
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
	m_Hex.SetPage(m_hWnd);
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
	GetDlgItem(IDC_MEM_EDIT).GetWindowRect(&rc);
	m_EditSize = rc.Size();
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
	int editLeft = checkLeft - gap - m_EditSize.cx;
	GetDlgItem(IDC_MEM_EDIT).SetWindowPos(nullptr, editLeft, bottom - (m_ButtonSize.cy + m_EditSize.cy) / 2, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	CWindow status(GetDlgItem(IDC_MEM_STATUS));
	CRect rc;
	status.GetWindowRect(&rc);
	status.SetWindowPos(nullptr, margin, bottom - (m_ButtonSize.cy + rc.Height()) / 2,
		(std::max)(0, int(editLeft - margin - gap)), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	// Find, Find Next, Go To and Save in a row above
	int row = bottom - m_ButtonSize.cy - gap - m_ButtonSize.cy;
	int x = margin;
	for (UINT id : { IDC_MEM_FIND, IDC_MEM_FINDNEXT, IDC_MEM_GOTO, IDC_MEM_SAVE }) {
		GetDlgItem(id).SetWindowPos(nullptr, x, row, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		x += m_ButtonSize.cx + gap;
	}
	m_Splitter.SetWindowPos(nullptr, margin, margin, (std::max)(0, cx - 2 * margin),
		(std::max)(0, int(row - gap - margin)), SWP_NOZORDER | SWP_NOACTIVATE);
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
		m_Items.push_back(item);
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
	SetNote(L"");

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
			m_Bytes = std::make_unique<ProcessMemoryBuffer>(m_hProcess.get(), m_Edit ? m_hWrite.get() : nullptr,
				(uintptr_t)info.BaseAddress, size, [this](uint64_t address, DWORD error) {
					SetNote(std::format(L"Writing at 0x{:X} failed: {}", address, WinSys::Helpers::GetErrorText(error)));
					m_Hex.Refresh();
				});
			m_Hex.SetReadOnly(!m_Edit);
			m_Hex.SetBufferManager(m_Bytes.get());
			// the offsets shown are the addresses
			m_Hex.SetBiasOffset((int64_t)info.BaseAddress);
			if (size < (int64_t)info.RegionSize)
				note = std::format(L"Showing the first {} of the region", FormatHelper::FormatBytes(size));
		}
	}
	SetNote(note);
}

void CProcMemoryPage::SetNote(std::wstring const& note) {
	SetDlgItemText(IDC_MEM_STATUS, (note.empty() ? m_Summary : m_Summary + L"    " + note).c_str());
}

void CProcMemoryPage::ShowHexView() {
	if (m_ShowHex)
		return;
	CheckDlgButton(IDC_MEM_HEXVIEW, BST_CHECKED);
	m_ShowHex = true;
	m_Splitter.SetSinglePaneMode(SPLIT_PANE_NONE);
	ShowBytes(GetSelectedRegion());
}

uint64_t CProcMemoryPage::GetCurrentAddress() const {
	auto region = GetSelectedRegion();
	if (!region)
		return 0;
	auto base = (uint64_t)region->Info.BaseAddress;
	return m_Bytes ? base + (std::max)(m_Hex.GetCaretOffset(), (int64_t)0) : base;
}

bool CProcMemoryPage::Reveal(uint64_t address, size_t length) {
	auto region = FindRegion((ULONG_PTR)address);
	if (!region)
		return false;
	auto item = m_Items[region - m_Regions.data()];
	if (auto parent = m_Tree.GetParentItem(item))
		m_Tree.Expand(parent);
	// shows its bytes (OnSelChanged), if it isn't selected already
	m_Tree.SelectItem(item);
	ShowHexView();
	if (!m_Bytes)
		return true;	// the note says why
	auto offset = (int64_t)(address - (uint64_t)region->Info.BaseAddress);
	if (offset >= m_Bytes->GetSize()) {
		SetNote(std::format(L"0x{:X} is past the part of the region shown", address));
		return true;
	}
	m_Hex.GotoOffset(offset);
	if (length)
		m_Hex.SetSelection(offset, (int64_t)length);
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
	uint64_t from = next && m_Found ? m_Found + 1 : GetCurrentAddress();
	uint64_t to = whole ? WinSys::ProcessMemory::MaxAddress() : (uint64_t)region->Info.BaseAddress + region->Info.RegionSize;
	std::optional<uint64_t> found;
	{
		CWaitCursor wait;
		found = WinSys::ProcessMemory::Find(m_hProcess.get(), pattern.data(), pattern.size(), from, to,
			m_Search.IgnoreCase && m_Search.Type != MemorySearch::Kind::Hex, m_Search.Type == MemorySearch::Kind::Unicode);
	}
	if (!found) {
		m_Found = 0;
		::MessageBeep(MB_ICONASTERISK);
		SetNote(std::format(L"Not found after 0x{:X}{}", from, whole ? L"" : L" in the region"));
		return;
	}
	m_Found = *found;
	Reveal(*found, pattern.size());
	SetNote(std::format(L"Found at 0x{:X} (F3: the next one)", *found));
}

LRESULT CProcMemoryPage::OnFind(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDC_MEM_FIND || !m_Searched) {
		CMemoryFindDlg dlg(m_Search, GetSelectedRegion() != nullptr);
		if (dlg.DoModal(m_hWnd) != IDOK)
			return 0;
		m_Searched = true;
		Search(false);
	}
	else
		Search(true);
	return 0;
}

LRESULT CProcMemoryPage::OnGoTo(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CMemoryGoToDlg dlg(GetCurrentAddress());
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	if (!Reveal(dlg.GetAddress(), 0))
		SetNote(std::format(L"0x{:X} isn't in the address space", dlg.GetAddress()));
	return 0;
}

LRESULT CProcMemoryPage::OnGoToRequested(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Hex) {
		bHandled = FALSE;
		return 0;
	}
	PostMessage(WM_COMMAND, IDC_MEM_GOTO);
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
	CFileDialog dlg(FALSE, L"bin", fileName.c_str(), OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_PATHMUSTEXIST,
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

LRESULT CProcMemoryPage::OnToggleEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	bool edit = IsDlgButtonChecked(IDC_MEM_EDIT) == BST_CHECKED;
	if (edit && !m_hWrite) {
		if (AtlMessageBox(m_hWnd, L"Changes made in the hex view are written to the process's memory as they're made, which can "
			L"make it fail or behave differently.\n\nEdit its memory?", IDR_MAINFRAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
			CheckDlgButton(IDC_MEM_EDIT, BST_UNCHECKED);
			return 0;
		}
		m_hWrite = OpenSheetProcess(m_Props, PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_LIMITED_INFORMATION);
		if (!m_hWrite) {
			AtlMessageBox(m_hWnd, std::format(L"The process can't be opened for writing: {}",
				WinSys::Helpers::GetErrorText(::GetLastError())).c_str(), IDR_MAINFRAME, MB_ICONERROR);
			CheckDlgButton(IDC_MEM_EDIT, BST_UNCHECKED);
			return 0;
		}
	}
	m_Edit = edit;
	if (m_Edit && !m_ShowHex) {
		ShowHexView();
		return 0;
	}
	// the bytes again (with or without the handle to write with), where they were
	auto caret = m_Hex.GetCaretOffset();
	auto selection = m_Hex.GetSelectionOffset(), length = m_Hex.GetSelectionLength();
	ShowBytes(GetSelectedRegion());
	if (m_Bytes && caret >= 0 && caret < m_Bytes->GetSize()) {
		m_Hex.GotoOffset(caret);
		if (length > 0)
			m_Hex.SetSelection(selection, length);
	}
	return 0;
}
