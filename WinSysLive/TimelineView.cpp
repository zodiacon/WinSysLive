#include "pch.h"
#include "TimelineView.h"
#include "Recorders.h"
#include "AppSettings.h"
#include <WTLHelper.h>
#include <DpiHelper.h>
#include <ToolbarHelper.h>
#include <ClipboardHelper.h>
#include <WinSys/SecurityHelper.h>

namespace {
	const ToolBarButtonInfo s_Buttons[] = {
		{ ID_TIMELINE_HIDETHROUGHOUT, IDI_PROCESS_HIDE, BTNS_CHECK | BTNS_AUTOSIZE, L"Hide Running Throughout" },
		{ ID_TIMELINE_FIT, IDI_REFRESH, BTNS_AUTOSIZE, L"Fit" },
	};
	const int ButtonImageSize = 16;	// at 96 DPI
	const int64_t MinSpan = TimelineModel::Second;	// zoomed in at most to a second
	const int TickPixels = 90;	// at 96 DPI, at least between ticks

	struct Colors {
		COLORREF Back, Text, Dim, Grid, Axis, Selected, Running, Exited, Failed, Link, Now, Since;
	};
	Colors GetColors() {
		if (WTLHelper::IsDarkMode())
			return { RGB(32, 32, 32), RGB(230, 230, 230), RGB(150, 150, 150), RGB(55, 55, 55), RGB(45, 45, 45), RGB(45, 70, 105),
				RGB(70, 135, 210), RGB(115, 115, 115), RGB(200, 65, 65), RGB(175, 160, 80), RGB(90, 200, 90), RGB(200, 160, 60) };
		return { RGB(255, 255, 255), RGB(0, 0, 0), RGB(110, 110, 110), RGB(232, 232, 232), RGB(242, 242, 242), RGB(204, 228, 247),
			RGB(70, 140, 225), RGB(175, 175, 175), RGB(225, 80, 80), RGB(170, 140, 40), RGB(40, 160, 40), RGB(200, 140, 20) };
	}

	std::wstring TickText(int64_t time, int64_t step) {
		FILETIME local;
		SYSTEMTIME st{};
		if (!::FileTimeToLocalFileTime((FILETIME const*)&time, &local) || !::FileTimeToSystemTime(&local, &st))
			return L"";
		if (step >= 86400 * TimelineModel::Second)
			return std::format(L"{:02}-{:02}", st.wMonth, st.wDay);
		if (step >= 60 * TimelineModel::Second)
			return std::format(L"{:02}:{:02}", st.wHour, st.wMinute);
		return std::format(L"{:02}:{:02}:{:02}", st.wHour, st.wMinute, st.wSecond);
	}

	int64_t LocalOffset() {
		// ticks fall on local whole seconds, minutes and hours
		TIME_ZONE_INFORMATION tz;
		auto id = ::GetTimeZoneInformation(&tz);
		auto bias = tz.Bias + (id == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : 0);
		return -(int64_t)bias * 60 * TimelineModel::Second;
	}
}

//
// CTimelineCtrl
//

LRESULT CTimelineCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_DefaultFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_hWnd)));
	m_Font = m_DefaultFont;
	Measure();
	m_Tip.Create(m_hWnd, nullptr, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP);
	m_Tip.SetMaxTipWidth(DpiHelper::Scale(600, m_hWnd));
	m_Tip.SetDelayTime(TTDT_AUTOPOP, 30000);
	CToolInfo ti(TTF_IDISHWND | TTF_SUBCLASS, m_hWnd, (UINT_PTR)m_hWnd, nullptr, LPSTR_TEXTCALLBACK);
	m_Tip.AddTool(&ti);
	return 0;
}

void CTimelineCtrl::SetFont(HFONT font) {
	m_Font = font ? font : (HFONT)m_DefaultFont;
	Measure();
	UpdateScrollBar();
	Invalidate();
}

LRESULT CTimelineCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	bool own = m_Font == (HFONT)m_DefaultFont;
	m_DefaultFont.DeleteObject();
	m_DefaultFont.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(m_hWnd)));
	if (own)
		m_Font = m_DefaultFont;
	Measure();
	UpdateScrollBar();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

void CTimelineCtrl::Measure() {
	CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_Font);
	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	dc.SelectFont(old);
	m_RowHeight = tm.tmHeight + tm.tmHeight / 2;
	m_AxisHeight = tm.tmHeight * 2;
	m_NameWidth = DpiHelper::Scale(260, m_hWnd);
}

int CTimelineCtrl::ChartWidth() const {
	CRect rc;
	GetClientRect(&rc);
	return (std::max)(10, (int)rc.Width() - m_NameWidth - DpiHelper::Scale(8, m_hWnd));
}

void CTimelineCtrl::SetRows(std::vector<TimelineRow> rows, int64_t from, int64_t to, int64_t now, int64_t since) {
	m_Rows = std::move(rows);
	m_From = from;
	m_To = to;
	m_Now = now;
	m_Since = since;
	// the selection by its process
	m_Selected = -1;
	m_SelectedLife = nullptr;
	for (int i = 0; i < (int)m_Rows.size(); i++)
		if (m_Rows[i].Life->Key == m_SelectedKey) {
			m_Selected = i;
			m_SelectedLife = m_Rows[i].Life;
			break;
		}
	UpdateScrollBar();
	Invalidate();
}

HistoryRecorder::ProcessLife const* CTimelineCtrl::GetSelected() const {
	return m_SelectedLife;
}

void CTimelineCtrl::UpdateScrollBar() {
	if (!m_hWnd)
		return;
	CRect rc;
	GetClientRect(&rc);
	int page = (std::max)(1, ((int)rc.Height() - m_AxisHeight) / m_RowHeight);
	m_Top = std::clamp(m_Top, 0, (std::max)(0, (int)m_Rows.size() - page));
	SCROLLINFO si{ sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL };
	si.nMin = 0;
	si.nMax = (int)m_Rows.size() - 1;
	si.nPage = page;
	si.nPos = m_Top;
	SetScrollInfo(SB_VERT, &si);
}

void CTimelineCtrl::ScrollTo(int top) {
	CRect rc;
	GetClientRect(&rc);
	int page = (std::max)(1, ((int)rc.Height() - m_AxisHeight) / m_RowHeight);
	top = std::clamp(top, 0, (std::max)(0, (int)m_Rows.size() - page));
	if (top == m_Top)
		return;
	m_Top = top;
	UpdateScrollBar();
	Invalidate();
}

int CTimelineCtrl::RowAt(int y) const {
	if (y < m_AxisHeight)
		return -1;
	int row = m_Top + (y - m_AxisHeight) / m_RowHeight;
	return row < (int)m_Rows.size() ? row : -1;
}

void CTimelineCtrl::Select(int row) {
	if (row < 0 || row >= (int)m_Rows.size())
		return;
	m_Selected = row;
	m_SelectedLife = m_Rows[row].Life;
	m_SelectedKey = m_SelectedLife->Key;
	// kept in view
	CRect rc;
	GetClientRect(&rc);
	int page = (std::max)(1, ((int)rc.Height() - m_AxisHeight) / m_RowHeight);
	if (row < m_Top)
		ScrollTo(row);
	else if (row >= m_Top + page)
		ScrollTo(row - page + 1);
	Invalidate();
}

LRESULT CTimelineCtrl::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	CPaintDC pdc(m_hWnd);
	CRect rc;
	GetClientRect(&rc);
	CMemoryDC dc(pdc, rc);
	auto colors = GetColors();
	dc.FillSolidRect(&rc, colors.Back);
	auto oldFont = dc.SelectFont(m_Font);
	dc.SetBkMode(TRANSPARENT);

	int left = ChartLeft(), width = ChartWidth();
	auto x = [&](int64_t t) { return TimelineModel::X(t, m_From, m_To, left, width); };
	int pad = DpiHelper::Scale(4, m_hWnd);

	// the axis: its band, the ticks and their grid lines
	CRect axis(rc.left, rc.top, rc.right, rc.top + m_AxisHeight);
	dc.FillSolidRect(&axis, colors.Axis);
	auto step = TimelineModel::TickStep(m_To - m_From, width, DpiHelper::Scale(TickPixels, m_hWnd));
	auto offset = LocalOffset();
	// the first tick: the step's first multiple (local time) in the range
	auto first = ((m_From + offset) / step + 1) * step - offset;
	dc.SetTextColor(colors.Dim);
	for (auto t = first; t <= m_To; t += step) {
		int tx = x(t);
		dc.FillSolidRect(tx, rc.top + m_AxisHeight - pad, 1, (int)rc.Height(), colors.Grid);
		auto text = TickText(t, step);
		CRect rcText(tx - 100, rc.top, tx + 100, rc.top + m_AxisHeight - pad);
		dc.DrawText(text.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	dc.SetTextColor(colors.Text);
	CRect title(rc.left + pad, rc.top, left - pad, rc.top + m_AxisHeight);
	dc.DrawText(std::format(L"{} processes", m_Rows.size()).c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

	// the rows on screen
	int page = ((int)rc.Height() - m_AxisHeight) / m_RowHeight + 1;
	int barInset = m_RowHeight / 5;
	bool focused = ::GetFocus() == m_hWnd;
	for (int i = m_Top; i < (int)m_Rows.size() && i < m_Top + page; i++) {
		auto& life = *m_Rows[i].Life;
		int y = rc.top + m_AxisHeight + (i - m_Top) * m_RowHeight;
		if (i == m_Selected)
			dc.FillSolidRect(rc.left, y, rc.Width(), m_RowHeight, focused ? colors.Selected : colors.Grid);
		CRect name(rc.left + pad, y, left - pad, y + m_RowHeight);
		dc.SetTextColor(life.End ? colors.Dim : colors.Text);
		dc.DrawText(std::format(L"{} ({})", life.Name, life.Key.Id).c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		int x1 = (std::max)(x(life.Start), left);
		int x2 = (std::min)(x(TimelineModel::EndOf(life, m_Now)), left + width);
		// a process of a few milliseconds still shows
		if (int least = (std::max)(3, DpiHelper::Scale(3, m_hWnd)); x2 < x1 + least)
			x2 = x1 + least;
		auto color = !life.End ? colors.Running : life.ExitCode && *life.ExitCode != 0 ? colors.Failed : colors.Exited;
		dc.FillSolidRect(x1, y + barInset, x2 - x1, m_RowHeight - 2 * barInset, color);
	}
	// the parents' links, over the bars: from the parent's row down (or up) to the child's start
	CPen linkPen;
	linkPen.CreatePen(PS_SOLID, (std::max)(1, DpiHelper::Scale(1, m_hWnd)), colors.Link);
	auto oldPen = dc.SelectPen(linkPen);
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		int parent = m_Rows[i].ParentRow;
		if (parent < 0)
			continue;
		// a link crosses the screen if either end is on it, or they're on both sides
		int lo = (std::min)(i, parent), hi = (std::max)(i, parent);
		if (hi < m_Top || lo >= m_Top + page)
			continue;
		int lx = x(m_Rows[i].Life->Start);
		if (lx < left || lx > left + width)
			continue;
		auto center = [&](int row) { return rc.top + m_AxisHeight + (row - m_Top) * m_RowHeight + m_RowHeight / 2; };
		int y1 = (std::max)(center(parent), rc.top + m_AxisHeight), y2 = (std::max)(center(i), rc.top + m_AxisHeight);
		dc.MoveTo(lx, y1);
		dc.LineTo(lx, y2);
		dc.LineTo(lx + pad, y2);
	}
	dc.SelectPen(oldPen);
	// the recording's start and now
	if (int sx = x(m_Since); sx >= left && sx <= left + width)
		dc.FillSolidRect(sx, rc.top + m_AxisHeight - pad, (std::max)(1, DpiHelper::Scale(2, m_hWnd)), rc.Height(), colors.Since);
	if (int nx = x(m_Now); nx >= left && nx <= left + width)
		dc.FillSolidRect(nx, rc.top, (std::max)(1, DpiHelper::Scale(2, m_hWnd)), rc.Height(), colors.Now);
	dc.SelectFont(oldFont);
	return 0;
}

LRESULT CTimelineCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateScrollBar();
	Invalidate();
	return 0;
}

LRESULT CTimelineCtrl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	SCROLLINFO si{ sizeof(si), SIF_ALL };
	GetScrollInfo(SB_VERT, &si);
	int top = m_Top;
	switch (LOWORD(wParam)) {
		case SB_LINEUP: top--; break;
		case SB_LINEDOWN: top++; break;
		case SB_PAGEUP: top -= si.nPage; break;
		case SB_PAGEDOWN: top += si.nPage; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: top = si.nTrackPos; break;
		case SB_TOP: top = 0; break;
		case SB_BOTTOM: top = (int)m_Rows.size(); break;
	}
	ScrollTo(top);
	return 0;
}

LRESULT CTimelineCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	auto keys = GET_KEYSTATE_WPARAM(wParam);
	auto [from, to] = m_View.GetRange();
	if (keys & MK_CONTROL) {
		// around the time under the mouse
		CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		ScreenToClient(&pt);
		auto anchor = TimelineModel::TimeAt(std::clamp((int)pt.x, ChartLeft(), ChartLeft() + ChartWidth()), from, to, ChartLeft(), ChartWidth());
		auto [f, t] = TimelineModel::Zoom(from, to, anchor, delta > 0 ? 0.8 : 1.25, MinSpan);
		m_View.SetRange(f, t);
	}
	else if (keys & MK_SHIFT) {
		auto shift = (int64_t)((to - from) * 0.1) * (delta > 0 ? -1 : 1);
		m_View.SetRange(from + shift, to + shift);
	}
	else
		ScrollTo(m_Top - delta / WHEEL_DELTA * 3);
	return 0;
}

LRESULT CTimelineCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (int row = RowAt(pt.y); row >= 0)
		Select(row);
	// a drag in the chart pans
	if (pt.x >= ChartLeft()) {
		m_Dragging = true;
		m_DragX = pt.x;
		std::tie(m_DragFrom, m_DragTo) = m_View.GetRange();
		SetCapture();
	}
	return 0;
}

LRESULT CTimelineCtrl::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Dragging) {
		m_Dragging = false;
		::ReleaseCapture();
	}
	return 0;
}

LRESULT CTimelineCtrl::OnLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (int row = RowAt(GET_Y_LPARAM(lParam)); row >= 0)
		m_View.GoToProcess(*m_Rows[row].Life);
	return 0;
}

LRESULT CTimelineCtrl::OnRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	SetFocus();
	if (int row = RowAt(GET_Y_LPARAM(lParam)); row >= 0)
		Select(row);
	bHandled = FALSE;
	return 0;
}

LRESULT CTimelineCtrl::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the selected row
		pt = { ChartLeft(), m_AxisHeight + (m_Selected - m_Top + 1) * m_RowHeight };
		ClientToScreen(&pt);
	}
	if (m_SelectedLife)
		m_View.ShowRowMenu(pt);
	return 0;
}

LRESULT CTimelineCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (m_Dragging) {
		auto span = m_DragTo - m_DragFrom;
		auto shift = (int64_t)((double)(m_DragX - pt.x) * span / ChartWidth());
		m_View.SetRange(m_DragFrom + shift, m_DragTo + shift);
		return 0;
	}
	if (int row = RowAt(pt.y); row != m_TipRow) {
		m_TipRow = row;
		// shown again, with the new row's text, after the usual delay
		m_Tip.Pop();
	}
	return 0;
}

LRESULT CTimelineCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto [from, to] = m_View.GetRange();
	CRect rc;
	GetClientRect(&rc);
	int page = (std::max)(1, ((int)rc.Height() - m_AxisHeight) / m_RowHeight);
	switch (wParam) {
		case VK_UP: Select((std::max)(0, m_Selected - 1)); break;
		case VK_DOWN: Select((std::min)((int)m_Rows.size() - 1, m_Selected + 1)); break;
		case VK_PRIOR: Select((std::max)(0, m_Selected - page)); break;
		case VK_NEXT: Select((std::min)((int)m_Rows.size() - 1, m_Selected + page)); break;
		case VK_LEFT: case VK_RIGHT:
		{
			auto shift = (int64_t)((to - from) * 0.1) * (wParam == VK_LEFT ? -1 : 1);
			m_View.SetRange(from + shift, to + shift);
			break;
		}
		case VK_ADD: case VK_OEM_PLUS: case VK_SUBTRACT: case VK_OEM_MINUS:
		{
			bool in = wParam == VK_ADD || wParam == VK_OEM_PLUS;
			auto [f, t] = TimelineModel::Zoom(from, to, from + (to - from) / 2, in ? 0.8 : 1.25, MinSpan);
			m_View.SetRange(f, t);
			break;
		}
		case VK_HOME: m_View.Fit(); break;
		case VK_RETURN:
			if (m_SelectedLife)
				m_View.GoToProcess(*m_SelectedLife);
			break;
	}
	return 0;
}

LRESULT CTimelineCtrl::OnGetTipText(int, LPNMHDR hdr, BOOL&) {
	auto di = reinterpret_cast<NMTTDISPINFO*>(hdr);
	CPoint pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	m_TipRow = RowAt(pt.y);
	m_TipText = m_TipRow >= 0 ? m_View.Describe(*m_Rows[m_TipRow].Life) : L"";
	di->lpszText = m_TipText.data();
	return 0;
}

//
// CTimelineView
//

LRESULT CTimelineView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_hWndClient = m_Chart.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS);

	m_Filter.Create(m_hWndToolBar, IDC_VIEW_FILTER, 280, L"Name, PID, command line or user (Ctrl+F)", m_Chart, [this](auto&) {
		Rebuild();
		});
	AddSimpleReBarBand(m_Filter.GetHwnd(), nullptr, FALSE, m_Filter.GetWidth());
	m_Bar = ToolbarHelper::CreateAndInitToolBar(m_hWndToolBar, s_Buttons, _countof(s_Buttons), ButtonImageSize);
	m_BarFont.Apply(m_Bar);
	AddSimpleReBarBand(m_Bar, nullptr, FALSE);
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE };
		rebar.GetBandInfo(i, &band);
		band.fStyle |= RBBS_NOGRIPPER;
		band.fMask = RBBIM_STYLE;
		if (band.hwndChild == m_Filter.GetHwnd()) {
			band.fMask |= RBBIM_CHILDSIZE;
			band.cxMinChild = m_Filter.GetWidth();
		}
		else if (band.hwndChild == m_Bar) {
			CRect last;
			m_Bar.GetItemRect(m_Bar.GetButtonCount() - 1, &last);
			band.fMask |= RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
			band.cxMinChild = band.cxIdeal = last.right;
			band.cyMinChild = last.Height();
		}
		rebar.SetBandInfo(i, &band);
	}
	m_HideThroughout = AppSettings::Get().TimelineHideThroughout() != 0;
	m_Bar.CheckButton(ID_TIMELINE_HIDETHROUGHOUT, m_HideThroughout);

	// the frame's with Record from Startup (what happened since the app started), else one of the tab's own
	m_Recorder = Recorders::AcquireHistory(Frame()->GetProcessManager());
	if (WinSys::SecurityHelper::IsRunningElevated())
		m_Creations = Recorders::AcquireProcessCreation();
	Fit();
	return 0;
}

void CTimelineView::LayoutBar() {
	if (!m_Bar)
		return;
	ToolbarHelper::UpdateToolBarImages(m_Bar, s_Buttons, _countof(s_Buttons), ButtonImageSize);
	m_BarFont.Apply(m_Bar);
	CReBarCtrl rebar(m_hWndToolBar);
	for (UINT i = 0; i < rebar.GetBandCount(); i++) {
		REBARBANDINFO band{ sizeof(band), RBBIM_CHILD };
		if (rebar.GetBandInfo(i, &band) && band.hwndChild == m_Bar) {
			CRect last;
			m_Bar.GetItemRect(m_Bar.GetButtonCount() - 1, &last);
			band.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
			band.cxMinChild = band.cxIdeal = last.right;
			band.cyMinChild = last.Height();
			rebar.SetBandInfo(i, &band);
			break;
		}
	}
	UpdateLayout();
}

void CTimelineView::UpdateLayout(BOOL resizeBars) {
	CViewBase::UpdateLayout(resizeBars);
	if (m_Bar) {
		CReBarCtrl rebar(m_hWndToolBar);
		rebar.MaximizeBand(rebar.GetBandCount() - 1, FALSE);
	}
}

void CTimelineView::OnActivate(bool active) {
	m_Active = active;
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_TIMELINE_HIDETHROUGHOUT, active);
	ui.UISetCheck(ID_TIMELINE_HIDETHROUGHOUT, m_HideThroughout);
	ui.UIEnable(ID_TIMELINE_FIT, active);
	if (active) {
		Rebuild();
		m_Chart.SetFocus();
	}
	else
		Frame()->SetStatusText(0, L"");
}

void CTimelineView::OnRefresh(bool active) {
	// the recorder takes the changes on its own (Recorders::OnRefresh): only what's seen is redrawn
	if (active)
		Rebuild();
}

void CTimelineView::OnFontChanged(HFONT font) {
	m_Chart.SetFont(font);
	m_Filter.SetFont(font);
	// the bar's buttons and band (and the filter's) take the font's size
	m_BarFont.SetUserFont(font);
	LayoutBar();
}

void CTimelineView::Fit() {
	auto now = HistoryRecorder::Now();
	auto since = m_Recorder ? m_Recorder->GetSince() : now;
	// a little before the recording, so its first processes' starts show; at least a minute
	auto span = (std::max)(now - since, 60 * TimelineModel::Second);
	m_From = now - span - span / 50;
	m_To = now;
	m_Follow = true;
	Rebuild();
}

void CTimelineView::SetRange(int64_t from, int64_t to) {
	auto now = HistoryRecorder::Now();
	// past now: back to it, and following again
	if (to >= now) {
		from -= to - now;
		to = now;
		m_Follow = true;
	}
	else
		m_Follow = false;
	m_From = from;
	m_To = to;
	Rebuild();
}

void CTimelineView::Rebuild() {
	if (!m_Recorder)
		return;
	auto now = HistoryRecorder::Now();
	if (m_Follow) {
		auto span = m_To - m_From;
		m_To = now;
		m_From = now - span;
	}
	TimelineModel::Options options{ m_From, m_To, now, m_Filter.GetText(), m_HideThroughout };
	auto rows = TimelineModel::Build(m_Recorder->GetLives(), options, &m_Hidden);
	m_Shown = rows.size();
	m_Chart.SetRows(std::move(rows), m_From, m_To, now, m_Recorder->GetSince());
	UpdateStatus();
}

void CTimelineView::UpdateStatus() {
	if (!m_Active)
		return;
	FILETIME local;
	SYSTEMTIME st{};
	auto since = m_Recorder ? m_Recorder->GetSince() : 0;
	::FileTimeToLocalFileTime((FILETIME const*)&since, &local);
	::FileTimeToSystemTime(&local, &st);
	auto text = std::format(L"{} processes", m_Shown);
	if (m_Hidden)
		text += std::format(L" ({} running throughout hidden)", m_Hidden);
	text += std::format(L"; recording since {:02}:{:02}:{:02}; Ctrl+wheel zooms, Shift+wheel or dragging pans, Home fits",
		st.wHour, st.wMinute, st.wSecond);
	if (!m_Follow)
		text += L" (not following now)";
	Frame()->SetStatusText(0, text.c_str());
}

std::wstring CTimelineView::Describe(HistoryRecorder::ProcessLife const& life) const {
	std::wstring parentName;
	if (m_Recorder && life.Parent.Created)
		if (auto it = m_Recorder->GetLives().find(life.Parent); it != m_Recorder->GetLives().end())
			parentName = it->second.Name;
	auto text = TimelineModel::Describe(life, parentName, HistoryRecorder::Now());
	// who created it, when it wasn't its parent
	if (m_Creations)
		if (auto c = m_Creations->Get(life.Key); c && c->Creator != WinSys::ProcessCreator::Parent)
			text += L"\n" + WinSys::ProcessCreationParser::Describe(*c);
	return text;
}

void CTimelineView::GoToProcess(HistoryRecorder::ProcessLife const& life) {
	if (life.End) {
		Frame()->SetStatusText(0, std::format(L"{} ({}) isn't running anymore", life.Name, life.Key.Id).c_str());
		return;
	}
	Frame()->GoToProcess(life.Key.Id);
}

void CTimelineView::ShowRowMenu(CPoint pt) {
	auto life = m_Chart.GetSelected();
	if (!life)
		return;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (life->End ? MF_GRAYED : 0), ID_TIMELINE_GOTOPROCESS, L"&Go to Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	Frame()->ShowContextMenu(menu, pt, TPM_RIGHTBUTTON);
}

LRESULT CTimelineView::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Changed();
	return 0;
}

LRESULT CTimelineView::OnHideThroughout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_HideThroughout = !m_HideThroughout;
	m_Bar.CheckButton(ID_TIMELINE_HIDETHROUGHOUT, m_HideThroughout);
	Frame()->GetUI().UISetCheck(ID_TIMELINE_HIDETHROUGHOUT, m_HideThroughout);
	auto& settings = AppSettings::Get();
	settings.TimelineHideThroughout(m_HideThroughout);
	settings.Save();
	Rebuild();
	return 0;
}

LRESULT CTimelineView::OnFit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Fit();
	return 0;
}

LRESULT CTimelineView::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto life = m_Chart.GetSelected()) {
		auto text = Describe(*life);
		// lines as a text editor has them
		std::wstring crlf;
		for (auto ch : text) {
			if (ch == L'\n')
				crlf += L'\r';
			crlf += ch;
		}
		ClipboardHelper::CopyText(m_hWnd, crlf.c_str());
	}
	return 0;
}

LRESULT CTimelineView::OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Filter.Focus();
	return 0;
}

LRESULT CTimelineView::OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto life = m_Chart.GetSelected())
		GoToProcess(*life);
	return 0;
}
