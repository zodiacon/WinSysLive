#pragma once

#include "resource.h"
#include "Symbols.h"

//
// A thread's user-mode stack: the frames, innermost first, with their symbols and source lines (a WOW64 thread's
// native frames, then its 32-bit frames). The stack is captured on the symbol engine's thread, which may download
// symbols, so the dialog opens at once and fills when the capture arrives (WM_STACK_CAPTURED); Refresh captures it
// again. Copy puts the frames on the clipboard as tab-separated lines.
//
class CStackDlg :
	public CDialogImpl<CStackDlg>,
	public CDialogResize<CStackDlg> {
public:
	enum { IDD = IDD_STACK };

	// created: the process's creation time; name: its image name, for the title
	CStackDlg(uint32_t pid, int64_t created, uint32_t tid, std::wstring name);

	BEGIN_DLGRESIZE_MAP(CStackDlg)
		DLGRESIZE_CONTROL(IDC_STACK_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_STACK_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_STACK_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_STACK_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CStackDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_STACK_CAPTURED, OnStackCaptured)
		COMMAND_ID_HANDLER(IDC_STACK_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_STACK_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDialogResize<CStackDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnStackCaptured(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Capture();

	uint32_t m_Pid, m_Tid;
	int64_t m_Created;
	std::wstring m_Name;
	CListViewCtrl m_List;
	std::vector<StackFrame> m_Frames;
};
