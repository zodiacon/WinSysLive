#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "Alerts.h"
#include <functional>

//
// Options > Alerts... (also the alert icon's and the tray icons' menu): a modal, resizable dialog. The rules above
// (check boxes enable them; Add..., Edit... (or a double-click) and Remove; every change is saved and applied at once
// through CAlerts::SetRules, which restarts the rules' conditions), the recent alerts below (time, text; newest first;
// Clear). Double-clicking a recent process alert whose process still runs goes to it, closing the dialog.
//
class CAlertsDlg :
	public CDialogImpl<CAlertsDlg>,
	public CDialogResize<CAlertsDlg>,
	public CDpiDialogResize<CAlertsDlg> {
public:
	enum { IDD = IDD_ALERTS };

	// changed: after the rules change (the frame shows or hides the alert icon); goToProcess: selects a process
	CAlertsDlg(CAlerts& alerts, std::function<void()> changed, std::function<void(uint32_t)> goToProcess)
		: m_Alerts(alerts), m_Changed(std::move(changed)), m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CAlertsDlg)
		DLGRESIZE_CONTROL(IDC_AL_RULES, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_AL_ADD, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_AL_EDIT, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_AL_REMOVE, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_AL_RECENT, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_AL_CLEAR, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_AL_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CAlertsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_AL_RULES, LVN_ITEMCHANGED, OnRuleChanged)
		NOTIFY_HANDLER(IDC_AL_RULES, NM_DBLCLK, OnRuleDoubleClick)
		NOTIFY_HANDLER(IDC_AL_RECENT, NM_DBLCLK, OnRecentDoubleClick)
		COMMAND_ID_HANDLER(IDC_AL_ADD, OnAdd)
		COMMAND_ID_HANDLER(IDC_AL_EDIT, OnEdit)
		COMMAND_ID_HANDLER(IDC_AL_REMOVE, OnRemove)
		COMMAND_ID_HANDLER(IDC_AL_CLEAR, OnClear)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CAlertsDlg>)
		CHAIN_MSG_MAP(CDialogResize<CAlertsDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRuleChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRuleDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRecentDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnAdd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRemove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void ShowRules(int select);
	void ShowRecent();
	void Apply();	// m_Rules saved and given to the alerts
	void UpdateButtons();

	CAlerts& m_Alerts;
	std::function<void()> m_Changed;
	std::function<void(uint32_t)> m_GoToProcess;
	std::vector<AlertRule> m_Rules;
	CListViewCtrl m_RuleList, m_RecentList;
	bool m_Filling{ false };	// check box changes while the list is filled aren't the user's
};

// a rule's kind, image names, threshold and seconds
class CAlertRuleDlg : public CDialogImpl<CAlertRuleDlg> {
public:
	enum { IDD = IDD_ALERT_RULE };

	explicit CAlertRuleDlg(AlertRule rule) : m_Rule(std::move(rule)) {}
	AlertRule const& GetRule() const {
		return m_Rule;
	}

	BEGIN_MSG_MAP(CAlertRuleDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_AR_KIND, CBN_SELCHANGE, OnKindChanged)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKindChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the fields the kind uses enabled, the threshold's unit shown
	void UpdateFields();

	AlertRule m_Rule;
	CComboBox m_Kind;
};
