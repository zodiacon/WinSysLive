#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <VirtualListView.h>
#include <thread>
#include <mutex>
#include <functional>
#include <WinSys/Handles.h>

struct IMainFrame;

//
// Find Handle or DLL (Ctrl+Shift+F): every process's handles whose names, and loaded modules whose paths, contain the
// text. Modeless, on the UI thread; the search runs on a worker thread of its own (std::jthread, stopped and joined
// when a new search starts or the dialog closes) and posts the results as it finds them. Handle names are the Handles
// page's (WinSys HandleNames::QueryName); a file's name query can hang, so it has a timeout, and a process with a hung one has the
// rest of its files skipped (and past a limit, all files are). Processes that can't be opened (other users', protected ones; most without elevation) are counted.
// Double-clicking a result (or Go to Process) selects its process in the Processes view; Properties (also in a result's
// context menu) opens a handle's properties (CHandleSheet). Mapped Files adds the data files mapped into the processes
// (WinSys ProcessModuleTracker's views, not the images, which DLLs has). Whole Path matches a name that is the text's
// path or under it (LockPath.h): who's locking a file or a folder (FindLocks, File > Who's Locking a File..., a folder
// dropped on the window, Explorer's menu); Close Handle and Kill Process act on the selected result.
//
class CFindHandleDlg :
	public CDialogImpl<CFindHandleDlg>,
	public CDialogResize<CFindHandleDlg>,
	public CDpiDialogResize<CFindHandleDlg>,
	public CVirtualListView<CFindHandleDlg> {
public:
	enum { IDD = IDD_FIND_HANDLE };

	explicit CFindHandleDlg(IMainFrame* frame);
	// searches for what has the file or folder open, loaded or mapped (Whole Path, every kind)
	void FindLocks(std::wstring const& path);

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
		DLGRESIZE_CONTROL(IDC_FIND_CLOSEHANDLE, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FIND_KILL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FIND_PROPERTIES, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FIND_GOTO, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CFindHandleDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_FIND_PROGRESS, OnProgress)
		COMMAND_ID_HANDLER(IDC_FIND_SEARCH, OnSearch)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_ID_HANDLER(IDC_FIND_GOTO, OnGoToProcess)
		COMMAND_ID_HANDLER(IDC_FIND_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(IDC_FIND_CLOSEHANDLE, OnCloseHandle)
		COMMAND_ID_HANDLER(IDC_FIND_KILL, OnKill)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		COMMAND_HANDLER(IDC_FIND_TEXT, EN_CHANGE, OnTextChanged)
		NOTIFY_HANDLER(IDC_FIND_LIST, LVN_ITEMCHANGED, OnItemChanged)
		CHAIN_MSG_MAP(CVirtualListView<CFindHandleDlg>)
		CHAIN_MSG_MAP(CDpiDialogResize<CFindHandleDlg>)
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
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToProcess(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCloseHandle(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnTextChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	enum class Column { Process, Pid, Type, Value, Name };

	struct Result {
		uint32_t Pid;
		std::wstring ProcessName;
		std::wstring Type;	// the object type, "DLL" or "Mapped File"
		uint64_t Value;		// the handle, or the module's or view's base address
		std::wstring Name;
		std::optional<WinSys::HandleEntry> Handle;	// a handle's, for its properties
		int64_t Created{ 0 };	// the process's (FILETIME), so acting on it doesn't open another that got its ID
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
	struct SearchOptions {
		bool Handles, Dlls, Mapped;
		bool WholePath;	// the text is a path: a name is it or under it (else: contains it)
	};
	static void Search(std::stop_token stop, std::shared_ptr<SearchState> state, HWND hWnd, std::wstring text, SearchOptions options);
	// a process that exited since the search (or another error) told
	void ShowOpenError(Result const& r) const;
	// the results of a process gone, or of a handle closed
	void RemoveResults(std::function<bool(Result const&)> which);
	void UpdateStatus(bool done);
	Result const* GetSelected() const;
	void UpdateButtons();

	IMainFrame* m_Frame;
	CListViewCtrl m_List;
	std::vector<Result> m_Results;	// in display order
	std::shared_ptr<SearchState> m_State;
	std::jthread m_Worker;
	bool m_Searching{ false };
	unsigned m_LastId{ 0 };
};
