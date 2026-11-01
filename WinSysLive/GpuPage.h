#pragma once

#include "ProcessPages.h"
#include <WinSys/Gpu.h>
#include <WinSys/Processes.h>

//
// The process sheet's GPU page (added when the system has GPUs): the process's use of each adapter, from WinSys
// GpuInfo::QueryProcess with details: a graph per engine (3D, Copy, Video Decode...; its busiest node, as Task Manager
// shows it) and its dedicated and shared memory, with a line of the current values below each adapter's graphs.
// The query needs PROCESS_QUERY_INFORMATION (a handle opened once; not for protected processes, nor other users' without
// elevation: the page says so). Created with the sheet (PSP_PREMATURE) and fed every sample, as the Performance page,
// so its graphs have history when it's first shown; nothing more once the process is gone.
//
class CProcGpuPage : public CPropertyPageImpl<CProcGpuPage> {
public:
	enum { IDD = IDD_PROC_GPU };

	explicit CProcGpuPage(ProcessProperties const& props);

	void OnSample(ProcessSnapshot const& snapshot);

	BEGIN_MSG_MAP(CProcGpuPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcGpuPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// an adapter's graphs and line
	struct Adapter {
		GraphCtrl::CGraphGrid Engines;
		GraphCtrl::CGraphControl Memory;
		CStatic Line;
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	void Layout(int cx, int cy);

	ProcessProperties const& m_Props;
	std::vector<std::unique_ptr<Adapter>> m_Adapters;	// in GpuInfo::GetAdapters order
	wil::unique_handle m_hProcess;	// PROCESS_QUERY_INFORMATION (OpenSheetProcess); null if it can't be opened so
	DWORD m_OpenError{ 0 };
	WinSys::GpuProcessState m_State;
	bool m_Exited{ false };
};
