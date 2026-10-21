#pragma once

#include <functional>

//
// A view's filter box: an edit in the view's rebar whose text filters its list. The text is applied after a pause in
// typing (a timer on the edit), passed lowercase to the view's callback. Escape clears it (or, empty, goes back to the
// list); Enter and Down apply it at once and go to the list. The edit's EN_CHANGE reaches the view (through the rebar),
// which hands it to Changed. It follows its DPI (WM_DPICHANGED_AFTERPARENT): its size, its band in a rebar (or its
// parent's layout, which gets a WM_SIZE), and its font unless the view gave it one (which it sets again for the new DPI).
//
class CFilterBox {
public:
	using Callback = std::function<void(std::wstring const& text)>;

	// hRebar: the view's (CreateSimpleReBar), or another parent that lays it out; id: the edit's, for its EN_CHANGE;
	// width: at 96 DPI; list: where Enter and Escape go
	HWND Create(HWND hRebar, UINT id, int width, PCWSTR cue, HWND hList, Callback applied);
	HWND GetHwnd() const;
	// the box's width now, in pixels (for its rebar band)
	int GetWidth() const;
	// nullptr: a font of its own, the default for its DPI
	void SetFont(HFONT font);

	// the edit's EN_CHANGE: applied after the pause
	void Changed();
	// Ctrl+F
	void Focus();
	// empties the box and applies that now
	void Clear();
	// the text applied, lowercase; empty: no filter
	std::wstring const& GetText() const;

	// whether the text contains the filter, ignoring case (filter: lowercase)
	static bool Contains(std::wstring text, std::wstring const& filter);

private:
	static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);
	void Apply();
	void OnDpiChanged();
	int GetHeight() const;

	enum { TimerId = 0x4654, Delay = 250 };
	CEdit m_Edit;
	int m_Width{ 0 };	// at 96 DPI
	HFONT m_ViewFont{ nullptr };	// the view's (Options > Font), if it gave one
	CFont m_OwnFont;	// otherwise
	HWND m_hList{ nullptr };
	Callback m_Applied;
	std::wstring m_Text;
};
