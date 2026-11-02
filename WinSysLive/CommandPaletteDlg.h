#pragma once

#include "resource.h"
#include <functional>
#include <mutex>
#include <thread>

//
// View > Command Palette (Ctrl+Q): every command of the main menu (the tabs are View's), as a list that typing filters,
// in a borderless popup near the top of the frame. Every word typed must be in the command's name or its menu path;
// names that start with the text come first. Up, Down, Page Up and Page Down move in the list while typing; Enter (or
// a double-click) runs the selected command (posted to the frame, which routes a view's commands to the active view);
// Escape, or clicking elsewhere, closes it. While nothing is typed, the commands run last from it come first. The frame
// builds the list (CMainFrame::OnCommandPalette), leaving out the commands its update UI has disabled.
// It also searches what the frame gives it, below the commands once two characters are typed: items (processes, from
// the snapshot) and those a loader reads on a thread of the palette's (services, drivers, devices; merged when it's
// done, the list shown again; "Searching..." until then), each word typed in the name, kind or detail, names starting
// with the text first, at most MaxPerKind of a kind and MaxResults in all. Choosing one closes the palette with its
// action (GetSearchAction), which the frame runs.
//
class CCommandPaletteDlg : public CDialogImpl<CCommandPaletteDlg> {
public:
	enum { IDD = IDD_COMMANDPALETTE };

	struct Command {
		UINT Id;
		std::wstring Name;		// without the & and the shortcut
		std::wstring Path;		// the menus it's in: "View", "System > Memory Lists"
		std::wstring Shortcut;	// "Ctrl+Q", or empty
		int Image{ -1 };		// in the image list given, or none
		bool Checked{ false };
	};

	// something to go to: a process, a service... (Name in the first column, Kind in the second, Detail in the third)
	struct SearchItem {
		std::wstring Name, Kind, Detail;
		int Image{ -1 };
		std::function<void()> Go;	// run by the frame after the palette closed
	};
	static constexpr size_t MaxResults = 100, MaxPerKind = 25;

	// images: the frame's (shared, not owned); recent: the commands run last, newest first, listed first while nothing
	// is typed
	CCommandPaletteDlg(std::vector<Command> commands, HIMAGELIST images, std::vector<UINT> recent = {})
		: m_Commands(std::move(commands)), m_Images(images), m_Recent(std::move(recent)) {}

	// the items searched (see above): items now, the loader's when it returns (it runs on a thread of the palette's,
	// which the destructor waits for)
	void SetSearch(std::vector<SearchItem> items, std::function<std::vector<SearchItem>()> loader) {
		m_Search = std::move(items);
		m_Loader = std::move(loader);
	}

	// the search alone (View > Search, Ctrl+Shift+Q): no commands listed
	void SetSearchOnly() {
		m_SearchOnly = true;
	}

	// after IDOK: the command chosen, or 0 for a search result (its action)
	UINT GetCommand() const {
		return m_Chosen;
	}
	std::function<void()> const& GetSearchAction() const {
		return m_SearchAction;
	}

	BEGIN_MSG_MAP(CCommandPaletteDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SHOWN, OnShown)
		MESSAGE_HANDLER(WM_SEARCHLOADED, OnSearchLoaded)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		COMMAND_HANDLER(IDC_CP_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_HANDLER(IDC_CP_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	ALT_MSG_MAP(1)	// the filter box
		MESSAGE_HANDLER(WM_KEYDOWN, OnFilterKeyDown)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnActivate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnShown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	// posted from WM_INITDIALOG: the dialog is shown by the time it arrives
	static constexpr UINT WM_SHOWN = WM_APP + 1;
	// the loader is done (its items in m_Loaded)
	static constexpr UINT WM_SEARCHLOADED = WM_APP + 2;
	LRESULT OnSearchLoaded(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	// the commands the filter matches, the first selected
	void ShowCommands();

	std::vector<Command> m_Commands;
	HIMAGELIST m_Images;
	std::vector<UINT> m_Recent;
	CContainedWindowT<CEdit> m_Filter{ this, 1 };
	CListViewCtrl m_List;
	UINT m_Chosen{ 0 };
	bool m_Closing{ false };	// ending: the deactivation that follows isn't a cancel

	std::vector<SearchItem> m_Search;
	bool m_SearchOnly{ false };
	std::function<std::vector<SearchItem>()> m_Loader;
	bool m_Loading{ false };
	std::mutex m_LoadLock;
	std::optional<std::vector<SearchItem>> m_Loaded;	// the loader's, until merged
	std::function<void()> m_SearchAction;
	std::jthread m_Worker;	// last: joined before the members it fills go
};
