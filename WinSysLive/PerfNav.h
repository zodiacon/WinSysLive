#pragma once

#include <GraphControl/include/GraphRenderer.h>
#include <deque>
#include <functional>

//
// The list of pages on the left of the Performance tab, in the style of Task Manager: each entry shows its title,
// its current value and, for a resource, a small graph of the recent history: 0-100, or scaled to its own peak.
// It scrolls vertically when the items don't fit (many drives); the selected item is kept in view.
//
class CPerfNav : public CWindowImpl<CPerfNav> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfNav", CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

	enum class GraphType { None, Percent, AutoScale };

	int AddItem(std::wstring title, COLORREF color, GraphType graph);
	// the selection follows the item it was on; removing the selected item leaves nothing selected
	void InsertItem(int index, std::wstring title, COLORREF color, GraphType graph);
	void RemoveItem(int index);
	void SetItemValue(int index, std::wstring value);
	void PushItemSample(int index, float value);
	int GetItemCount() const;

	int GetSelectedIndex() const;
	void SelectItem(int index);
	void SetTheme(GraphCtrl::GraphTheme const& theme);
	int GetIdealWidth() const;

	std::function<void(int)> OnSelectionChanged;

	BEGIN_MSG_MAP(CPerfNav)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocusChanged)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocusChanged)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseWheel(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDlgCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFocusChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	struct Item {
		std::wstring Title, Value;
		COLORREF Color;
		GraphType Graph;
		std::deque<float> History;
	};

	int Scale(int value) const;
	int ItemHeight() const;
	CRect GetItemRect(int index) const;	// in client coordinates, so scrolled
	void UpdateScrollBar();
	void ScrollTo(int pos);
	void EnsureVisible(int index);
	void DrawItem(CDCHandle dc, int index, CRect const& rc);
	void DrawGraph(CDCHandle dc, Item const& item, CRect const& rc);

	std::vector<Item> m_Items;
	int m_Selected{ -1 };
	int m_ScrollPos{ 0 };	// pixels scrolled from the top
	int m_WheelDelta{ 0 };	// wheel movement not scrolled yet (high resolution wheels send less than WHEEL_DELTA)
	GraphCtrl::GraphTheme m_Theme;
	CFont m_TitleFont, m_ValueFont;
	int m_TitleHeight{ 0 }, m_ValueHeight{ 0 };
};
