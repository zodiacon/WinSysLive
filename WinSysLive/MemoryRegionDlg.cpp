#include "pch.h"
#include "MemoryRegionDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/ProcessMemory.h>
#include <WTLHelper.h>
#include <DpiHelper.h>
#include <IconHelper.h>

namespace {
	// the hex view shows no more of a region than this
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

	// the hex view's grouping buttons', in IDC_MEM_GROUP1.. order
	UINT const s_GroupIcons[] = { IDI_NUMBER_1, IDI_NUMBER_2, IDI_NUMBER_4, IDI_NUMBER_8 };
}

int CMemoryHexView::GetWidthFor(int bytesPerLine) {
	auto dpi = ::GetDpiForWindow(m_hWnd);
	if (m_CharWidth == 0 || m_CharWidthDpi != dpi) {
		// as the control measures it: its font for its DPI (FontSize is in tenths of a point)
		LOGFONT lf{};
		::StringCchCopy(lf.lfFaceName, LF_FACESIZE, FontName);
		lf.lfHeight = -::MulDiv(FontSize, dpi, 720);
		lf.lfCharSet = DEFAULT_CHARSET;
		CFont font;
		font.CreateFontIndirect(&lf);
		CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(font);
		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		m_CharWidth = tm.tmAveCharWidth;
		m_CharWidthDpi = dpi;
	}
	// the address, the groups (two digits a byte and a space each), the characters, and a little room
	int chars = 16 + 1 + bytesPerLine / GetDataSize() * (2 * GetDataSize() + 1) + 1 + bytesPerLine + 2;
	return chars * m_CharWidth + ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) + 2 * ::GetSystemMetricsForDpi(SM_CXEDGE, dpi);
}

LRESULT CMemoryHexView::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
	if (wParam == VK_INSERT)
		return 0;	// overwriting only: a process's memory doesn't grow
	if (m_Target && ((wParam == 'F' && ctrl) || wParam == VK_F3)) {
		// the dialog as the command's window: the parent of the Find dialog
		::PostMessage(m_Target, WM_COMMAND, wParam == VK_F3 ? IDC_MEM_FINDNEXT : IDC_MEM_FIND, (LPARAM)::GetAncestor(m_hWnd, GA_ROOT));
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

void CMemoryHexView::SetGrouping(int size) {
	if (size == GetDataSize() || !SetDataSize(size))
		return;
	// the caret (and what's typed in Edit mode) is on a group's first byte
	if (GetBufferManager()) {
		auto caret = (std::max)(GetCaretOffset(), (int64_t)0);
		GotoOffset(caret - caret % size);
	}
}

HWND CMemoryHexPane::Create(HWND parent, RECT const& rc, UINT id) {
	CRect rect(rc);
	if (!CWindowImpl::Create(parent, rect, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT, id))
		return nullptr;
	// mixed buttons: the grouping buttons' texts aren't shown (no BTNS_SHOWTEXT) but are their tooltips; the line lengths
	// and Edit show theirs (their tooltips: TBN_GETINFOTIP)
	m_ToolBar.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST
		| TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN, 0);
	m_ToolBar.SetButtonStructSize();
	m_ToolBar.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);
	auto size = DpiHelper::SmallIconSize(m_hWnd);
	m_Images.Create(size, size, ILC_COLOR32 | ILC_MASK, _countof(s_GroupIcons), 0);
	AddImages();
	m_ToolBar.SetImageList(m_Images);
	PCWSTR texts[] = { L"1 Byte", L"2 Bytes", L"4 Bytes", L"8 Bytes" };
	static_assert(_countof(texts) == _countof(s_GroupIcons));
	for (int i = 0; i < _countof(texts); i++)
		m_ToolBar.AddButton(IDC_MEM_GROUP1 + i, BTNS_CHECKGROUP, TBSTATE_ENABLED | (i == 0 ? TBSTATE_CHECKED : 0), i,
			(INT_PTR)texts[i], 0);
	m_ToolBar.AddSeparator(0);
	// 16 a line when the dialog opens
	PCWSTR lines[] = { L"8", L"16", L"24", L"32" };
	static_assert(_countof(lines) == IDC_MEM_LINE32 - IDC_MEM_LINE8 + 1);
	for (int i = 0; i < _countof(lines); i++)
		m_ToolBar.AddButton(IDC_MEM_LINE8 + i, BTNS_CHECKGROUP | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
			TBSTATE_ENABLED | (i == 1 ? TBSTATE_CHECKED : 0), I_IMAGENONE, (INT_PTR)lines[i], 0);
	m_ToolBar.AddSeparator(0);
	m_ToolBar.AddButton(IDC_MEM_EDIT, BTNS_CHECK | BTNS_SHOWTEXT | BTNS_AUTOSIZE, TBSTATE_ENABLED, I_IMAGENONE, (INT_PTR)L"Edit", 0);
	m_ToolBar.AutoSize();

	m_Hex.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER, 0);
	m_Hex.SetBytesPerLine(16);
	Layout();
	return m_hWnd;
}

void CMemoryHexPane::AddImages() {
	auto size = DpiHelper::SmallIconSize(m_hWnd);
	for (auto id : s_GroupIcons)
		m_Images.AddIcon(IconHelper::LoadCached(id, size));
}

void CMemoryHexPane::Layout() {
	if (!m_Hex.IsWindow())
		return;
	CRect rc;
	GetClientRect(&rc);
	CRect button;
	m_ToolBar.GetItemRect(m_ToolBar.GetButtonCount() - 1, &button);
	int height = button.bottom + ::MulDiv(2, ::GetDpiForWindow(m_hWnd), 96);
	m_ToolBar.SetWindowPos(nullptr, 0, 0, rc.Width(), height, SWP_NOZORDER | SWP_NOACTIVATE);
	m_Hex.SetWindowPos(nullptr, 0, height, rc.Width(), (std::max)(0, int(rc.Height() - height)), SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CMemoryHexPane::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	Layout();
	return 0;
}

LRESULT CMemoryHexPane::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the toolbar and the hex view cover it
	return 1;
}

LRESULT CMemoryHexPane::OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// the hex view's go to the dialog (the toolbar's are its own)
	auto hdr = reinterpret_cast<NMHDR*>(lParam);
	if (hdr->hwndFrom == m_Hex.m_hWnd)
		return GetParent().SendMessage(uMsg, wParam, lParam);
	if (hdr->hwndFrom == m_ToolBar && hdr->code == TBN_GETINFOTIP) {
		// the buttons that show their text; the others' tooltips are their texts
		auto tip = reinterpret_cast<NMTBGETINFOTIP*>(lParam);
		std::wstring text;
		if (tip->iItem >= IDC_MEM_LINE8 && tip->iItem <= IDC_MEM_LINE32)
			text = std::format(L"{} Bytes a Line", 8 * (tip->iItem - IDC_MEM_LINE8 + 1));
		else if (tip->iItem == IDC_MEM_EDIT)
			text = L"Edit: changes are written to the process's memory as they're made";
		if (!text.empty())
			::StringCchCopy(tip->pszText, tip->cchTextMax, text.c_str());
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

void CMemoryHexPane::FitWidth() {
	CRect rc;
	m_Hex.GetWindowRect(&rc);
	int extra = m_Hex.GetWidthFor(m_Hex.GetBytesPerLine()) - rc.Width();
	CWindow top(GetAncestor(m_hWnd, GA_ROOT));
	if (extra <= 0 || top.IsZoomed())
		return;
	CRect window;
	top.GetWindowRect(&window);
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromWindow(top, MONITOR_DEFAULTTONEAREST), &mi);
	int width = (std::min)(window.Width() + extra, (int)(mi.rcWork.right - mi.rcWork.left));
	// to the left too, if the right edge would leave the work area
	int left = (std::max)((int)mi.rcWork.left, (std::min)((int)window.left, (int)mi.rcWork.right - width));
	top.SetWindowPos(nullptr, left, window.top, width, window.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CMemoryHexPane::OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the icons in the new size, at the same indices; the toolbar is given the image list again for its buttons' size
	auto size = DpiHelper::SmallIconSize(m_hWnd);
	m_Images.SetIconSize(size, size);
	AddImages();
	m_ToolBar.SetImageList(m_Images);
	m_ToolBar.AutoSize();
	Layout();
	bHandled = FALSE;
	return 0;
}

LRESULT CMemoryHexPane::OnGrouping(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Hex.SetGrouping(1 << (wID - IDC_MEM_GROUP1));
	FitWidth();
	return 0;
}

LRESULT CMemoryHexPane::OnLineLength(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// every length is a multiple of every grouping
	m_Hex.SetBytesPerLine(8 * (wID - IDC_MEM_LINE8 + 1));
	FitWidth();
	return 0;
}

LRESULT CMemoryHexPane::OnEdit(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& /*bHandled*/) {
	// the dialog's to act on
	return GetParent().SendMessage(WM_COMMAND, MAKEWPARAM(wID, wNotifyCode), (LPARAM)hWndCtl);
}

CMemoryRegionDlg::CMemoryRegionDlg(HANDLE hProcess, MEMORY_BASIC_INFORMATION const& info, std::wstring name, HWND page,
	OpenForWriting openForWriting, Closed closed) : m_hProcess(hProcess), m_Info(info), m_Name(std::move(name)), m_Page(page),
	m_OpenForWriting(std::move(openForWriting)), m_Closed(std::move(closed)) {
}

LRESULT CMemoryRegionDlg::OnToggleEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	HANDLE hWrite = nullptr;
	std::wstring note;
	if (m_HexPane.IsEditChecked()) {
		hWrite = m_OpenForWriting ? m_OpenForWriting(m_hWnd) : nullptr;
		if (!hWrite || !MakeWritable(hWrite, note)) {
			m_HexPane.SetEditChecked(false);
			m_Hex.SetFocus();
			return 0;
		}
	}
	SetWriteHandle(hWrite);
	if (!note.empty())
		SetNote(note);
	m_Hex.SetFocus();
	return 0;
}

bool CMemoryRegionDlg::MakeWritable(HANDLE hWrite, std::wstring& note) {
	// the region as it is now: its protection may have changed since the map was read
	MEMORY_BASIC_INFORMATION mbi;
	if (::VirtualQueryEx(m_hProcess, m_Info.BaseAddress, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) {
		AtlMessageBox(m_hWnd, L"The region isn't committed anymore (Refresh the Memory page).", IDR_MAINFRAME, MB_ICONERROR);
		return false;
	}
	if (WinSys::ProcessMemory::IsWritableProtection(mbi.Protect))
		return true;

	auto current = FormatHelper::PageProtectionToString(mbi.Protect);
	auto writable = WinSys::ProcessMemory::WritableProtection(mbi.Protect);
	auto target = FormatHelper::PageProtectionToString(writable);
	auto text = std::format(L"The region's pages are {} ({} at 0x{:X}), so they can't be edited.\n\nChange their protection to {}?",
		current, FormatHelper::FormatBytes(mbi.RegionSize), (uint64_t)mbi.BaseAddress, target);
	if (mbi.Protect & PAGE_GUARD)
		text += L"\n\nTheir guard is removed as well.";
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
		SetNote(L"Not editing: the region is read-only");
		return false;
	}
	if (!WinSys::ProcessMemory::Protect(hWrite, (uint64_t)mbi.BaseAddress, mbi.RegionSize, writable)) {
		auto error = ::GetLastError();
		AtlMessageBox(m_hWnd, std::format(L"The protection can't be changed to {}: {}\n\nThe region can't be edited.", target,
			WinSys::Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
		SetNote(L"Not editing: the protection can't be changed");
		return false;
	}
	m_Info.Protect = writable;
	// the first change only: Edit checked again finds the pages writable
	if (!m_ProtectionChange)
		m_ProtectionChange = ProtectionChange{ hWrite, (uint64_t)mbi.BaseAddress, mbi.RegionSize, mbi.Protect, writable };
	note = std::format(L"Protection changed from {} to {}", current, target);
	return true;
}

void CMemoryRegionDlg::AskToRestoreProtection() {
	if (!m_ProtectionChange)
		return;
	auto change = *m_ProtectionChange;
	m_ProtectionChange.reset();
	auto original = FormatHelper::PageProtectionToString(change.Original);
	auto text = std::format(L"The protection of the region's pages ({} at 0x{:X}) was changed from {} to {}.\n\nRestore it to {}?",
		FormatHelper::FormatBytes(change.Size), change.Address, original, FormatHelper::PageProtectionToString(change.Changed), original);
	if (AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	if (!WinSys::ProcessMemory::Protect(change.hProcess, change.Address, change.Size, change.Original)) {
		auto error = ::GetLastError();
		AtlMessageBox(m_hWnd, std::format(L"The protection can't be restored to {}: {}", original,
			WinSys::Helpers::GetErrorText(error)).c_str(), IDR_MAINFRAME, MB_ICONERROR);
	}
}

void CMemoryRegionDlg::OnFinalMessage(HWND /*hWnd*/) {
	if (m_Closed)
		m_Closed(this);
	delete this;
}

LRESULT CMemoryRegionDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(IconHelper::LoadCached(IDI_MEMORY, DpiHelper::SmallIconSize(m_hWnd)), FALSE);

	// the pane takes the placeholder's place (and its ID, which the resize map knows)
	CWindow placeholder(GetDlgItem(IDC_MEMR_HEX));
	CRect rc;
	placeholder.GetWindowRect(&rc);
	ScreenToClient(&rc);
	placeholder.SetDlgCtrlID(0);
	m_Hex.SetFont(CMemoryHexView::FontName, CMemoryHexView::FontSize);
	m_HexPane.Create(m_hWnd, rc, IDC_MEMR_HEX);
	// first in the tab order, where the placeholder was
	m_HexPane.SetWindowPos(placeholder, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	placeholder.DestroyWindow();
	m_Hex.SetCommandTarget(m_Page);
	m_Hex.SetOptions(HexControlOptions::DisableUndoRedo | HexControlOptions::GrayOutZeros);
	auto& colors = m_Hex.GetColors();
	colors.Text = ::GetSysColor(COLOR_WINDOWTEXT);
	colors.Background = ::GetSysColor(COLOR_WINDOW);
	if (WTLHelper::IsDarkMode()) {
		// the defaults are dark red and blue, for a light background
		colors.Ascii = RGB(206, 145, 120);
		colors.Offset = colors.Ruler = RGB(86, 156, 214);
	}

	DlgResize_Init(true, true);
	InitDpi();
	// as wide as 16 bytes a line take
	int extra = m_Hex.GetWidthFor(16) - rc.Width();
	if (extra > 0) {
		CRect window;
		GetWindowRect(&window);
		SetWindowPos(nullptr, 0, 0, window.Width() + extra, window.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	CenterWindow(GetParent());

	UpdateTitle();
	ShowBytes();
	m_Hex.SetFocus();
	return FALSE;
}

void CMemoryRegionDlg::UpdateTitle() {
	SetWindowText(std::format(L"{} ({}): 0x{:X} ({}){}", m_Name, ::GetProcessId(m_hProcess), GetBase(),
		FormatHelper::FormatBytes(m_Info.RegionSize), m_hWrite ? L" - Editing" : L"").c_str());
}

void CMemoryRegionDlg::ShowBytes() {
	m_Hex.SetBufferManager(nullptr);
	auto size = (std::min)((int64_t)m_Info.RegionSize, MaxShownBytes);
	m_Bytes = std::make_unique<ProcessMemoryBuffer>(m_hProcess, m_hWrite, (uintptr_t)m_Info.BaseAddress, size,
		[this](uint64_t address, DWORD error) {
			SetNote(std::format(L"Writing at 0x{:X} failed: {}", address, WinSys::Helpers::GetErrorText(error)));
			m_Hex.Refresh();
		});
	m_Hex.SetReadOnly(m_hWrite == nullptr);
	m_Hex.SetBufferManager(m_Bytes.get());
	// the offsets shown are the addresses
	m_Hex.SetBiasOffset((int64_t)m_Info.BaseAddress);
	m_Summary = m_hWrite ? L"Editing: changes are written to the process as they're made" : L"Read-only";
	if (size < (int64_t)m_Info.RegionSize)
		m_Summary += std::format(L"    Showing the first {} of the region", FormatHelper::FormatBytes(size));
	SetNote(L"");
}

void CMemoryRegionDlg::SetNote(std::wstring const& note) {
	SetDlgItemText(IDC_MEMR_STATUS, (note.empty() ? m_Summary : m_Summary + L"    " + note).c_str());
}

void CMemoryRegionDlg::SetWriteHandle(HANDLE hWrite) {
	if (hWrite == m_hWrite)
		return;
	m_hWrite = hWrite;
	// the bytes again (with or without the handle to write with), where they were
	auto caret = m_Hex.GetCaretOffset();
	auto selection = m_Hex.GetSelectionOffset(), length = m_Hex.GetSelectionLength();
	ShowBytes();
	UpdateTitle();
	if (caret >= 0 && caret < m_Bytes->GetSize()) {
		m_Hex.GotoOffset(caret);
		if (length > 0)
			m_Hex.SetSelection(selection, length);
	}
}

uint64_t CMemoryRegionDlg::GetCurrentAddress() const {
	return GetBase() + (std::max)(m_Hex.GetCaretOffset(), (int64_t)0);
}

bool CMemoryRegionDlg::Reveal(uint64_t address, size_t length) {
	if (IsIconic())
		ShowWindow(SW_RESTORE);
	SetActiveWindow();
	auto offset = (int64_t)(address - GetBase());
	if (!Contains(address) || offset >= m_Bytes->GetSize())
		return false;
	m_Hex.GotoOffset(offset);
	if (length)
		m_Hex.SetSelection(offset, (int64_t)length);
	m_Hex.SetFocus();
	return true;
}

LRESULT CMemoryRegionDlg::OnGoToRequested(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom != m_Hex) {
		bHandled = FALSE;
		return 0;
	}
	// the page's Go To, parented by this dialog
	::PostMessage(m_Page, WM_COMMAND, IDC_MEM_GOTO, (LPARAM)m_hWnd);
	return 0;
}

LRESULT CMemoryRegionDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// not asked (OnClose asks and forgets the change): restored without asking, nothing to tell if it fails
	if (m_ProtectionChange) {
		auto& change = *m_ProtectionChange;
		WinSys::ProcessMemory::Protect(change.hProcess, change.Address, change.Size, change.Original);
		m_ProtectionChange.reset();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CMemoryRegionDlg::OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// Close, Escape, the close box, and the sheet closing (which sends its dialogs WM_CLOSE first)
	AskToRestoreProtection();
	DestroyWindow();
	return 0;
}
