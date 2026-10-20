#include "pch.h"
#include "TrayIcon.h"

namespace {
	const UINT IconId = 1;

	int IconSize() {
		return ::GetSystemMetrics(SM_CXSMICON);
	}
}

CTrayIcon::~CTrayIcon() {
	Remove();
}

bool CTrayIcon::Add(HWND hWnd, UINT callbackMessage) {
	m_hWnd = hWnd;
	m_Message = callbackMessage;
	if (!m_hIcon)
		m_hIcon = CreateIcon();
	NOTIFYICONDATA nid{ sizeof(nid) };
	nid.hWnd = hWnd;
	nid.uID = IconId;
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
		nid.uID = IconId;
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

void CTrayIcon::AddSample(float cpu) {
	m_Samples.push_back(std::clamp(cpu, 0.0f, 100.0f));
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
	nid.uID = IconId;
	nid.uFlags = NIF_ICON | NIF_TIP;
	nid.hIcon = hIcon;
	wcsncpy_s(nid.szTip, m_Tip.c_str(), _TRUNCATE);
	::Shell_NotifyIcon(NIM_MODIFY, &nid);
	// the shell has its own copy now
	if (m_hIcon)
		::DestroyIcon(m_hIcon);
	m_hIcon = hIcon;
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
	// the samples as columns, the newest at the right edge
	int height = rc.Height();
	int x = rc.right - 1;
	for (auto it = m_Samples.rbegin(); it != m_Samples.rend() && x >= rc.left; ++it, --x) {
		int bar = (int)(*it * height / 100.0f + 0.5f);
		if (*it > 0 && bar == 0)
			bar = 1;	// any usage shows
		if (bar > 0)
			dc.FillSolidRect(x, rc.bottom - bar, 1, bar, RGB(0, 224, 0));
	}
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
