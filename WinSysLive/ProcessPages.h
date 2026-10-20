#pragma once

#include <VirtualListView.h>
#include "ThreadsPage.h"
#include "PerfPages.h"
#include "resource.h"

namespace WinSys {
	class Token;
	class ProcessNetworkTracker;
}

//
// What a process's property sheet shows of it: copied from the snapshot on the UI thread, because the sheet runs on
// a thread of its own. The pages query anything else from the process themselves.
//
struct ProcessProperties {
	uint32_t Id{ 0 };
	int64_t CreateTime{ 0 };
	uint32_t ParentId{ 0 };
	uint32_t SessionId{ 0 };
	std::wstring ImageName, ImagePath, CommandLine, User, Description;
	std::wstring ParentName;	// empty if the parent isn't running (or its ID was reused)
	// settings, which belong to the UI thread
	std::wstring ThreadColumns;
	int UpdateInterval{ 1000 };
};

//
// The General page: identity and origin of the process, its security mitigations and signature. Its controls follow
// the page's size through the dialog's AFX_DIALOG_LAYOUT resource (CDynamicDialogLayout)
//
class CProcGeneralPage :
	public CPropertyPageImpl<CProcGeneralPage>,
	public CDynamicDialogLayout<CProcGeneralPage> {
public:
	enum { IDD = IDD_PROC_GENERAL };

	CProcGeneralPage(ProcessProperties const& props, HICON hIcon);

	BEGIN_MSG_MAP(CProcGeneralPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_PROC_VERIFY, OnVerify)
		COMMAND_ID_HANDLER(IDC_PROC_KILL, OnKill)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcGeneralPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcGeneralPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVerify(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// ASLR, CFG, CET and DEP, from the process's mitigation policies (and the image's header, for ASLR)
	void ShowMitigations();

	ProcessProperties const& m_Props;
	HICON m_hIcon;	// the sheet's
};

//
// The Environment page: the process's environment variables, read from its process parameters (WinSys
// Process::GetEnvironment) when the page is first shown. Copy puts the selected variables on the clipboard,
// Copy All all of them, as NAME=value lines.
//
class CProcEnvironmentPage :
	public CPropertyPageImpl<CProcEnvironmentPage>,
	public CDynamicDialogLayout<CProcEnvironmentPage>,
	public CVirtualListView<CProcEnvironmentPage> {
public:
	enum { IDD = IDD_PROC_ENVIRONMENT };

	explicit CProcEnvironmentPage(ProcessProperties const& props);

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);

	BEGIN_MSG_MAP(CProcEnvironmentPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_ENV_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_ENV_COPYALL, OnCopy)
		NOTIFY_HANDLER(IDC_ENV_LIST, LVN_ITEMCHANGED, OnItemChanged)
		CHAIN_MSG_MAP(CVirtualListView<CProcEnvironmentPage>)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcEnvironmentPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcEnvironmentPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	// reads the variables; false (with the status line saying why) if they can't be read
	bool Load();
	void UpdateButtons();

	ProcessProperties const& m_Props;
	CListViewCtrl m_List;
	std::vector<std::pair<std::wstring, std::wstring>> m_Vars;	// in display order
};

//
// The Token page: the process token's properties (read-only edits, grouped), privileges and groups (with an
// AppContainer's capabilities), read from the token (WinSys Token) when the page is first shown
//
class CProcTokenPage :
	public CPropertyPageImpl<CProcTokenPage>,
	public CDynamicDialogLayout<CProcTokenPage> {
public:
	enum { IDD = IDD_PROC_TOKEN };

	explicit CProcTokenPage(ProcessProperties const& props);

	BEGIN_MSG_MAP(CProcTokenPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(CDynamicDialogLayout<CProcTokenPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcTokenPage>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	void ShowProperties(WinSys::Token const& token);
	void ShowPrivileges(WinSys::Token const& token);
	void ShowGroups(WinSys::Token const& token);

	ProcessProperties const& m_Props;
	CListViewCtrl m_Privileges, m_Groups;
};

//
// A snapshot of the processes (SystemExtendedProcessInformation, which has each process's threads after it), taken by
// the sheet on every tick for its pages, as the frame's belongs to the UI thread
//
class ProcessSnapshot {
public:
	explicit ProcessSnapshot(ProcessProperties const& props);

	void Take();
	// the process's entry in the last snapshot, valid until the next Take; nullptr if the snapshot failed or the
	// process is gone
	[[nodiscard]] SYSTEM_PROCESS_INFORMATION const* Process() const;
	// the process is gone (or its ID belongs to another process now)
	[[nodiscard]] bool Exited() const;
	// of the last snapshot (QPC)
	[[nodiscard]] int64_t Time() const;

private:
	ProcessProperties const& m_Props;
	std::unique_ptr<BYTE[]> m_Buffer;
	ULONG m_BufferSize{ 1 << 20 };
	SYSTEM_PROCESS_INFORMATION const* m_Process{ nullptr };
	bool m_Exited{ false };
	int64_t m_Time{ 0 };
};

//
// The Performance page: graphs of the process's CPU, memory, I/O and network use, and their current values below them,
// from the sheet's snapshots and the shared network tracker (which needs elevation). The page is created with the
// sheet (PSP_PREMATURE), so the graphs have history from the moment it opened. The graphs are in two columns when the
// page is wide enough, unless the One column check box is checked.
//
class CProcPerformancePage : public CPropertyPageImpl<CProcPerformancePage> {
public:
	enum { IDD = IDD_PROC_PERFORMANCE };

	explicit CProcPerformancePage(ProcessProperties const& props);

	void OnSample(ProcessSnapshot const& snapshot);

	BEGIN_MSG_MAP(CProcPerformancePage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		COMMAND_ID_HANDLER(IDC_PERF_VERTICAL, OnVertical)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcPerformancePage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVertical(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Layout(int cx, int cy);

	// the cumulative values the rates are computed from
	struct Counters {
		int64_t Time;	// QPC
		int64_t KernelTime, UserTime;
		int64_t Read, Write, Other;
		uint64_t Received{ 0 }, Sent{ 0 };
	};

	ProcessProperties const& m_Props;
	CCpuGraph m_Cpu;
	GraphCtrl::CGraphControl m_Memory, m_Io;
	CNetworkGraph m_Network;
	std::shared_ptr<WinSys::ProcessNetworkTracker> m_NetworkTracker;
	DWORD m_NetworkError{ 0 };	// why there's no tracker
	std::optional<Counters> m_Prev;
	bool m_Exited{ false };
	bool m_Vertical{ false };	// the graphs one above the other, whatever the width (the One column check box)
	CSize m_ButtonSize;	// the template's
};

//
// The Threads page: the lower pane's Threads tab (CThreadsPage), fed from the sheet's snapshots while the page is shown.
// Its columns start as the lower pane's; changes to them aren't saved. When the process exits, the last threads stay
// shown.
//
class CProcThreadsPage : public CPropertyPageImpl<CProcThreadsPage> {
public:
	enum { IDD = IDD_PROC_THREADS };

	CProcThreadsPage(ProcessProperties const& props, ProcessSnapshot const& snapshot);

	void OnSample(ProcessSnapshot const& snapshot);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcThreadsPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcThreadsPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	ProcessSnapshot const& m_Snapshot;
	CThreadsPage m_Threads;
};

//
// A page still to be implemented: its title, icon and a line saying what it will show
//
class CProcPlaceholderPage : public CPropertyPageImpl<CProcPlaceholderPage> {
public:
	enum { IDD = IDD_PROC_PAGE };

	// title: kept (not copied) until the page is created
	CProcPlaceholderPage(PCWSTR title, UINT icon, std::wstring text);

	BEGIN_MSG_MAP(CProcPlaceholderPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcPlaceholderPage>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	std::wstring m_Text;
};
