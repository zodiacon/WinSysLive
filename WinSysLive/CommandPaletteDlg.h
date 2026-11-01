#pragma once

#include "resource.h"

//
// View > Command Palette (Ctrl+Q): every command of the main menu (the tabs are View's), as a list that typing filters,
// in a borderless popup near the top of the frame. Every word typed must be in the command's name or its menu path;
// names that start with the text come first. Up, Down, Page Up and Page Down move in the list while typing; Enter (or
// a double-click) runs the selected command (posted to the frame, which routes a view's commands to the active view);
// Escape, or clicking elsewhere, closes it. The frame builds the list (CMainFrame::OnCommandPalette), leaving out the
// commands its update UI has disabled.
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

	// images: the frame's (shared, not owned); owner: positions the popup
	CCommandPaletteDlg(std::vector<Command> commands, HIMAGELIST images) : m_Commands(std::move(commands)), m_Images(images) {}

	// after IDOK
	UINT GetCommand() const {
		return m_Chosen;
	}

	BEGIN_MSG_MAP(CCommandPaletteDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SHOWN, OnShown)
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
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFilterKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	// the commands the filter matches, the first selected
	void ShowCommands();

	std::vector<Command> m_Commands;
	HIMAGELIST m_Images;
	CContainedWindowT<CEdit> m_Filter{ this, 1 };
	CListViewCtrl m_List;
	UINT m_Chosen{ 0 };
	bool m_Closing{ false };	// ending: the deactivation that follows isn't a cancel
};
