#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "TimelineModel.h"
#include <WinSys/ProcessCreation.h>

class CTimelineView;

//
// The Timeline tab's chart: a row per process, its name and PID on the left, a bar from its start to its exit (or
// now) along the time axis above; running processes blue, exited ones gray, those that exited with a code other than 0
// red; a line from the parent's row to the child's start where both are shown; the recording's start and now marked.
// Ctrl+wheel (or + and -) zooms around the mouse, Shift+wheel (or dragging, or the arrow keys) pans, the wheel scrolls
// the rows, Home fits the whole recording. A click selects a row; the tooltip describes the process under the mouse.
//
class CTimelineCtrl : public CWindowImpl<CTimelineCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLive_Timeline", CS_DBLCLKS, COLOR_WINDOW)

	explicit CTimelineCtrl(CTimelineView& view) : m_View(view) {}

	void SetRows(std::vector<TimelineRow> rows, int64_t from, int64_t to, int64_t now, int64_t since);
	void SetFont(HFONT font);
	// the selected row's lifetime; nullptr for none
	HistoryRecorder::ProcessLife const* GetSelected() const;

	BEGIN_MSG_MAP(CTimelineCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		NOTIFY_CODE_HANDLER(TTN_GETDISPINFO, OnGetTipText)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
		return DLGC_WANTARROWS | DLGC_WANTCHARS;
	}
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&) {
		Invalidate();
		return 0;
	}
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetTipText(int, LPNMHDR, BOOL&);

	void Measure();
	void UpdateScrollBar();
	// the row at the client y, or -1
	int RowAt(int y) const;
	void Select(int row);
	void ScrollTo(int top);
	// the chart's left edge and width (right of the names)
	int ChartLeft() const {
		return m_NameWidth;
	}
	int ChartWidth() const;

	CTimelineView& m_View;
	std::vector<TimelineRow> m_Rows;
	int64_t m_From{ 0 }, m_To{ 0 }, m_Now{ 0 }, m_Since{ 0 };
	int m_Top{ 0 };			// the first row shown
	int m_Selected{ -1 };
	HistoryRecorder::ProcessLife const* m_SelectedLife{ nullptr };	// kept across SetRows by key
	WinSys::ProcessOrThreadKey m_SelectedKey{};
	HFONT m_Font{ nullptr };
	CFont m_DefaultFont;
	int m_RowHeight{ 20 }, m_AxisHeight{ 30 }, m_NameWidth{ 260 };
	// dragging pans
	bool m_Dragging{ false };
	int m_DragX{ 0 };
	int64_t m_DragFrom{ 0 }, m_DragTo{ 0 };
	CToolTipCtrl m_Tip;
	int m_TipRow{ -1 };
	std::wstring m_TipText;
};

//
// The Timeline tab (View > Timeline): what ran when, from the History recorder's process lifetimes (HistoryRecorder::
// ProcessLife: from the tab's opening, or the app's start with Record from Startup), in CTimelineCtrl. The range is the
// recording at first, following now; zooming or panning stops following until Fit. Hide Running Throughout (on at
// first, the TimelineHideThroughout setting) leaves out what ran through the whole range shown, so an install's or a
// boot's processes stand out. The filter matches the name, PID, command line and user. With the process creation tracker
// (elevated) the tooltip also says who created a process that another created for its parent, or whose parent was
// spoofed. Double-click or Go to Process selects a running process in the Processes tab; Copy copies its description.
//
class CTimelineView : public CViewBase<CTimelineView> {
public:
	explicit CTimelineView(IMainFrame* frame) : CViewBase(frame), m_Chart(*this) {}

	CString GetTitle() const override {
		return L"Timeline";
	}
	bool CanCopy() const override {
		return m_Chart.GetSelected() != nullptr;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void UpdateLayout(BOOL resizeBars = TRUE);

	// for the chart: the range changed by zooming or panning (following stops unless it reaches now)
	void SetRange(int64_t from, int64_t to);
	void Fit();
	std::pair<int64_t, int64_t> GetRange() const {
		return { m_From, m_To };
	}
	std::wstring Describe(HistoryRecorder::ProcessLife const& life) const;
	void GoToProcess(HistoryRecorder::ProcessLife const& life);
	void ShowRowMenu(CPoint pt);

	BEGIN_MSG_MAP(CTimelineView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_TIMELINE_HIDETHROUGHOUT, OnHideThroughout)
		COMMAND_ID_HANDLER(ID_TIMELINE_FIT, OnFit)
		CHAIN_MSG_MAP(CViewBase<CTimelineView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_TIMELINE_GOTOPROCESS, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_TIMELINE_HIDETHROUGHOUT, OnHideThroughout)
		COMMAND_ID_HANDLER(ID_TIMELINE_FIT, OnFit)
		CHAIN_MSG_MAP_ALT(CViewBase<CTimelineView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideThroughout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the rows from the recorder's lifetimes for the range and the options
	void Rebuild();
	void UpdateStatus();

	CTimelineCtrl m_Chart;
	CFilterBox m_Filter;
	CToolBarCtrl m_Bar;	// Hide Running Throughout, Fit
	CFont m_BarFont;
	std::shared_ptr<HistoryRecorder> m_Recorder;
	std::shared_ptr<WinSys::ProcessCreationTracker> m_Creations;	// elevated only
	int64_t m_From{ 0 }, m_To{ 0 };
	bool m_Follow{ true };	// the range's end is now
	bool m_HideThroughout{ true };
	bool m_Active{ false };
	size_t m_Shown{ 0 }, m_Hidden{ 0 };
};
