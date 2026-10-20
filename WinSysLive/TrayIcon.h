#pragma once

#include <deque>

//
// The notification area icon: a graph of the recent CPU usage, one column per refresh (newest on the right), with a
// tooltip. Explorer drops the icons when it restarts, so the owner calls Add again on the "TaskbarCreated" message.
//
class CTrayIcon {
public:
	~CTrayIcon();

	// hWnd gets callbackMessage with the mouse message in lParam (the classic, version 0 callbacks)
	bool Add(HWND hWnd, UINT callbackMessage);
	void Remove();
	bool IsShown() const;

	// cpu: percent, 0 to 100
	void AddSample(float cpu);
	void SetTip(std::wstring const& tip);
	// redraws the icon from the samples, and sets the tooltip
	void Update();

private:
	HICON CreateIcon() const;

	HWND m_hWnd{ nullptr };
	UINT m_Message{ 0 };
	bool m_Shown{ false };
	HICON m_hIcon{ nullptr };
	std::deque<float> m_Samples;	// the newest last; as many as the icon is wide
	std::wstring m_Tip;
};
