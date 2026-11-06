#pragma once

#include <DpiHelper.h>

//
// The font of a view's tool bar with text (beside its filter box): the user's (Options > Font, which the view gets with
// IView::OnFontChanged), else one of its own for the bar's DPI, made again by Apply after a DPI change. After Apply the
// view sizes the bar's band (and lays itself out), as the buttons' size follows the font.
//
class CBarFont {
public:
	// the frame's font, which it keeps until the next call
	void SetUserFont(HFONT font) {
		m_User = font;
	}

	// gives the bar its font and sizes the buttons
	void Apply(HWND hBar) {
		if (!hBar)
			return;
		// the bar's own font, deleted once the bar has the new one
		CFont old(m_Own.Detach());
		HFONT font = m_User;
		if (!font) {
			m_Own.Attach(DpiHelper::CreateControlFont(DpiHelper::GetDpi(hBar)));
			font = m_Own;
		}
		CToolBarCtrl bar(hBar);
		bar.SetFont(font);
		Remeasure(bar);
	}

	// how much wider the text is in the bar's font than in the default one for its DPI (negative: narrower), for a
	// button whose width is set by the view from its width in the default font
	static int ExtraWidth(HWND hBar, PCWSTR text) {
		CClientDC dc(hBar);
		CFont standard(DpiHelper::CreateControlFont(DpiHelper::GetDpi(hBar)));
		CSize inBar, inStandard;
		auto old = dc.SelectFont(CWindow(hBar).GetFont());
		dc.GetTextExtent(text, -1, &inBar);
		dc.SelectFont(standard);
		dc.GetTextExtent(text, -1, &inStandard);
		dc.SelectFont(old);
		return inBar.cx - inStandard.cx;
	}

private:
	// the tool bar keeps the buttons' sizes it measured with its old font (BTNS_AUTOSIZE widths, the buttons' height):
	// they're measured again from the text in the new one (BarFontTests)
	static void Remeasure(CToolBarCtrl& bar) {
		int count = bar.GetButtonCount();
		for (int i = 0; i < count; i++) {
			WCHAR text[128]{};
			TBBUTTONINFO info{ sizeof(info), TBIF_BYINDEX | TBIF_STYLE | TBIF_TEXT };
			info.pszText = text;
			info.cchText = _countof(text);
			if (bar.GetButtonInfo(i, &info) < 0 || (info.fsStyle & BTNS_SEP))
				continue;
			// the text again: the bar measures it in the new font
			info.dwMask = TBIF_BYINDEX | TBIF_TEXT;
			bar.SetButtonInfo(i, &info);
			if (info.fsStyle & BTNS_AUTOSIZE) {
				// 0: as wide as its content
				info.dwMask = TBIF_BYINDEX | TBIF_SIZE;
				info.cx = 0;
				bar.SetButtonInfo(i, &info);
			}
		}
		// the height isn't measured again: the taller of the images and the text, and the padding
		int image = 0;
		if (auto images = bar.GetImageList(); !images.IsNull()) {
			int cx;
			images.GetIconSize(cx, image);
		}
		CClientDC dc(bar);
		auto old = dc.SelectFont(bar.GetFont());
		TEXTMETRIC tm{};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		SIZE padding{};
		bar.GetPadding(&padding);
		bar.SetButtonSize(0, (std::max)(image, (int)tm.tmHeight) + padding.cy);
		bar.AutoSize();
	}

	HFONT m_User{ nullptr };	// the frame's
	CFont m_Own;	// without the user's
};
