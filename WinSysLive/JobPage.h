#pragma once

#include <wil/resource.h>
#include <WinSys/Job.h>
#include <WinSys/Keys.h>
#include "ProcessPages.h"

//
// The Job page: the limits and statistics of a job the process is in, and the job's processes. There's no API that
// opens a process's job, so the page looks for job handles in the system's handle table, duplicates them (from
// processes it can open with PROCESS_DUP_HANDLE: all of them when elevated) and keeps those the process is in. For
// nested jobs that's the job and its ancestors: a combo box lists them, innermost first (a single job's name is in a
// read-only edit instead; the combo box is kept, hidden, as the current job's index). The jobs are found when the
// page is first shown; their limits, statistics and processes are read again on every sample while the page is active.
//
class CProcJobPage :
	public CPropertyPageImpl<CProcJobPage>,
	public CDpiDynamicDialogLayout<CProcJobPage> {
public:
	enum { IDD = IDD_PROC_JOB };

	CProcJobPage(ProcessProperties const& props, ProcessSnapshot const& snapshot);

	void OnSample(ProcessSnapshot const& snapshot);

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcJobPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_JOB_JOBS, CBN_SELCHANGE, OnJobChanged)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcJobPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcJobPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnJobChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// fills m_Jobs and the combo box; the status line says why there's no job
	void FindJobs();
	void ShowJob(ProcessSnapshot const& snapshot);
	void ShowLimits(HANDLE hJob);
	void ShowStatistics(HANDLE hJob);
	void ShowProcesses(HANDLE hJob, ProcessSnapshot const& snapshot);

	ProcessProperties const& m_Props;
	ProcessSnapshot const& m_Snapshot;
	CComboBox m_JobList;
	CListViewCtrl m_Limits, m_Stats, m_Processes;
	std::vector<WinSys::ProcessJob> m_Jobs;	// innermost first (WinSys JobInfo::FindProcessJobs)
	// the processes' image names, for when the snapshot is gone; by ID and creation time, as an ID is reused
	std::unordered_map<WinSys::ProcessOrThreadKey, std::wstring> m_Names;
	bool m_Loaded{ false };
	bool m_Selected{ false };	// the sheet's process was selected in the list
};
