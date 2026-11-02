#pragma once

#include "ProcessPages.h"
#include <WinSys/UnloadedModules.h>

//
// The Unloaded page: the DLLs the process unloaded lately (WinSys UnloadedModules: ntdll's unload event trace, the
// last 64), newest first, read again on every sample while the page is active: name, base address, size, product
// version, link time stamp, checksum and the unload's sequence number. Unloads seen while the page is open are
// highlighted for a while (not on the first read); the selection is kept by sequence number. Copy and Copy All put
// tab-separated lines on the clipboard. Other users' processes need elevation, protected ones can't be read; for a
// 32-bit process the list is the 64-bit ntdll's (the status line says so).
//
class CProcUnloadedPage :
	public CPropertyPageImpl<CProcUnloadedPage>,
	public CDpiDynamicDialogLayout<CProcUnloadedPage>,
	public CVirtualListView<CProcUnloadedPage> {
public:
	enum { IDD = IDD_PROC_UNLOADED };

	CProcUnloadedPage(ProcessProperties const& props, ProcessSnapshot const& snapshot);

	// from the sheet, while the page is active
	void OnSample(ProcessSnapshot const& snapshot);

	// CPropertyPageImpl: 0 allows the activation
	int OnSetActive();

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_MSG_MAP(CProcUnloadedPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_UNLOADED_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_UNLOADED_COPYALL, OnCopy)
		NOTIFY_HANDLER(IDC_UNLOADED_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_UNLOADED_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CProcUnloadedPage>)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcUnloadedPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcUnloadedPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	enum class Column { Name, BaseAddress, Size, Version, TimeStamp, CheckSum, Sequence, _Count };

	struct Row : WinSys::UnloadedModule {
		int NewTicks{ 0 };	// highlighted while above 0
	};

	// reads the list and merges it into m_Rows; false (with the status line saying why) on failure
	bool Load();
	std::wstring ErrorText(DWORD error) const;
	std::wstring GetText(Row const& row, Column column) const;
	std::vector<uint32_t> GetSelectedSequences() const;
	void SelectSequences(std::vector<uint32_t> const& sequences);
	void UpdateStatus();
	void UpdateButtons();

	ProcessProperties const& m_Props;
	ProcessSnapshot const& m_Snapshot;
	CListViewCtrl m_List;
	std::vector<Row> m_Rows;	// in display order
	bool m_Loaded{ false };	// read at least once: later unloads are new
	bool m_Exited{ false };	// the process is gone: the rows stay as they were
	bool m_Wow64{ false };	// a 32-bit process: only the 64-bit ntdll's list
};
