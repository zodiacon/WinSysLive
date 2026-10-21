#pragma once

#include <functional>

// the outline drawn around the window under the cursor while the finder is dragged: a topmost popup whose region is
// a frame, layered and transparent, so it takes no mouse input and WindowFromPoint looks through it
class CWindowHighlight : public CWindowImpl<CWindowHighlight> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_WindowHighlight", 0, -1)

	BEGIN_MSG_MAP(CWindowHighlight)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

	// around the window (its visible bounds); nullptr hides the outline
	void Show(HWND hTarget);

private:
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	HWND m_hTarget{ nullptr };
};

// Process Explorer's window finder: a toolbar button that's dragged (not clicked) onto a window. The toolbar is
// subclassed, so a press on the button starts the drag instead of the toolbar's own handling; while dragging, the
// window under the cursor is outlined and given to Hover, and releasing the button gives it to Found (Escape, or
// losing the capture, cancels: Hover gets nullptr).
class CWindowFinder : public CWindowImpl<CWindowFinder, CToolBarCtrl> {
public:
	BEGIN_MSG_MAP(CWindowFinder)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
	END_MSG_MAP()

	// Hover: the window under the cursor while dragging (nullptr when there's none, and when the drag ends)
	void Init(HWND hToolBar, UINT commandId, std::function<void(HWND)> hover, std::function<void(HWND)> found);
	bool IsDragging() const;
	void Cancel();

	// the window whose process the finder picks at a screen point: the outermost window of the same process as the
	// window there, which is the top-level window unless it hosts another process's (a Store app's CoreWindow)
	static HWND WindowFromPoint(POINT pt);

private:
	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCaptureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	void SetTarget(HWND hWnd);
	void EndDrag();

	std::function<void(HWND)> m_Hover, m_Found;
	CWindowHighlight m_Highlight;
	HWND m_hTarget{ nullptr };
	UINT m_CommandId{ 0 };
	bool m_Dragging{ false };
};
