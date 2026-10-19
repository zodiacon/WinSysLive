#include "pch.h"
#include <atlgdi.h>
#include "PerfNav.h"

namespace {
	// samples shown by an item's graph
	const size_t ItemHistory = 60;

	COLORREF Blend(COLORREF c1, COLORREF c2, int percent1) {
		auto mix = [=](int a, int b) { return (a * percent1 + b * (100 - percent1)) / 100; };
		return RGB(mix(GetRValue(c1), GetRValue(c2)), mix(GetGValue(c1), GetGValue(c2)), mix(GetBValue(c1), GetBValue(c2)));
	}
}

int CPerfNav::AddItem(std::wstring title, COLORREF color, GraphType graph) {
	m_Items.push_back({ std::move(title), {}, color, graph });
	if (m_hWnd)
		Invalidate();
	return (int)m_Items.size() - 1;
}

void CPerfNav::InsertItem(int index, std::wstring title, COLORREF color, GraphType graph) {
	m_Items.insert(m_Items.begin() + index, { std::move(title), {}, color, graph });
	if (m_Selected >= index)
		m_Selected++;
	if (m_hWnd)
		Invalidate();
}

void CPerfNav::RemoveItem(int index) {
	m_Items.erase(m_Items.begin() + index);
	if (m_Selected == index)
		m_Selected = -1;
	else if (m_Selected > index)
		m_Selected--;
	if (m_hWnd)
		Invalidate();
}

void CPerfNav::SetItemValue(int index, std::wstring value) {
	auto& item = m_Items[index];
	if (item.Value == value)
		return;
	item.Value = std::move(value);
	if (m_hWnd)
		InvalidateRect(GetItemRect(index), FALSE);
}

void CPerfNav::PushItemSample(int index, float value) {
	auto& item = m_Items[index];
	item.History.push_back(item.Graph == GraphType::Percent ? std::clamp(value, 0.0f, 100.0f) : std::max(value, 0.0f));
	if (item.History.size() > ItemHistory)
		item.History.pop_front();
	if (m_hWnd && IsWindowVisible())
		InvalidateRect(GetItemRect(index), FALSE);
}

int CPerfNav::GetItemCount() const {
	return (int)m_Items.size();
}

int CPerfNav::GetSelectedIndex() const {
	return m_Selected;
}

void CPerfNav::SelectItem(int index) {
	if (index < 0 || index >= (int)m_Items.size() || index == m_Selected)
		return;
	if (m_hWnd && m_Selected >= 0)
		InvalidateRect(GetItemRect(m_Selected), FALSE);
	m_Selected = index;
	if (m_hWnd)
		InvalidateRect(GetItemRect(index), FALSE);
	if (OnSelectionChanged)
		OnSelectionChanged(index);
}

void CPerfNav::SetTheme(GraphCtrl::GraphTheme const& theme) {
	m_Theme = theme;
	if (m_hWnd)
		Invalidate();
}

int CPerfNav::Scale(int value) const {
	return ::MulDiv(value, ::GetDpiForWindow(m_hWnd), 96);
}

int CPerfNav::GetIdealWidth() const {
	return Scale(210);
}

int CPerfNav::ItemHeight() const {
	return std::max(m_TitleHeight + m_ValueHeight, Scale(40)) + 2 * Scale(8);
}

CRect CPerfNav::GetItemRect(int index) const {
	CRect rc;
	GetClientRect(&rc);
	int height = ItemHeight();
	rc.top = Scale(4) + index * height;
	rc.bottom = rc.top + height;
	return rc;
}

LRESULT CPerfNav::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	NONCLIENTMETRICS ncm = { sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(m_hWnd));
	m_ValueFont.CreateFontIndirect(&ncm.lfMessageFont);
	auto lf = ncm.lfMessageFont;
	lf.lfHeight = lf.lfHeight * 4 / 3;
	lf.lfWeight = FW_SEMIBOLD;
	m_TitleFont.CreateFontIndirect(&lf);

	CClientDC dc(m_hWnd);
	TEXTMETRIC tm;
	auto old = dc.SelectFont(m_TitleFont);
	dc.GetTextMetrics(&tm);
	m_TitleHeight = tm.tmHeight;
	dc.SelectFont(m_ValueFont);
	dc.GetTextMetrics(&tm);
	m_ValueHeight = tm.tmHeight + tm.tmExternalLeading;
	dc.SelectFont(old);
	return 0;
}

LRESULT CPerfNav::OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return 1;	// OnPaint fills the background
}

LRESULT CPerfNav::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CPaintDC paintDC(m_hWnd);
	CRect client;
	GetClientRect(&client);
	CMemoryDC dc(paintDC, client);
	dc.FillSolidRect(&client, m_Theme.Background);
	for (int i = 0; i < (int)m_Items.size(); i++) {
		auto rc = GetItemRect(i);
		CRect intersect;
		if (intersect.IntersectRect(&rc, &paintDC.m_ps.rcPaint))
			DrawItem(dc.m_hDC, i, rc);
	}
	// separator from the page
	dc.FillSolidRect(client.right - Scale(1), client.top, Scale(1), client.Height(), m_Theme.GridColor);
	return 0;
}

void CPerfNav::DrawItem(CDCHandle dc, int index, CRect const& rc) {
	auto& item = m_Items[index];
	int pad = Scale(8);
	if (index == m_Selected) {
		CRect sel(rc);
		sel.DeflateRect(Scale(4), 0, Scale(6), 0);
		dc.FillSolidRect(&sel, m_Theme.PanelColor);
		dc.FillSolidRect(sel.left, sel.top, Scale(3), sel.Height(), item.Color);
		if (GetFocus() == m_hWnd) {
			dc.SetTextColor(m_Theme.TextColor);
			dc.SetBkColor(m_Theme.PanelColor);
			dc.DrawFocusRect(&sel);
		}
	}

	CRect text(rc);
	text.DeflateRect(pad + Scale(4), pad, pad, pad);
	if (item.Graph != GraphType::None) {
		CRect graph(text.left, text.top, text.left + Scale(64), text.bottom);
		DrawGraph(dc, item, graph);
		text.left = graph.right + pad;
	}

	dc.SetBkMode(TRANSPARENT);
	int top = text.top + (text.Height() - m_TitleHeight - m_ValueHeight) / 2;
	CRect line(text.left, top, text.right, top + m_TitleHeight);
	dc.SetTextColor(m_Theme.ValueColor);
	auto old = dc.SelectFont(m_TitleFont);
	dc.DrawText(item.Title.c_str(), (int)item.Title.size(), &line, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	line.SetRect(text.left, line.bottom, text.right, line.bottom + m_ValueHeight);
	dc.SetTextColor(m_Theme.TextColor);
	dc.SelectFont(m_ValueFont);
	dc.DrawText(item.Value.c_str(), (int)item.Value.size(), &line, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	dc.SelectFont(old);
}

void CPerfNav::DrawGraph(CDCHandle dc, Item const& item, CRect const& rc) {
	dc.FillSolidRect(&rc, m_Theme.PlotColor);
	auto& history = item.History;
	if (history.size() >= 2) {
		// an auto-scaled graph peaks at 90% of the height, so its top stays visible
		float range = item.Graph == GraphType::Percent ? 100.0f : *std::max_element(history.begin(), history.end()) / 0.9f;
		if (range <= 0)
			range = 1;
		// right-aligned, one step per sample, so the graph scrolls in from the right as history builds up
		double step = (double)(rc.Width() - 1) / (ItemHistory - 1);
		std::vector<POINT> points;
		points.reserve(history.size() + 2);
		for (size_t i = 0; i < history.size(); i++) {
			int x = rc.right - 1 - (int)std::lround((history.size() - 1 - i) * step);
			int y = rc.bottom - 1 - (int)std::lround(history[i] * (rc.Height() - 1) / range);
			points.push_back({ x, y });
		}
		auto line = points;
		points.push_back({ points.back().x, rc.bottom });
		points.push_back({ points.front().x, rc.bottom });

		auto fill = Blend(item.Color, m_Theme.PlotColor, 35);
		CBrush brush;
		brush.CreateSolidBrush(fill);
		CPen fillPen;
		fillPen.CreatePen(PS_SOLID, 1, fill);
		auto oldBrush = dc.SelectBrush(brush);
		auto oldPen = dc.SelectPen(fillPen);
		dc.Polygon(points.data(), (int)points.size());

		CPen pen;
		pen.CreatePen(PS_SOLID, Scale(1), item.Color);
		dc.SelectPen(pen);
		dc.Polyline(line.data(), (int)line.size());
		dc.SelectPen(oldPen);
		dc.SelectBrush(oldBrush);
	}

	CPen border;
	border.CreatePen(PS_SOLID, 1, item.Color);
	auto oldPen = dc.SelectPen(border);
	auto oldBrush = dc.SelectStockBrush(NULL_BRUSH);
	dc.Rectangle(&rc);
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

LRESULT CPerfNav::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	SetFocus();
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	for (int i = 0; i < (int)m_Items.size(); i++) {
		if (GetItemRect(i).PtInRect(pt)) {
			SelectItem(i);
			break;
		}
	}
	return 0;
}

LRESULT CPerfNav::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	int count = (int)m_Items.size();
	switch (wParam) {
		case VK_UP: SelectItem(std::max(0, m_Selected - 1)); break;
		case VK_DOWN: SelectItem(std::min(count - 1, m_Selected + 1)); break;
		case VK_HOME: SelectItem(0); break;
		case VK_END: SelectItem(count - 1); break;
		default: bHandled = FALSE; break;
	}
	return 0;
}

LRESULT CPerfNav::OnGetDlgCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	return DLGC_WANTARROWS;
}

LRESULT CPerfNav::OnFocusChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_Selected >= 0)
		InvalidateRect(GetItemRect(m_Selected), FALSE);
	return 0;
}
