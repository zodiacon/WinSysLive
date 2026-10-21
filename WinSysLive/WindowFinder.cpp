#include "pch.h"
#include "WindowFinder.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi")

namespace {
	const int OutlineWidth = 3;	// at 96 DPI
	const COLORREF OutlineColor = RGB(255, 0, 0);
}

void CWindowHighlight::Show(HWND hTarget) {
	m_hTarget = hTarget;
	if (!hTarget) {
		ShowWindow(SW_HIDE);
		return;
	}
	// a top-level window's visible bounds, without the invisible resize borders
	CRect rc;
	if (::GetAncestor(hTarget, GA_ROOT) != hTarget
		|| FAILED(::DwmGetWindowAttribute(hTarget, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc))))
		::GetWindowRect(hTarget, &rc);

	auto dpi = ::GetDpiForWindow(hTarget);
	auto width = ::MulDiv(OutlineWidth, dpi ? dpi : USER_DEFAULT_SCREEN_DPI, USER_DEFAULT_SCREEN_DPI);
	CRgn frame, inner;
	frame.CreateRectRgn(0, 0, rc.Width(), rc.Height());
	inner.CreateRectRgn(width, width, rc.Width() - width, rc.Height() - width);
	frame.CombineRgn(inner, RGN_DIFF);
	// the window owns the region
	SetWindowRgn(frame.Detach(), FALSE);
	SetWindowPos(HWND_TOPMOST, &rc, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	Invalidate();
}

LRESULT CWindowHighlight::OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CDCHandle dc((HDC)wParam);
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, OutlineColor);
	return 1;
}

void CWindowFinder::Init(HWND hToolBar, UINT commandId, std::function<void(HWND)> hover, std::function<void(HWND)> found) {
	m_CommandId = commandId;
	m_Hover = std::move(hover);
	m_Found = std::move(found);
	SubclassWindow(hToolBar);
}

bool CWindowFinder::IsDragging() const {
	return m_Dragging;
}

void CWindowFinder::Cancel() {
	if (m_Dragging)
		EndDrag();
}

HWND CWindowFinder::WindowFromPoint(POINT pt) {
	auto hWnd = ::WindowFromPoint(pt);
	if (!hWnd)
		return nullptr;
	DWORD pid = 0;
	::GetWindowThreadProcessId(hWnd, &pid);
	auto hDesktop = ::GetDesktopWindow();
	for (auto hParent = ::GetAncestor(hWnd, GA_PARENT); hParent && hParent != hDesktop; hParent = ::GetAncestor(hParent, GA_PARENT)) {
		DWORD parentPid = 0;
		::GetWindowThreadProcessId(hParent, &parentPid);
		if (parentPid != pid)
			break;
		hWnd = hParent;
	}
	return hWnd;
}

LRESULT CWindowFinder::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	auto index = HitTest(&pt);
	TBBUTTON button{};
	if (index < 0 || !GetButton(index, &button) || button.idCommand != (int)m_CommandId || !IsButtonEnabled(m_CommandId)) {
		bHandled = FALSE;
		return 0;
	}

	// the outline is created for each drag, so it never outlives one
	m_Highlight.Create(nullptr, rcDefault, nullptr, WS_POPUP,
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
	::SetLayeredWindowAttributes(m_Highlight, 0, 255, LWA_ALPHA);
	m_Dragging = true;
	PressButton(m_CommandId, TRUE);
	SetCapture();
	::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
	return 0;
}

LRESULT CWindowFinder::OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	if (!m_Dragging) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt;
	::GetCursorPos(&pt);
	SetTarget(WindowFromPoint(pt));
	return 0;
}

LRESULT CWindowFinder::OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	if (!m_Dragging) {
		bHandled = FALSE;
		return 0;
	}
	auto hTarget = m_hTarget;
	EndDrag();
	if (hTarget && ::IsWindow(hTarget) && m_Found)
		m_Found(hTarget);
	return 0;
}

LRESULT CWindowFinder::OnCaptureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// another window took the mouse (Alt+Tab, a message box): the drag is over
	if (m_Dragging)
		EndDrag();
	bHandled = FALSE;
	return 0;
}

void CWindowFinder::SetTarget(HWND hWnd) {
	if (hWnd == m_hTarget)
		return;
	m_hTarget = hWnd;
	m_Highlight.Show(hWnd);
	if (m_Hover)
		m_Hover(hWnd);
}

void CWindowFinder::EndDrag() {
	// not dragging before releasing the capture, whose WM_CAPTURECHANGED comes back here otherwise
	m_Dragging = false;
	if (::GetCapture() == m_hWnd)
		::ReleaseCapture();
	PressButton(m_CommandId, FALSE);
	SetTarget(nullptr);
	m_Highlight.DestroyWindow();
}
