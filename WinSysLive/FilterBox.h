#pragma once

#include <functional>

//
// A view's filter box: an edit in the view's rebar whose text filters its list. The text is applied after a pause in
// typing (a timer on the edit), passed lowercase to the view's callback. Escape clears it (or, empty, goes back to the
// list); Enter and Down apply it at once and go to the list. The edit's EN_CHANGE reaches the view (through the rebar),
// which hands it to Changed.
//
class CFilterBox {
public:
	using Callback = std::function<void(std::wstring const& text)>;

	// hRebar: the view's (CreateSimpleReBar); id: the edit's, for its EN_CHANGE; list: where Enter and Escape go
	HWND Create(HWND hRebar, UINT id, int width, PCWSTR cue, HWND hList, Callback applied);
	HWND GetHwnd() const;
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

	enum { TimerId = 0x4654, Delay = 250 };
	CEdit m_Edit;
	HWND m_hList{ nullptr };
	Callback m_Applied;
	std::wstring m_Text;
};
