#pragma once

#include <HexControl/SimpleHexControl.h>
#include <functional>
#include "resource.h"
#include "DpiLayout.h"

// the bytes of a region can be read without side effects (a guard page's read would clear its guard)
inline bool IsReadableRegion(MEMORY_BASIC_INFORMATION const& mbi) {
	return mbi.State == MEM_COMMIT && mbi.Protect != 0 && (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
}

//
// A memory region dialog's hex view: 8, 16, 24 or 32 bytes a line, as the toolbar sets it (16 when it opens). Ctrl+F and F3 are the Memory page's Find and Find Next (Ctrl+G, the control's own, is
// its Go To): posted to the page with the dialog as the command's window, the parent of the dialogs they show.
// Insert doesn't switch to insert mode: a process's memory can't grow
//
class CMemoryHexView : public CHexControl {
public:
	// the font the control is given; its average character width is the control's column width
	static constexpr PCWSTR FontName = L"Consolas";
	static constexpr int FontSize = 100;	// tenths of a point

	// the width the view takes for the given bytes a line, at its grouping, with 64-bit addresses, its scroll bar and
	// its border
	int GetWidthFor(int bytesPerLine);
	// shows the bytes in groups of 1, 2, 4 or 8 (as little-endian values), the caret moved to its group's start
	void SetGrouping(int size);
	// where Ctrl+F and F3 go
	void SetCommandTarget(HWND target) {
		m_Target = target;
	}

	BEGIN_MSG_MAP(CMemoryHexView)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		CHAIN_MSG_MAP(CHexControl)
	END_MSG_MAP()

private:
	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	int m_CharWidth{ 0 };
	UINT m_CharWidthDpi{ 0 };	// the DPI m_CharWidth was measured at
	HWND m_Target{ nullptr };
};

//
// The hex view with a toolbar above it: buttons (icons, their texts as tooltips) that group its bytes by 1, 2, 4 or 8;
// buttons (their numbers as text) for 8, 16, 24 or 32 bytes a line; and the Edit toggle, whose command goes to the
// pane's parent. None of it is saved: the dialog runs on the sheet's thread, and the settings belong to the UI thread.
// A grouping or line length the view is too narrow for widens its window. The hex view's notifications go on to the
// pane's parent.
//
class CMemoryHexPane : public CWindowImpl<CMemoryHexPane> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLiveMemoryHexPane", 0, -1)

	explicit CMemoryHexPane(CMemoryHexView& hex) : m_Hex(hex) {}

	// creates the pane with its toolbar, and the hex view inside it
	HWND Create(HWND parent, RECT const& rc, UINT id);
	void SetEditChecked(bool checked) {
		m_ToolBar.CheckButton(IDC_MEM_EDIT, checked);
	}
	bool IsEditChecked() const {
		return m_ToolBar.IsButtonChecked(IDC_MEM_EDIT);
	}

	BEGIN_MSG_MAP(CMemoryHexPane)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		COMMAND_RANGE_HANDLER(IDC_MEM_GROUP1, IDC_MEM_GROUP8, OnGrouping)
		COMMAND_RANGE_HANDLER(IDC_MEM_LINE8, IDC_MEM_LINE32, OnLineLength)
		COMMAND_ID_HANDLER(IDC_MEM_EDIT, OnEdit)
	END_MSG_MAP()

private:
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGrouping(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnLineLength(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the buttons' icons in the pane's small icon size, at the buttons' indices
	void AddImages();
	void Layout();
	// widens the window (not past its monitor's work area) when the hex view is narrower than its bytes a line take
	void FitWidth();

	CMemoryHexView& m_Hex;
	CToolBarCtrl m_ToolBar;
	// of the pane's own, as it runs on the sheet's thread
	CImageList m_Images;
};

//
// The bytes of one committed region of a process (opened by the Memory page's View and Edit), modeless and owned by the
// process sheet: the hex view (with its grouping toolbar), read from the process as it's shown (ProcessMemoryBuffer,
// at most 256 MB of the region), the offsets shown as the addresses. It opens read-only; the toolbar's Edit toggle asks
// the page for a handle to write with (OpenForWriting: confirmed once a page); a region whose pages can't be written is
// made read/write first if the user agrees (MakeWritable), else Edit is refused, as it is when the protection can't be
// changed; closing the dialog asks whether to restore the protection changed, and a dialog destroyed without asking
// (the app exiting) restores it. While Edit is checked the hex view is
// writable: each change is written to the process at once (WinSys ProcessMemory::Write); a failed write is in the
// status line, and the bytes are read again. The dialog deletes itself when it's destroyed, after telling its owner
// (Closed). The handles are the page's, which destroys its dialogs before they go.
//
class CMemoryRegionDlg :
	public CDialogImpl<CMemoryRegionDlg>,
	public CDialogResize<CMemoryRegionDlg>,
	public CDpiDialogResize<CMemoryRegionDlg> {
public:
	enum { IDD = IDD_MEMORY_REGION };

	using Closed = std::function<void(CMemoryRegionDlg*)>;
	// a handle with PROCESS_VM_WRITE and PROCESS_VM_OPERATION, the confirmation shown over the given window; null if it's
	// declined or can't be opened
	using OpenForWriting = std::function<HANDLE(HWND parent)>;

	// page: where the hex view's Find, Find Next and Go To go; name: the process's, for the title
	CMemoryRegionDlg(HANDLE hProcess, MEMORY_BASIC_INFORMATION const& info, std::wstring name, HWND page,
		OpenForWriting openForWriting, Closed closed);
	uint64_t GetBase() const {
		return (uint64_t)m_Info.BaseAddress;
	}
	// the region holds the address
	bool Contains(uint64_t address) const {
		return address >= GetBase() && address < GetBase() + m_Info.RegionSize;
	}
	// the caret's address, or the region's start
	uint64_t GetCurrentAddress() const;
	// the caret on the address and the length after it selected, the dialog activated; false if the address is past the
	// part of the region shown
	bool Reveal(uint64_t address, size_t length);
	// in the status line, after what's shown
	void SetNote(std::wstring const& note);

	BEGIN_DLGRESIZE_MAP(CMemoryRegionDlg)
		DLGRESIZE_CONTROL(IDC_MEMR_HEX, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_MEMR_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CMemoryRegionDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		NOTIFY_CODE_HANDLER(NMHX_GOTO_REQUESTED, OnGoToRequested)
		COMMAND_ID_HANDLER(IDC_MEM_EDIT, OnToggleEdit)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CMemoryRegionDlg>)
		CHAIN_MSG_MAP(CDialogResize<CMemoryRegionDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	void OnFinalMessage(HWND /*hWnd*/) override;

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	// destroyed without being asked (the app exiting destroys the sheet and its dialogs): the protection restored
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGoToRequested(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// makes the region writable for Edit: if its pages (as they are now) can't be written, asks to change their
	// protection to read/write (WinSys ProcessMemory::WritableProtection: executable pages stay executable) and does;
	// false (the user told why) if it's declined or fails. note: the change made, for the status line
	bool MakeWritable(HANDLE hWrite, std::wstring& note);
	// after MakeWritable changed the protection: asks whether to restore it, and does (the user told if that fails)
	void AskToRestoreProtection();
	// the bytes in the hex view, with or without the handle to write with
	void ShowBytes();
	// the bytes again, writable with the handle or read-only (null), keeping the caret and the selection
	void SetWriteHandle(HANDLE hWrite);
	void UpdateTitle();

	HANDLE m_hProcess, m_hWrite{ nullptr };
	MEMORY_BASIC_INFORMATION m_Info;
	std::wstring m_Name;
	HWND m_Page;
	OpenForWriting m_OpenForWriting;
	Closed m_Closed;
	// the protection MakeWritable changed, to restore when the dialog closes (the handle is the page's, which outlives it)
	struct ProtectionChange {
		HANDLE hProcess;
		uint64_t Address;
		size_t Size;
		DWORD Original, Changed;
	};
	std::optional<ProtectionChange> m_ProtectionChange;
	CMemoryHexView m_Hex;
	CMemoryHexPane m_HexPane{ m_Hex };
	std::unique_ptr<IBufferManager> m_Bytes;
	std::wstring m_Summary;	// what's shown, in the status line
};
