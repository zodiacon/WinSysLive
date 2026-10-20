#pragma once

#include "resource.h"
#include <VirtualListView.h>
#include <thread>
#include <mutex>

struct IMainFrame;

//
// Find Handle or DLL (Ctrl+Shift+F): every process's handles whose names, and loaded modules whose paths, contain the
// text. Modeless, on the UI thread; the search runs on a worker thread of its own (std::jthread, stopped and joined
// when a new search starts or the dialog closes) and posts the results as it finds them. Handle names are the Handles
// page's (WinSys HandleNames::QueryName); a file's name query can hang, so it has a timeout, and a process with a hung one has the
// rest of its files skipped (and past a limit, all files are). Processes that can't be opened (other users', protected ones; most without elevation) are counted.
// Double-clicking a result (or Go to Process) selects its process in the Processes view.
//
class CFindHandleDlg :
	public CDialogImpl<CFindHandleDlg>,
	public CDialogResize<CFindHandleDlg>,
	public CVirtualListView<CFindHandleDlg> {
public:
	enum { IDD = IDD_FIND_HANDLE };

	explicit CFindHandleDlg(IMainFrame* frame);

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	BEGIN_DLGRESIZE_MAP(CFindHandleDlg)
		DLGRESIZE_CONTROL(IDC_FIND_TEXT, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_FIND_SEARCH, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_FIND_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_FIND_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FIND_GOTO, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CFindHandleDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_FIND_PROGRESS, OnProgress)
		COMMAND_ID_HANDLER(IDC_FIND_SEARCH, OnSearch)
		COMMAND_ID_HANDLER(IDC_FIND_GOTO, OnGoToProcess)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		COMMAND_HANDLER(IDC_FIND_TEXT, EN_CHANGE, OnTextChanged)
		NOTIFY_HANDLER(IDC_FIND_LIST, LVN_ITEMCHANGED, OnItemChanged)
		CHAIN_MSG_MAP(CVirtualListView<CFindHandleDlg>)
		CHAIN_MSG_MAP(CDialogResize<CFindHandleDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	// posted by the worker: new results or progress (the dialog takes them from the shared state); wParam: done,
	// lParam: the search's ID
	static constexpr UINT WM_FIND_PROGRESS = WM_APP + 0x110;

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnProgress(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSearch(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTextChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	enum class Column { Process, Pid, Type, Value, Name };

	struct Result {
		uint32_t Pid;
		std::wstring ProcessName;
		std::wstring Type;	// the object type, or "DLL"
		uint64_t Value;		// the handle, or the module's base address
		std::wstring Name;
	};

	// what the worker and the dialog share (the worker may outlive nothing: the dialog joins it)
	struct SearchState {
		unsigned Id;	// a message from an earlier search is told apart by it
		std::mutex Lock;
		std::vector<Result> Pending;	// found, not yet taken by the dialog
		int Processes{ 0 }, Searched{ 0 }, Denied{ 0 };	// processes: all, searched so far, not opened
		std::wstring Current;	// the process being searched
		bool FilesSkipped{ false };	// file name queries hung too often
		bool Truncated{ false };	// stopped at the result limit
	};

	void StartSearch();
	// cancels the search in progress (if any) and waits for its thread
	void StopSearch();
	static void Search(std::stop_token stop, std::shared_ptr<SearchState> state, HWND hWnd, std::wstring text, bool handles, bool dlls);
	void UpdateStatus(bool done);
	Result const* GetSelected() const;

	IMainFrame* m_Frame;
	CListViewCtrl m_List;
	std::vector<Result> m_Results;	// in display order
	std::shared_ptr<SearchState> m_State;
	std::jthread m_Worker;
	bool m_Searching{ false };
	unsigned m_LastId{ 0 };
};
