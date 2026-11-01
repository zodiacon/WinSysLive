#include "pch.h"
#include "AlertsDlg.h"
#include <ListViewhelper.h>

namespace {
	std::wstring TimeText(int64_t time) {
		FILETIME ft, local;
		ft.dwLowDateTime = (DWORD)time;
		ft.dwHighDateTime = (DWORD)(time >> 32);
		SYSTEMTIME st;
		if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
			return L"";
		WCHAR date[64]{}, clock[64]{};
		::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, _countof(date), nullptr);
		::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, clock, _countof(clock));
		return std::wstring(date) + L" " + clock;
	}
}

LRESULT CAlertsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_ALERT, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_ALERT, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
	m_Rules = m_Alerts.GetRules();

	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_RuleList.Attach(GetDlgItem(IDC_AL_RULES));
	m_RuleList.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_RuleList.InsertColumn(0, L"Alert when", LVCFMT_LEFT, ::MulDiv(560, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_RuleList);
	m_RecentList.Attach(GetDlgItem(IDC_AL_RECENT));
	m_RecentList.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
	m_RecentList.InsertColumn(0, L"Time", LVCFMT_LEFT, ::MulDiv(150, dpi, 96));
	m_RecentList.InsertColumn(1, L"Alert", LVCFMT_LEFT, ::MulDiv(480, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_RecentList);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	ShowRules(0);
	ShowRecent();
	return TRUE;
}

void CAlertsDlg::ShowRules(int select) {
	m_Filling = true;
	m_RuleList.DeleteAllItems();
	for (int i = 0; i < (int)m_Rules.size(); i++) {
		m_RuleList.InsertItem(i, m_Rules[i].Describe().c_str());
		m_RuleList.SetCheckState(i, m_Rules[i].Enabled);
	}
	if (select >= 0 && select < (int)m_Rules.size()) {
		m_RuleList.SetItemState(select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_RuleList.EnsureVisible(select, FALSE);
	}
	m_Filling = false;
	UpdateButtons();
}

void CAlertsDlg::ShowRecent() {
	auto& recent = m_Alerts.GetRecent();
	m_RecentList.SetRedraw(FALSE);
	m_RecentList.DeleteAllItems();
	// newest first
	int i = 0;
	for (auto it = recent.rbegin(); it != recent.rend(); ++it, ++i) {
		m_RecentList.InsertItem(i, TimeText(it->Time).c_str());
		m_RecentList.SetItemText(i, 1, it->Text.c_str());
		m_RecentList.SetItemData(i, (DWORD_PTR)(recent.size() - 1 - i));
	}
	m_RecentList.SetRedraw(TRUE);
	UpdateButtons();
}

void CAlertsDlg::UpdateButtons() {
	bool selected = m_RuleList.GetSelectedCount() > 0;
	GetDlgItem(IDC_AL_EDIT).EnableWindow(selected);
	GetDlgItem(IDC_AL_REMOVE).EnableWindow(selected);
	GetDlgItem(IDC_AL_CLEAR).EnableWindow(m_RecentList.GetItemCount() > 0);
	auto enabled = std::ranges::count_if(m_Rules, &AlertRule::Enabled);
	std::wstring text = m_Rules.empty() ? L"No rules: Add... makes one"
		: std::format(L"{} rules, {} enabled; {} recent alerts", m_Rules.size(), enabled, m_Alerts.GetRecent().size());
	SetDlgItemText(IDC_AL_STATUS, text.c_str());
}

void CAlertsDlg::Apply() {
	CAlerts::Save(m_Rules);
	m_Alerts.SetRules(m_Rules);
	if (m_Changed)
		m_Changed();
	UpdateButtons();
}

LRESULT CAlertsDlg::OnRuleChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (m_Filling || lv->iItem < 0 || lv->iItem >= (int)m_Rules.size())
		return 0;
	// the check box is the state image: 1 unchecked, 2 checked
	if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK)) {
		bool checked = ((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
		if (checked != m_Rules[lv->iItem].Enabled) {
			m_Rules[lv->iItem].Enabled = checked;
			Apply();
		}
	}
	UpdateButtons();
	return 0;
}

LRESULT CAlertsDlg::OnRuleDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnEdit(0, 0, nullptr, handled);
}

LRESULT CAlertsDlg::OnRecentDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	int selected = m_RecentList.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return 0;
	auto index = (size_t)m_RecentList.GetItemData(selected);
	auto& recent = m_Alerts.GetRecent();
	if (index >= recent.size() || !recent[index].Running || !recent[index].Pid || !m_GoToProcess)
		return 0;
	auto pid = recent[index].Pid;
	EndDialog(IDOK);
	m_GoToProcess(pid);
	return 0;
}

LRESULT CAlertsDlg::OnAdd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	AlertRule rule;
	rule.Kind = AlertKind::SystemCpu;
	rule.Threshold = 90;
	rule.Seconds = 10;
	CAlertRuleDlg dlg(rule);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	m_Rules.push_back(dlg.GetRule());
	Apply();
	ShowRules((int)m_Rules.size() - 1);
	return 0;
}

LRESULT CAlertsDlg::OnEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_RuleList.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || selected >= (int)m_Rules.size())
		return 0;
	CAlertRuleDlg dlg(m_Rules[selected]);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	m_Rules[selected] = dlg.GetRule();
	Apply();
	ShowRules(selected);
	return 0;
}

LRESULT CAlertsDlg::OnRemove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_RuleList.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || selected >= (int)m_Rules.size())
		return 0;
	m_Rules.erase(m_Rules.begin() + selected);
	Apply();
	ShowRules((std::min)(selected, (int)m_Rules.size() - 1));
	return 0;
}

LRESULT CAlertsDlg::OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Alerts.ClearRecent();
	ShowRecent();
	return 0;
}

LRESULT CAlertsDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}

LRESULT CAlertRuleDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Kind.Attach(GetDlgItem(IDC_AR_KIND));
	for (int k = 0; k < (int)AlertKind::_Count; k++)
		m_Kind.AddString(AlertRule::KindName((AlertKind)k));
	m_Kind.SetCurSel((int)m_Rule.Kind);
	SetDlgItemText(IDC_AR_PATTERN, m_Rule.Pattern.c_str());
	SetDlgItemText(IDC_AR_THRESHOLD, (m_Rule.Threshold == (int64_t)m_Rule.Threshold ? std::to_wstring((int64_t)m_Rule.Threshold)
		: std::format(L"{:.1f}", m_Rule.Threshold)).c_str());
	SetDlgItemInt(IDC_AR_SECONDS, m_Rule.Seconds, FALSE);
	UpdateFields();
	CenterWindow(GetParent());
	return TRUE;
}

void CAlertRuleDlg::UpdateFields() {
	auto kind = (AlertKind)m_Kind.GetCurSel();
	bool pattern = AlertRule::HasPattern(kind), threshold = AlertRule::HasThreshold(kind);
	for (int id : { IDC_AR_PATTERN, IDC_AR_PATTERN_LABEL })
		GetDlgItem(id).EnableWindow(pattern);
	for (int id : { IDC_AR_THRESHOLD, IDC_AR_THRESHOLD_LABEL, IDC_AR_UNIT, IDC_AR_SECONDS, IDC_AR_SECONDS_LABEL })
		GetDlgItem(id).EnableWindow(threshold);
	SetDlgItemText(IDC_AR_UNIT, AlertRule::Unit(kind));
	// image names for the process rules, service names (or display names) for the service rules
	bool service = AlertRule::IsServiceKind(kind);
	SetDlgItemText(IDC_AR_PATTERN_LABEL, service ? L"&Services:" : L"&Image names:");
	::SendDlgItemMessage(m_hWnd, IDC_AR_PATTERN, EM_SETCUEBANNER, TRUE, (LPARAM)(service
		? L"Any service (name or display name, e.g. Spooler;Windows Update)" : L"Any process (e.g. notepad.exe;msedge*.exe)"));
}

LRESULT CAlertRuleDlg::OnKindChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	UpdateFields();
	return 0;
}

LRESULT CAlertRuleDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Rule.Kind = (AlertKind)m_Kind.GetCurSel();
	CString text;
	GetDlgItemText(IDC_AR_PATTERN, text);
	m_Rule.Pattern = (PCWSTR)text.Trim();
	if (AlertRule::HasThreshold(m_Rule.Kind)) {
		GetDlgItemText(IDC_AR_THRESHOLD, text);
		WCHAR* end = nullptr;
		auto value = ::wcstod(text.Trim(), &end);
		bool percent = m_Rule.Kind != AlertKind::ProcessPrivateBytes;
		if (text.IsEmpty() || (end && *end) || value < 0 || (percent && value > 100)) {
			AtlMessageBox(m_hWnd, percent ? L"The threshold is a percentage, from 0 to 100." : L"The threshold is a number of megabytes.",
				L"Alert", MB_ICONWARNING);
			GotoDlgCtrl(GetDlgItem(IDC_AR_THRESHOLD));
			return 0;
		}
		m_Rule.Threshold = value;
		BOOL ok = FALSE;
		auto seconds = GetDlgItemInt(IDC_AR_SECONDS, &ok, FALSE);
		m_Rule.Seconds = ok ? (int)(std::min)(seconds, 86400u) : 0;
	}
	EndDialog(wID);
	return 0;
}

LRESULT CAlertRuleDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
