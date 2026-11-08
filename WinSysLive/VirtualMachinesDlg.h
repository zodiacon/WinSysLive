#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "ProcessInfoEx.h"
#include <WinSys/VirtualMachines.h>
#include <functional>

//
// System > Virtual Machines and Containers: a modal, resizable dialog of what the Host Compute Service runs (Hyper-V's
// VMs, WSL 2's VM, Windows Sandbox and its template, containers and their utility VMs) and Hyper-V's VMs that are off or
// saved (WinSys VirtualMachines, read by VirtualMachineLookup's thread every 2 seconds; an administrator's): name, kind,
// state, the CPU its memory process uses (where Windows counts its virtual processors' time, as Task Manager shows it),
// virtual processors, assigned memory, its worker and memory processes (found in the frame's snapshot as the Virtual
// Machine column finds them), when its worker started, and its ID. Sorts by any column, keeping the selection;
// double-click (or Go to Worker Process, Go to Memory Process) goes to the process, closing the dialog; Copy copies the
// selected rows, or all.
//
class CVirtualMachinesDlg :
	public CDialogImpl<CVirtualMachinesDlg>,
	public CDialogResize<CVirtualMachinesDlg>,
	public CDpiDialogResize<CVirtualMachinesDlg> {
public:
	enum { IDD = IDD_VIRTUALMACHINES };

	// pm: the frame's snapshot, updated while the dialog is open; goToProcess: selects a process in the Processes tab
	CVirtualMachinesDlg(ProcessManager const& pm, std::function<void(uint32_t)> goToProcess) : m_ProcessManager(pm), m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CVirtualMachinesDlg)
		DLGRESIZE_CONTROL(IDC_VM_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_VM_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_VM_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_VM_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CVirtualMachinesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_VM_READ, OnRead)
		NOTIFY_HANDLER(IDC_VM_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_VM_LIST, NM_DBLCLK, OnDoubleClick)
		COMMAND_ID_HANDLER(IDC_VM_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_VM_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VM_GOTOWORKER, OnGoToProcess)
		COMMAND_ID_HANDLER(ID_VM_GOTOMEMORY, OnGoToProcess)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CVirtualMachinesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CVirtualMachinesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// VirtualMachineLookup's read ended
	static const UINT WM_VM_READ = WM_APP + 0x1E7;

	enum class Column { Name, Kind, State, Cpu, Processors, Memory, Worker, MemoryProcess, Started, Id, _Count };

	struct Row {
		WinSys::ComputeSystem System;
		uint32_t WorkerPid{ 0 }, MemoryPid{ 0 };
		int32_t Cpu{ -1 };		// the memory process's (ProcessInfo::CPU units); -1: none
		int64_t Started{ 0 };	// the worker's start
		std::wstring Cells[(int)Column::_Count];
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the rows from the last read and the current snapshot, in their order, keeping the selection by ID
	void Load();
	void Fill();
	Row const* SelectedRow() const;

	ProcessManager const& m_ProcessManager;
	std::function<void(uint32_t)> m_GoToProcess;
	CListViewCtrl m_List;
	std::vector<Row> m_Rows;
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
	bool m_Read{ false };	// a read ended since the dialog opened
};
