#include "pch.h"
#include "MemoryDialogs.h"

namespace {
	std::wstring GetText(CWindow win) {
		CString text;
		win.GetWindowText(text);
		return std::wstring(text);
	}

	int HexDigit(wchar_t ch) {
		if (ch >= L'0' && ch <= L'9')
			return ch - L'0';
		ch = (wchar_t)towlower(ch);
		return ch >= L'a' && ch <= L'f' ? ch - L'a' + 10 : -1;
	}
}

std::vector<uint8_t> MemorySearch::GetPattern(std::wstring* error) const {
	std::vector<uint8_t> pattern;
	switch (Type) {
		case Kind::Ascii:
			for (auto ch : Text) {
				if (ch > 0xFF) {
					if (error)
						*error = L"The text has characters beyond Latin-1; search for it as Unicode.";
					return {};
				}
				pattern.push_back((uint8_t)ch);
			}
			break;
		case Kind::Unicode:
			pattern.resize(Text.size() * sizeof(wchar_t));
			::memcpy(pattern.data(), Text.data(), pattern.size());
			break;
		case Kind::Hex:
		{
			// pairs of hex digits, with spaces (or nothing) between the bytes
			int high = -1;
			for (auto ch : Text) {
				if (ch == L' ' || ch == L',' || ch == L'-')
					continue;
				int digit = HexDigit(ch);
				if (digit < 0) {
					if (error)
						*error = L"Type the bytes in hex, such as \"4D 5A 90 00\".";
					return {};
				}
				if (high < 0)
					high = digit;
				else {
					pattern.push_back(uint8_t(high << 4 | digit));
					high = -1;
				}
			}
			if (high >= 0) {
				if (error)
					*error = L"Each byte takes two hex digits.";
				return {};
			}
			break;
		}
	}
	if (pattern.empty() && error)
		*error = L"Type what to look for.";
	return pattern;
}

LRESULT CMemoryFindDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetDlgItemText(IDC_MFIND_TEXT, m_Search.Text.c_str());
	CheckRadioButton(IDC_MFIND_ASCII, IDC_MFIND_HEX, IDC_MFIND_ASCII + (int)m_Search.Type);
	CheckDlgButton(IDC_MFIND_CASE, m_Search.IgnoreCase ? BST_UNCHECKED : BST_CHECKED);
	// without a selected region, there's only the whole address space
	bool whole = m_Search.WholeSpace || !m_HasRegion;
	CheckRadioButton(IDC_MFIND_REGION, IDC_MFIND_ALL, whole ? IDC_MFIND_ALL : IDC_MFIND_REGION);
	GetDlgItem(IDC_MFIND_REGION).EnableWindow(m_HasRegion);
	BOOL handled;
	OnKind(0, 0, nullptr, handled);
	CenterWindow(GetParent());
	GotoDlgCtrl(GetDlgItem(IDC_MFIND_TEXT));
	CEdit(GetDlgItem(IDC_MFIND_TEXT)).SetSelAll();
	return FALSE;
}

LRESULT CMemoryFindDlg::OnKind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// case applies to text
	GetDlgItem(IDC_MFIND_CASE).EnableWindow(IsDlgButtonChecked(IDC_MFIND_HEX) != BST_CHECKED);
	return 0;
}

LRESULT CMemoryFindDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	MemorySearch search;
	search.Text = GetText(GetDlgItem(IDC_MFIND_TEXT));
	search.Type = IsDlgButtonChecked(IDC_MFIND_HEX) == BST_CHECKED ? MemorySearch::Kind::Hex
		: IsDlgButtonChecked(IDC_MFIND_UNICODE) == BST_CHECKED ? MemorySearch::Kind::Unicode : MemorySearch::Kind::Ascii;
	search.IgnoreCase = IsDlgButtonChecked(IDC_MFIND_CASE) != BST_CHECKED;
	search.WholeSpace = IsDlgButtonChecked(IDC_MFIND_ALL) == BST_CHECKED;
	std::wstring error;
	if (search.GetPattern(&error).empty()) {
		AtlMessageBox(m_hWnd, error.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		GotoDlgCtrl(GetDlgItem(IDC_MFIND_TEXT));
		return 0;
	}
	m_Search = std::move(search);
	EndDialog(IDOK);
	return 0;
}

LRESULT CMemoryFindDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}

LRESULT CMemoryGoToDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_Address)
		SetDlgItemText(IDC_MGOTO_ADDRESS, std::format(L"{:X}", m_Address).c_str());
	CenterWindow(GetParent());
	GotoDlgCtrl(GetDlgItem(IDC_MGOTO_ADDRESS));
	CEdit(GetDlgItem(IDC_MGOTO_ADDRESS)).SetSelAll();
	return FALSE;
}

LRESULT CMemoryGoToDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto text = GetText(GetDlgItem(IDC_MGOTO_ADDRESS));
	// as debuggers show addresses: "0x" and the backtick are optional
	std::erase_if(text, [](wchar_t ch) { return ch == L'`' || ch == L' '; });
	if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X'))
		text.erase(0, 2);
	uint64_t address = 0;
	bool valid = !text.empty() && text.size() <= 16;
	for (auto ch : text) {
		int digit = HexDigit(ch);
		if (digit < 0) {
			valid = false;
			break;
		}
		address = address << 4 | digit;
	}
	if (!valid) {
		AtlMessageBox(m_hWnd, L"Type an address in hex, such as 7FF6`12340000.", IDR_MAINFRAME, MB_ICONWARNING);
		GotoDlgCtrl(GetDlgItem(IDC_MGOTO_ADDRESS));
		return 0;
	}
	m_Address = address;
	EndDialog(IDOK);
	return 0;
}

LRESULT CMemoryGoToDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
