#pragma once

#include "ViewBase.h"
#include <TreeListView/include/TreeListView.h>

class CProcessesView :
	public CViewBase<CProcessesView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;
	void ShowProcess(uint32_t pid) override;

	BEGIN_MSG_MAP(CProcessesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(TLVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(TLVN_SORT, OnSort)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelectionChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		CHAIN_MSG_MAP(CViewBase<CProcessesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_PROCESS_KILL, OnKill)
		COMMAND_ID_HANDLER(ID_PROCESS_SUSPEND, OnSuspend)
		COMMAND_ID_HANDLER(ID_PROCESS_RESUME, OnResume)
		COMMAND_RANGE_HANDLER(ID_PRIORITY_REALTIME, ID_PRIORITY_IDLE, OnSetPriority)
		COMMAND_ID_HANDLER(ID_PROCESS_GOTOFILELOCATION, OnGoToFileLocation)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_PROCESSTREE, OnToggleTree)
		CHAIN_MSG_MAP_ALT(CViewBase<CProcessesView>, 1)
	END_MSG_MAP()

private:
	enum class Column {
		Name, Id, CPU, PrivateBytes, WorkingSet, Threads, Handles, Session, User, Priority, StartTime, ImagePath, CommandLine,
		_Count
	};

	struct ProcessItem {
		std::shared_ptr<ProcessInfoEx> Process;
		HTLITEM Item{ nullptr };
		int Image{ -1 };
		int NewTicks{ 0 };			// > 0: still highlighted as new
		bool Terminated{ false };	// highlighted for one update, then removed
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSort(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSelectionChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnKill(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSuspend(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnResume(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSetPriority(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnToggleTree(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void BuildItems();
	HTLITEM EnsureInserted(ProcessItem& item);
	void RemoveItem(ProcessItem& item);
	void CollectDescendants(HTLITEM hItem, std::vector<ProcessItem*>& items) const;
	ProcessItem* FindParent(ProcessItem const& item);
	ProcessItem* GetSelectedProcess() const;
	void RestoreSelection(WinSys::ProcessOrThreadKey const* key);
	void Sort();
	static int CALLBACK CompareItems(HTLITEM h1, HTLITEM h2, LPARAM lParam);
	int CompareProcesses(ProcessItem& item1, ProcessItem& item2) const;
	std::wstring GetColumnText(ProcessItem& item, Column column) const;
	int GetProcessIcon(ProcessInfoEx& process);
	void UpdateUI();
	static bool CanControl(ProcessItem const* item);
	void ShowError(PCWSTR operation) const;

	CTreeListViewCtrl m_Tree;
	std::unordered_map<WinSys::ProcessOrThreadKey, ProcessItem> m_Items;	// node-based: ProcessItem* stays valid
	std::unordered_map<std::wstring, int> m_Icons;	// image path (lowercase) to image list index
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
	bool m_TreeMode{ true };
	bool m_Active{ false };
};
