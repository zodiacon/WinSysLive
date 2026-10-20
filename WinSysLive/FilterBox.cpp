#include "pch.h"
#include "FilterBox.h"

HWND CFilterBox::Create(HWND hRebar, UINT id, int width, PCWSTR cue, HWND hList, Callback applied) {
	m_hList = hList;
	m_Applied = std::move(applied);
	CRect rc(0, 0, width, ::MulDiv(22, ::GetDpiForWindow(hRebar), 96));
	m_Edit.Create(hRebar, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, id);
	m_Edit.SetCueBannerText(cue, TRUE);
	::SetWindowSubclass(m_Edit, SubclassProc, 0, (DWORD_PTR)this);
	return m_Edit;
}

HWND CFilterBox::GetHwnd() const {
	return m_Edit;
}

void CFilterBox::SetFont(HFONT font) {
	m_Edit.SetFont(font);
}

void CFilterBox::Changed() {
	m_Edit.SetTimer(TimerId, Delay);
}

void CFilterBox::Focus() {
	m_Edit.SetFocus();
	m_Edit.SetSelAll();
}

void CFilterBox::Clear() {
	m_Edit.SetWindowText(L"");
	// now, not after the pause that EN_CHANGE started
	Apply();
}

std::wstring const& CFilterBox::GetText() const {
	return m_Text;
}

bool CFilterBox::Contains(std::wstring text, std::wstring const& filter) {
	::CharLowerBuff(text.data(), (DWORD)text.size());
	return text.find(filter) != std::wstring::npos;
}

void CFilterBox::Apply() {
	m_Edit.KillTimer(TimerId);
	CString text;
	m_Edit.GetWindowText(text);
	text.Trim();
	std::wstring filter(text);
	::CharLowerBuff(filter.data(), (DWORD)filter.size());
	if (filter == m_Text)
		return;
	m_Text = std::move(filter);
	CWaitCursor wait;
	m_Applied(m_Text);
}

LRESULT CALLBACK CFilterBox::SubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	auto box = reinterpret_cast<CFilterBox*>(data);
	switch (msg) {
		case WM_TIMER:
			if (wParam == TimerId) {
				box->Apply();
				return 0;
			}
			break;
		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				if (box->m_Edit.GetWindowTextLength() > 0)
					box->Clear();
				else
					::SetFocus(box->m_hList);
				return 0;
			}
			if (wParam == VK_RETURN || wParam == VK_DOWN) {
				box->Apply();
				::SetFocus(box->m_hList);
				return 0;
			}
			break;
		case WM_CHAR:
			// no beep for the keys handled above
			if (wParam == VK_ESCAPE || wParam == VK_RETURN)
				return 0;
			break;
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, SubclassProc, 0);
			break;
	}
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}
