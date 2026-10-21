#pragma once

#include <DpiHelper.h>

//
// Dialog layouts for the property pages of a per-monitor DPI aware app. A page is a child dialog: when its DPI changes it
// gets WM_DPICHANGED_AFTERPARENT (not WM_DPICHANGED, the only one WTL's layouts know). The dialog manager would move its
// controls by the new font's dialog units, which isn't the DPI's ratio the layouts scale their data by (rows of a page
// came apart by a few pixels each), so it only scales the font (DDC_DISABLE_CONTROL_RELAYOUT): every control is moved by
// the ratio here, and then the layout follows the page's size again.
//

namespace DpiLayout {
	// every child of the dialog (in its client coordinates) scaled from one DPI to another
	inline void ScaleChildren(HWND hDlg, UINT toDpi, UINT fromDpi) {
		std::vector<HWND> children;
		for (auto h = ::GetWindow(hDlg, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT))
			children.push_back(h);
		auto hDwp = ::BeginDeferWindowPos((int)children.size());
		for (auto h : children) {
			CRect rc;
			::GetWindowRect(h, &rc);
			::MapWindowPoints(nullptr, hDlg, (POINT*)&rc, 2);
			rc = DpiHelper::Scale(rc, toDpi, fromDpi);
			hDwp = ::DeferWindowPos(hDwp, h, nullptr, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		}
		::EndDeferWindowPos(hDwp);
	}

	// the dialog manager scales the font for a new DPI, the page moves the controls
	inline void DisableControlRelayout(HWND hDlg) {
		::SetDialogDpiChangeBehavior(hDlg, DDC_DISABLE_CONTROL_RELAYOUT, DDC_DISABLE_CONTROL_RELAYOUT);
	}
}

//
// CDynamicDialogLayout (AFX_DIALOG_LAYOUT) keeps the controls' and the page's first rectangles in pixels of the DPI it
// started at: they're scaled to each new DPI. A page calls InitDynamicLayout as before (this one hides the base's)
//
template<typename T>
class CDpiDynamicDialogLayout : public CDynamicDialogLayout<T> {
public:
	using Base = CDynamicDialogLayout<T>;

	void InitDynamicLayout(bool bAddGripper = true, bool bMinTrackSize = true) {
		auto hWnd = static_cast<T*>(this)->m_hWnd;
		Base::InitDynamicLayout(bAddGripper, bMinTrackSize);
		m_LayoutDpi = DpiHelper::GetDpi(hWnd);
		DpiLayout::DisableControlRelayout(hWnd);
	}

	BEGIN_MSG_MAP(CDpiDynamicDialogLayout)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChangedAfterParent)
		CHAIN_MSG_MAP(Base)
	END_MSG_MAP()

private:
	LRESULT OnDpiChangedAfterParent(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		auto pT = static_cast<T*>(this);
		auto dpi = DpiHelper::GetDpi(pT->m_hWnd);
		if (m_LayoutDpi && dpi != m_LayoutDpi) {
			DpiLayout::ScaleChildren(pT->m_hWnd, dpi, m_LayoutDpi);
			auto scale = [&](auto value) { return DpiHelper::Scale(value, dpi, m_LayoutDpi); };
			for (int i = 0; i < this->m_arrLayoutData.GetSize(); i++) {
				auto& rc = this->m_arrLayoutData[i].m_rcInit;
				rc = { scale(rc.left), scale(rc.top), scale(rc.right), scale(rc.bottom) };
			}
			this->m_szParentInit = { scale(this->m_szParentInit.cx), scale(this->m_szParentInit.cy) };
			if (this->m_ptMinTrackSize.x != -1)
				this->m_ptMinTrackSize = { scale(this->m_ptMinTrackSize.x), scale(this->m_ptMinTrackSize.y) };
			m_LayoutDpi = dpi;
			this->UpdateDynamicLayout();
		}
		bHandled = FALSE;	// the dialog manager's font
		return 0;
	}

	UINT m_LayoutDpi{ 0 };	// the DPI of the layout data (0: not initialized)
};

//
// A top-level dialog with CDialogResize: WTL's handles WM_DPICHANGED, but doesn't take the size Windows suggests (the
// dialog came out too narrow for its controls), and the dialog manager's own move of the controls wouldn't match
// CDialogResize's data. So the dialog takes the suggested rectangle, its controls are moved by the DPI's ratio, and the
// layout is done again. Chain this before CDialogResize, and call InitDpi after DlgResize_Init
//
template<typename T>
class CDpiDialogResize {
public:
	void InitDpi() {
		::SetDialogDpiChangeBehavior(static_cast<T*>(this)->m_hWnd, DDC_DISABLE_RESIZE | DDC_DISABLE_CONTROL_RELAYOUT,
			DDC_DISABLE_RESIZE | DDC_DISABLE_CONTROL_RELAYOUT);
	}

	BEGIN_MSG_MAP(CDpiDialogResize)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
	END_MSG_MAP()

private:
	LRESULT OnDpiChanged(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
		auto pT = static_cast<T*>(this);
		UINT dpi = LOWORD(wParam);
		if (pT->m_uDPI && dpi != pT->m_uDPI)
			DpiLayout::ScaleChildren(pT->m_hWnd, dpi, pT->m_uDPI);
		pT->m_uDPI = dpi;
		pT->SetWindowPos(nullptr, reinterpret_cast<RECT const*>(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
		// the size may not have changed, which leaves the layout to do here
		CRect rc;
		pT->GetClientRect(&rc);
		pT->DlgResize_UpdateLayout(rc.Width(), rc.Height());
		return 0;
	}
};

//
// CDialogResize keeps its data at 96 DPI (_WTL_DPI_SUPPORT) and scales it to the DPI it's told about, which for a page
// comes with WM_DPICHANGED_AFTERPARENT. Chain this before CDialogResize, and call InitDpi after DlgResize_Init
//
template<typename T>
class CDpiDialogResizePage {
public:
	void InitDpi() {
		DpiLayout::DisableControlRelayout(static_cast<T*>(this)->m_hWnd);
	}

	BEGIN_MSG_MAP(CDpiDialogResizePage)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChangedAfterParent)
	END_MSG_MAP()

private:
	LRESULT OnDpiChangedAfterParent(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		auto pT = static_cast<T*>(this);
		auto dpi = DpiHelper::GetDpi(pT->m_hWnd);
		if (pT->m_uDPI && dpi != pT->m_uDPI) {
			DpiLayout::ScaleChildren(pT->m_hWnd, dpi, pT->m_uDPI);
			pT->m_uDPI = dpi;
			CRect rc;
			pT->GetClientRect(&rc);
			pT->DlgResize_UpdateLayout(rc.Width(), rc.Height());
		}
		bHandled = FALSE;	// the dialog manager's font
		return 0;
	}
};
