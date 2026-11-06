#include "pch.h"
#include "TestFramework.h"
#include "..\WinSysLive\BarFont.h"

//
// CBarFont on a tool bar made as ToolbarHelper::CreateAndInitToolBar makes the views' (list style, mixed buttons,
// BTNS_AUTOSIZE check buttons with text): the buttons must fit their text in the font given, larger or smaller
//

namespace {
	const struct {
		UINT id;
		BYTE style;
		PCWSTR text;
	} s_Buttons[] = {
		{ 1001, BTNS_CHECK | BTNS_AUTOSIZE, L"By Type" },
		{ 1002, BTNS_CHECK | BTNS_AUTOSIZE, L"Unnamed Handles" },
	};

	struct BarWindow {
		BarWindow() {
			INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
			::InitCommonControlsEx(&icc);
			// off the screen, as wide as a view, shown (a hidden bar's buttons have no rectangles)
			CRect rc(-3000, -3000, -3000 + 1200, -3000 + 200);
			Parent.Create(L"STATIC", nullptr, &rc, nullptr, WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
			Parent.ShowWindow(SW_SHOWNOACTIVATE);
			CRect barRect(0, 0, 1200, 100);
			Bar.Create(Parent, &barRect, nullptr, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST, 0, ATL_IDW_TOOLBAR);
			Bar.SetButtonStructSize();
			Bar.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);
			for (auto& b : s_Buttons)
				Bar.AddButton(b.id, b.style | BTNS_SHOWTEXT, TBSTATE_ENABLED, I_IMAGENONE, b.text, 0);
		}
		~BarWindow() {
			Parent.DestroyWindow();
		}

		// the text's width in the bar's font
		int TextWidth(PCWSTR text) {
			CClientDC dc(Bar);
			auto old = dc.SelectFont(Bar.GetFont());
			CSize size;
			dc.GetTextExtent(text, -1, &size);
			dc.SelectFont(old);
			return size.cx;
		}

		// whether every button is wider than its text (and its height at least the font's)
		void CheckFits(const char* what) {
			CClientDC dc(Bar);
			auto old = dc.SelectFont(Bar.GetFont());
			TEXTMETRIC tm{};
			dc.GetTextMetrics(&tm);
			dc.SelectFont(old);
			for (int i = 0; i < _countof(s_Buttons); i++) {
				CRect rc;
				Bar.GetItemRect(i, &rc);
				int text = TextWidth(s_Buttons[i].text);
				if (rc.Width() <= text)
					Tests::Fail(__FILE__, __LINE__, std::format("{}: button {} is {} wide, its text {}", what, i, rc.Width(), text));
				if (rc.Height() < tm.tmHeight)
					Tests::Fail(__FILE__, __LINE__, std::format("{}: button {} is {} high, the font {}", what, i, rc.Height(), tm.tmHeight));
			}
		}

		CWindow Parent;
		CToolBarCtrl Bar;
	};

	CFont MakeFont(int height) {
		LOGFONT lf{};
		lf.lfHeight = -height;
		wcscpy_s(lf.lfFaceName, L"Segoe UI");
		return CFont(::CreateFontIndirect(&lf));
	}
}

TEST(BarFont_ButtonsFitLargerFont) {
	BarWindow w;
	CBarFont font;
	font.Apply(w.Bar);
	w.CheckFits("default");
	CRect before;
	w.Bar.GetItemRect(1, &before);

	auto large = MakeFont(32);
	font.SetUserFont(large);
	font.Apply(w.Bar);
	w.CheckFits("large");
	CRect after;
	w.Bar.GetItemRect(1, &after);
	CHECK(after.Width() > before.Width());
	CHECK(after.Height() > before.Height());
	font.SetUserFont(nullptr);
	font.Apply(w.Bar);
}

TEST(BarFont_ButtonsShrinkBack) {
	BarWindow w;
	CBarFont font;
	font.Apply(w.Bar);
	CRect standard;
	w.Bar.GetItemRect(1, &standard);

	auto large = MakeFont(32);
	font.SetUserFont(large);
	font.Apply(w.Bar);
	// back to the default: the buttons as they were
	font.SetUserFont(nullptr);
	font.Apply(w.Bar);
	w.CheckFits("default again");
	CRect again;
	w.Bar.GetItemRect(1, &again);
	CHECK_EQ(again.Width(), standard.Width());
	CHECK_EQ(again.Height(), standard.Height());
}
