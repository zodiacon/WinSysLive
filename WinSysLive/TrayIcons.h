#pragma once

#include "TrayIcon.h"
#include <array>
#include <WinSys/Network.h>

//
// The notification area icons, any of CPU, I/O, commit charge, physical memory and network, each a graph with the
// details in its tooltip (Options > Tray Icons..., CTrayIconsDlg), in the TrayIcons setting as Key:Mode pairs
// ("CPU:Graph,IO:Off,..."; keys must not change; any mode but Off shows the graph). Before that setting existed, the
// TrayIcon setting showed the CPU graph alone, which is what an empty TrayIcons means. Each icon's ID is its kind + 1;
// they're added in kind order. Refresh samples only what's shown: the CPU percentage the frame computes, SYSTEM_PERFORMANCE_INFORMATION's I/O transfer counts (rates over QPC
// time, the Performance tab's I/O page's), its commit charge and limit and available pages, and a NetworkTracker over
// the adapters (the Performance tab's), held only while the Network icon is shown.
//
enum class TrayIconKind { Cpu, Io, Commit, Memory, Network, _Count };
enum class TrayIconMode { Off, Graph };

class CTrayIcons {
public:
	static constexpr int Count = (int)TrayIconKind::_Count;
	using Modes = std::array<TrayIconMode, Count>;

	struct KindInfo {
		PCWSTR Key;		// in the setting
		PCWSTR Name;	// in the dialog
		COLORREF Color;
	};
	static KindInfo const& Info(TrayIconKind kind);

	// the setting's modes (an empty setting: the CPU graph if the old TrayIcon setting is on)
	static Modes Load();
	static void Save(Modes const& modes);

	// hWnd gets callbackMessage (wParam: the icon's ID, lParam: the mouse message)
	void Init(HWND hWnd, UINT callbackMessage);
	// adds and removes icons, and sets their styles
	void Apply(Modes const& modes);
	// Explorer restarted: every shown icon added again
	void AddAgain();
	void RemoveAll();
	bool AnyShown() const;
	// cpu: percent, nothing when there's no value yet
	void Refresh(std::optional<double> cpu);

private:
	void UpdateIcon(TrayIconKind kind, double value, std::wstring const& tip);

	HWND m_hWnd{ nullptr };
	UINT m_Message{ 0 };
	Modes m_Modes{};
	std::array<CTrayIcon, Count> m_Icons;
	std::unique_ptr<WinSys::NetworkTracker> m_Network;
	int64_t m_PrevIo[3]{}, m_PrevIoTime{ 0 };	// read, write and other bytes; QPC
	int64_t m_PageSize{ 0 }, m_PhysicalTotal{ 0 };
};
