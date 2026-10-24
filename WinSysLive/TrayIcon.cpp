#include "pch.h"
#include "TrayIcon.h"

namespace {
	int IconSize() {
		return ::GetSystemMetrics(SM_CXSMICON);
	}
}

CTrayIcon::~CTrayIcon() {
	Remove();
}

bool CTrayIcon::Add(HWND hWnd, UINT callbackMessage, UINT id) {
	m_hWnd = hWnd;
	m_Message = callbackMessage;
	m_Id = id;
	if (!m_hIcon)
		m_hIcon = CreateIcon();
	NOTIFYICONDATA nid{ sizeof(nid) };
	nid.hWnd = hWnd;
	nid.uID = id;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = callbackMessage;
	nid.hIcon = m_hIcon;
	wcsncpy_s(nid.szTip, m_Tip.c_str(), _TRUNCATE);
	m_Shown = ::Shell_NotifyIcon(NIM_ADD, &nid);
	return m_Shown;
}

void CTrayIcon::Remove() {
	if (m_Shown) {
		NOTIFYICONDATA nid{ sizeof(nid) };
		nid.hWnd = m_hWnd;
		nid.uID = m_Id;
		::Shell_NotifyIcon(NIM_DELETE, &nid);
		m_Shown = false;
	}
	if (m_hIcon) {
		::DestroyIcon(m_hIcon);
		m_hIcon = nullptr;
	}
}

bool CTrayIcon::IsShown() const {
	return m_Shown;
}

void CTrayIcon::SetColor(COLORREF color) {
	m_Color = color;
}

void CTrayIcon::SetScale(double max, double floor) {
	m_Max = max;
	m_Floor = floor;
}

void CTrayIcon::AddSample(double value) {
	m_Samples.push_back((std::max)(value, 0.0));
	while ((int)m_Samples.size() > IconSize())
		m_Samples.pop_front();
}

void CTrayIcon::SetTip(std::wstring const& tip) {
	m_Tip = tip;
}

void CTrayIcon::Update() {
	if (!m_Shown)
		return;
	auto hIcon = CreateIcon();
	NOTIFYICONDATA nid{ sizeof(nid) };
	nid.hWnd = m_hWnd;
	nid.uID = m_Id;
	nid.uFlags = NIF_ICON | NIF_TIP;
	nid.hIcon = hIcon;
	wcsncpy_s(nid.szTip, m_Tip.c_str(), _TRUNCATE);
	::Shell_NotifyIcon(NIM_MODIFY, &nid);
	// the shell has its own copy now
	if (m_hIcon)
		::DestroyIcon(m_hIcon);
	m_hIcon = hIcon;
}

void CTrayIcon::DrawGraph(CDCHandle dc, CRect const& rc) const {
	double top = m_Max;
	if (top <= 0) {
		top = m_Floor;
		for (auto v : m_Samples)
			top = (std::max)(top, v);
		if (top <= 0)
			return;
	}
	// the samples as columns, the newest at the right edge
	int height = rc.Height();
	int x = rc.right - 1;
	for (auto it = m_Samples.rbegin(); it != m_Samples.rend() && x >= rc.left; ++it, --x) {
		int bar = (int)((std::min)(*it, top) * height / top + 0.5);
		if (*it > 0 && bar == 0)
			bar = 1;	// any value shows
		if (bar > 0)
			dc.FillSolidRect(x, rc.bottom - bar, 1, bar, m_Color);
	}
}

HICON CTrayIcon::CreateIcon() const {
	int size = IconSize();
	CClientDC screen(nullptr);
	CDC dc;
	dc.CreateCompatibleDC(screen);
	CBitmap color;
	color.CreateCompatibleBitmap(screen, size, size);
	auto old = dc.SelectBitmap(color);

	// a dark frame and background, so the graph shows on light and dark taskbars alike
	CRect rc(0, 0, size, size);
	dc.FillSolidRect(&rc, RGB(64, 64, 64));
	rc.DeflateRect(1, 1);
	dc.FillSolidRect(&rc, RGB(0, 0, 0));
	DrawGraph(dc.m_hDC, rc);
	dc.SelectBitmap(old);

	// all opaque: the mask is all zeros (the color bitmap has no alpha, so the mask is used)
	CBitmap mask;
	mask.CreateBitmap(size, size, 1, 1, nullptr);
	{
		CDC maskDC;
		maskDC.CreateCompatibleDC(screen);
		auto oldMask = maskDC.SelectBitmap(mask);
		maskDC.PatBlt(0, 0, size, size, BLACKNESS);
		maskDC.SelectBitmap(oldMask);
	}
	ICONINFO ii{ TRUE, 0, 0, mask, color };
	return ::CreateIconIndirect(&ii);
}
