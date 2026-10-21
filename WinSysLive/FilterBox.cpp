#include "pch.h"
#include "FilterBox.h"
#include <DpiHelper.h>

HWND CFilterBox::Create(HWND hRebar, UINT id, int width, PCWSTR cue, HWND hList, Callback applied) {
	m_hList = hList;
	m_Applied = std::move(applied);
	m_Width = width;
	auto dpi = DpiHelper::GetDpi(hRebar);
	CRect rc(0, 0, DpiHelper::Scale(width, dpi), DpiHelper::Scale(22, dpi));
	m_Edit.Create(hRebar, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, id);
	m_Edit.SetCueBannerText(cue, TRUE);
	SetFont(nullptr);
	::SetWindowSubclass(m_Edit, SubclassProc, 0, (DWORD_PTR)this);
	return m_Edit;
}

HWND CFilterBox::GetHwnd() const {
	return m_Edit;
}

int CFilterBox::GetWidth() const {
	return DpiHelper::Scale(m_Width, m_Edit.m_hWnd);
}

int CFilterBox::GetHeight() const {
	return DpiHelper::Scale(22, m_Edit.m_hWnd);
}

void CFilterBox::SetFont(HFONT font) {
	m_ViewFont = font;
	if (!font) {
		if (!m_OwnFont.IsNull())
			m_OwnFont.DeleteObject();
		m_OwnFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_Edit)));
		font = m_OwnFont;
	}
	m_Edit.SetFont(font);
}

void CFilterBox::OnDpiChanged() {
	// a view's font comes again from the view, for the new DPI (IView::OnFontChanged)
	if (!m_ViewFont)
		SetFont(nullptr);
	m_Edit.SetWindowPos(nullptr, 0, 0, GetWidth(), GetHeight(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	CWindow parent(m_Edit.GetParent());
	WCHAR className[32]{};
	::GetClassName(parent, className, _countof(className));
	if (::_wcsicmp(className, REBARCLASSNAME) == 0) {
		CReBarCtrl rebar(parent);
		for (UINT i = 0; i < rebar.GetBandCount(); i++) {
			REBARBANDINFO band{ sizeof(band), RBBIM_CHILD };
			if (rebar.GetBandInfo(i, &band) && band.hwndChild == m_Edit) {
				band.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
				band.cxMinChild = band.cx = band.cxIdeal = GetWidth();
				band.cyMinChild = GetHeight();
				rebar.SetBandInfo(i, &band);
				break;
			}
		}
	}
	else {
		// the parent laid itself out before this (its WM_DPICHANGED_AFTERPARENT comes first), with the old height
		CRect rc;
		parent.GetClientRect(&rc);
		parent.SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.Width(), rc.Height()));
	}
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
		case WM_DPICHANGED_AFTERPARENT:
			box->OnDpiChanged();
			break;
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, SubclassProc, 0);
			break;
	}
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}
