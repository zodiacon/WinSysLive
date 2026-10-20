#pragma once

#include "resource.h"
#include <WinSys/WaitChain.h>
#include <thread>

//
// Wait chains (WinSys WaitChainTraversal) of one thread or of all a process's threads: what each thread waits for, which
// thread owns that, what that one waits for... A chain's nodes are rows, indented by their depth under the thread;
// a chain that leads back to itself (a deadlock) is red. The chains are read on a worker thread, as WCT may wait on hung
// processes, which posts WM_WAITCHAINS_READ; Refresh reads them again. Copy puts the rows on the clipboard.
//
class CWaitChainDlg :
	public CDialogImpl<CWaitChainDlg>,
	public CDialogResize<CWaitChainDlg> {
public:
	enum { IDD = IDD_WAITCHAIN };

	// tids: the threads whose chains are shown (one, or the process's); name: the process's image name, for the title
	CWaitChainDlg(uint32_t pid, std::wstring name, std::vector<uint32_t> tids);

	BEGIN_DLGRESIZE_MAP(CWaitChainDlg)
		DLGRESIZE_CONTROL(IDC_WAITCHAIN_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_WAITCHAIN_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_WAITCHAIN_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_WAITCHAIN_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CWaitChainDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_WAITCHAINS_READ, OnChainsRead)
		NOTIFY_HANDLER(IDC_WAITCHAIN_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(IDC_WAITCHAIN_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_WAITCHAIN_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDialogResize<CWaitChainDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	static constexpr UINT WM_WAITCHAINS_READ = WM_APP + 0x140;

	struct ThreadChain {
		uint32_t ThreadId;
		std::optional<WinSys::WaitChain> Chain;
		DWORD Error{ 0 };	// when there's no chain
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChainsRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Read();
	void ShowChains();
	std::wstring NodeText(WinSys::WaitChainNode const& node) const;
	int NodeImage(WinSys::WaitChainNode const& node) const;
	static std::wstring FormatWaitTime(uint32_t ms);

	uint32_t m_Pid;
	std::wstring m_Name;
	std::vector<uint32_t> m_Tids;
	CListViewCtrl m_List;
	CImageList m_Images;
	// written by the worker, read once it posts WM_WAITCHAINS_READ (the worker is joined before another read and when
	// the dialog goes)
	std::vector<ThreadChain> m_Chains;
	std::unordered_map<uint32_t, std::wstring> m_ProcessNames;
	std::vector<bool> m_DeadlockRows;	// by row: in a deadlocked chain
	std::jthread m_Worker;
};
