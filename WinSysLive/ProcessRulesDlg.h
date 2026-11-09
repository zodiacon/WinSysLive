#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "ProcessRuleRunner.h"

//
// Options > Process Rules...: a modal, resizable dialog. The rules above (check boxes enable them; Add..., Edit... (or a
// double-click) and Remove; every change is saved at once, and a rule added, changed or turned on is applied to the
// matching running processes), what the rules did below (time, text; newest first; Clear).
//
class CProcessRulesDlg :
	public CDialogImpl<CProcessRulesDlg>,
	public CDialogResize<CProcessRulesDlg>,
	public CDpiDialogResize<CProcessRulesDlg> {
public:
	enum { IDD = IDD_PROCESS_RULES };

	CProcessRulesDlg(CProcessRules& rules, ProcessManager& pm) : m_Runner(rules), m_ProcessManager(pm) {}

	BEGIN_DLGRESIZE_MAP(CProcessRulesDlg)
		DLGRESIZE_CONTROL(IDC_PRULE_RULES, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PRULE_ADD, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PRULE_EDIT, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PRULE_REMOVE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PRULE_RECENT, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_PRULE_CLEAR, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_PRULE_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CProcessRulesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_PRULE_RULES, LVN_ITEMCHANGED, OnRuleChanged)
		NOTIFY_HANDLER(IDC_PRULE_RULES, NM_DBLCLK, OnRuleDoubleClick)
		COMMAND_ID_HANDLER(IDC_PRULE_ADD, OnAdd)
		COMMAND_ID_HANDLER(IDC_PRULE_EDIT, OnEdit)
		COMMAND_ID_HANDLER(IDC_PRULE_REMOVE, OnRemove)
		COMMAND_ID_HANDLER(IDC_PRULE_CLEAR, OnClear)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CProcessRulesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CProcessRulesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRuleChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRuleDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnAdd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRemove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void ShowRules(int select);
	void ShowRecent();
	void Save();	// m_Rules saved and given to the runner
	void UpdateButtons();

	CProcessRules& m_Runner;
	ProcessManager& m_ProcessManager;
	std::vector<ProcessRule> m_Rules;
	CListViewCtrl m_RuleList, m_RecentList;
	bool m_Filling{ false };	// check box changes while the list is filled aren't the user's
};

// a rule's image names and what it sets
class CProcessRuleDlg : public CDialogImpl<CProcessRuleDlg> {
public:
	enum { IDD = IDD_PROCESS_RULE };

	explicit CProcessRuleDlg(ProcessRule rule) : m_Rule(std::move(rule)) {}
	ProcessRule const& GetRule() const {
		return m_Rule;
	}

	BEGIN_MSG_MAP(CProcessRuleDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDC_PRULE_AFFINITY, OnAffinityCheck)
		COMMAND_ID_HANDLER(IDC_PRULE_CPUSETS, OnCpuSetsCheck)
		COMMAND_ID_HANDLER(IDC_PRULE_AFFINITY_CHOOSE, OnChooseAffinity)
		COMMAND_ID_HANDLER(IDC_PRULE_CPUSETS_CHOOSE, OnChooseCpuSets)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnAffinityCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCpuSetsCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChooseAffinity(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChooseCpuSets(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the check boxes' texts beside them and the Choose buttons
	void UpdateProcessors();

	ProcessRule m_Rule;
	CComboBox m_Priority, m_Efficiency;
};
