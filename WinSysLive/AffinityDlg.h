#pragma once

#include "resource.h"

//
// A process's or a thread's processor affinity: a check box for each processor that can be chosen, laid out in rows of
// eight when the dialog opens. With more than one processor group (over 64 logical processors) a combo box chooses the
// group shown, each group keeping its own selection; a thread runs in one group, so choosing processors in another
// clears the rest (the caller moves it there). Processors are numbered across the groups (CPU 64 is group 1's first).
//
class CAffinityDlg : public CDialogImpl<CAffinityDlg> {
public:
	enum { IDD = IDD_AFFINITY };

	struct Group {
		uint16_t Number;
		KAFFINITY Available;	// the processors that can be chosen
		KAFFINITY Selected;
		bool Enabled{ true };	// false: shown, not changeable (a process held to another group)
		int FirstProcessor{ 0 };	// the number its bit 0 has, counting the groups before it
	};

	// title: for the caption (the process or thread); mask: the current affinity; systemMask: the processors there are;
	// thread: the label speaks of the thread
	CAffinityDlg(std::wstring title, DWORD_PTR mask, DWORD_PTR systemMask, bool thread = false);
	// several groups; note: under the combo box, what the choice does (the caller knows how it's applied)
	CAffinityDlg(std::wstring title, std::vector<Group> groups, bool thread, std::wstring note);

	// after IDOK
	DWORD_PTR GetMask() const;
	std::vector<Group> const& GetGroups() const {
		return m_Groups;
	}

	BEGIN_MSG_MAP(CAffinityDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_AFF_ALL, OnSelect)
		COMMAND_ID_HANDLER(IDC_AFF_NONE, OnSelect)
		COMMAND_HANDLER(IDC_AFF_GROUP, CBN_SELCHANGE, OnGroupChanged)
		COMMAND_RANGE_HANDLER(FirstCheckId, FirstCheckId + 63, OnCheck)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSelect(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGroupChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the check boxes' IDs: the shown group's processor n is FirstCheckId + n
	enum { FirstCheckId = 2000 };

	// the shown group's processors on the check boxes (created once, in WM_INITDIALOG, as the theming reaches only
	// the windows there are then)
	void ShowGroup(int index);
	// the shown group's selection from its check boxes
	KAFFINITY ReadMask() const;
	// a thread's processors are in one group: a selection in the shown one clears the others'
	void KeepOneGroup();
	// the combo box's texts (the counts chosen) and OK, which needs a processor
	void UpdateCounts();

	std::wstring m_Title, m_Note;
	std::vector<Group> m_Groups;
	int m_Shown{ -1 };
	bool m_Thread;
	CRect m_Area;	// where the check boxes go
	CSize m_Cell;
	CButton m_Checks[64];	// by bit
};
