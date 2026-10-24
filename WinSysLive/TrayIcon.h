#pragma once

#include <deque>

//
// A notification area icon: a graph of the recent values, one column per refresh (newest on the right), in one color on
// a dark square, with a tooltip. Explorer drops the icons when it restarts, so the owner calls Add again on the
// "TaskbarCreated" message.
//
class CTrayIcon {
public:
	~CTrayIcon();

	// hWnd gets callbackMessage with the icon's ID in wParam and the mouse message in lParam (the classic, version 0
	// callbacks)
	bool Add(HWND hWnd, UINT callbackMessage, UINT id);
	void Remove();
	bool IsShown() const;

	void SetColor(COLORREF color);
	// the graph's top: a fixed value (100 for percentages), or 0 for the largest value shown, at least floor
	void SetScale(double max, double floor = 0);
	void AddSample(double value);
	void SetTip(std::wstring const& tip);
	// redraws the icon, and sets the tooltip
	void Update();

private:
	HICON CreateIcon() const;
	void DrawGraph(CDCHandle dc, CRect const& rc) const;

	HWND m_hWnd{ nullptr };
	UINT m_Message{ 0 }, m_Id{ 0 };
	bool m_Shown{ false };
	HICON m_hIcon{ nullptr };
	COLORREF m_Color{ RGB(0, 224, 0) };
	double m_Max{ 100 }, m_Floor{ 0 };
	std::deque<double> m_Samples;	// the newest last; as many as the icon is wide
	std::wstring m_Tip;
};
