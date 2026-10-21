#pragma once

#include "resource.h"

//
// The Memory page's search: what to look for and where
//
struct MemorySearch {
	enum class Kind { Ascii, Unicode, Hex };

	std::wstring Text;
	Kind Type{ Kind::Ascii };
	bool IgnoreCase{ true };	// text only
	bool WholeSpace{ false };	// the whole address space, not just the selected region

	// the bytes to look for: the text as ASCII or UTF-16, or the hex bytes ("4D 5A", "4d5a90"); empty, with the reason, if
	// the text makes none
	std::vector<uint8_t> GetPattern(std::wstring* error = nullptr) const;
};

// Find in the Memory page (Find..., Ctrl+F in the hex view)
class CMemoryFindDlg : public CDialogImpl<CMemoryFindDlg> {
public:
	enum { IDD = IDD_MEMORY_FIND };

	// search: the last one, changed on OK; hasRegion: a region is selected (else the whole address space is searched)
	CMemoryFindDlg(MemorySearch& search, bool hasRegion) : m_Search(search), m_HasRegion(hasRegion) {}

	BEGIN_MSG_MAP(CMemoryFindDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_RANGE_HANDLER(IDC_MFIND_ASCII, IDC_MFIND_HEX, OnKind)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	MemorySearch& m_Search;
	bool m_HasRegion;
};

// Go To in the Memory page (Go To..., Ctrl+G in the hex view): an address in hex ("7ff6`12340000" and "0x" are fine)
class CMemoryGoToDlg : public CDialogImpl<CMemoryGoToDlg> {
public:
	enum { IDD = IDD_MEMORY_GOTO };

	explicit CMemoryGoToDlg(uint64_t address) : m_Address(address) {}
	uint64_t GetAddress() const {
		return m_Address;
	}

	BEGIN_MSG_MAP(CMemoryGoToDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	uint64_t m_Address;
};
