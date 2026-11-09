#include "pch.h"
#include "ProcessRulesDlg.h"
#include "AffinityDlg.h"
#include "FormatHelper.h"
#include <ListViewhelper.h>
#include <WinSys/SystemInfo.h>

LRESULT CProcessRulesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_CPU, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_CPU, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
	m_Rules = m_Runner.GetRules();

	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_RuleList.Attach(GetDlgItem(IDC_PRULE_RULES));
	m_RuleList.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_RuleList.InsertColumn(0, L"Rule", LVCFMT_LEFT, ::MulDiv(560, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_RuleList);
	m_RecentList.Attach(GetDlgItem(IDC_PRULE_RECENT));
	m_RecentList.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_RecentList.InsertColumn(0, L"Time", LVCFMT_LEFT, ::MulDiv(150, dpi, 96));
	m_RecentList.InsertColumn(1, L"Result", LVCFMT_LEFT, ::MulDiv(600, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_RecentList);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	ShowRules(0);
	ShowRecent();
	return TRUE;
}

void CProcessRulesDlg::ShowRules(int select) {
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

void CProcessRulesDlg::ShowRecent() {
	auto& recent = m_Runner.GetRecent();
	m_RecentList.SetRedraw(FALSE);
	m_RecentList.DeleteAllItems();
	// newest first
	int i = 0;
	for (auto it = recent.rbegin(); it != recent.rend(); ++it, ++i) {
		m_RecentList.InsertItem(i, FormatHelper::FormatDateTime(it->Time).c_str());
		m_RecentList.SetItemText(i, 1, it->Text.c_str());
	}
	m_RecentList.SetRedraw(TRUE);
	UpdateButtons();
}

void CProcessRulesDlg::UpdateButtons() {
	bool selected = m_RuleList.GetSelectedCount() > 0;
	GetDlgItem(IDC_PRULE_EDIT).EnableWindow(selected);
	GetDlgItem(IDC_PRULE_REMOVE).EnableWindow(selected);
	GetDlgItem(IDC_PRULE_CLEAR).EnableWindow(m_RecentList.GetItemCount() > 0);
	auto enabled = std::ranges::count_if(m_Rules, &ProcessRule::Enabled);
	std::wstring text = m_Rules.empty() ? L"No rules: Add... makes one"
		: std::format(L"{} rules, {} enabled; {} results", m_Rules.size(), enabled, m_Runner.GetRecent().size());
	SetDlgItemText(IDC_PRULE_STATUS, text.c_str());
}

void CProcessRulesDlg::Save() {
	CProcessRules::Save(m_Rules);
	m_Runner.SetRules(m_Rules);
	UpdateButtons();
}

LRESULT CProcessRulesDlg::OnRuleChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (m_Filling || lv->iItem < 0 || lv->iItem >= (int)m_Rules.size())
		return 0;
	// the check box is the state image: 1 unchecked, 2 checked
	if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK)) {
		bool checked = ((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
		if (checked != m_Rules[lv->iItem].Enabled) {
			m_Rules[lv->iItem].Enabled = checked;
			Save();
			if (checked) {
				m_Runner.ApplyToRunning(m_ProcessManager, m_Rules[lv->iItem]);
				ShowRecent();
			}
		}
	}
	UpdateButtons();
	return 0;
}

LRESULT CProcessRulesDlg::OnRuleDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnEdit(0, 0, nullptr, handled);
}

LRESULT CProcessRulesDlg::OnAdd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CProcessRuleDlg dlg{ ProcessRule{} };
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	m_Rules.push_back(dlg.GetRule());
	Save();
	m_Runner.ApplyToRunning(m_ProcessManager, m_Rules.back());
	ShowRules((int)m_Rules.size() - 1);
	ShowRecent();
	return 0;
}

LRESULT CProcessRulesDlg::OnEdit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_RuleList.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || selected >= (int)m_Rules.size())
		return 0;
	CProcessRuleDlg dlg(m_Rules[selected]);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	m_Rules[selected] = dlg.GetRule();
	Save();
	m_Runner.ApplyToRunning(m_ProcessManager, m_Rules[selected]);
	ShowRules(selected);
	ShowRecent();
	return 0;
}

LRESULT CProcessRulesDlg::OnRemove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int selected = m_RuleList.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || selected >= (int)m_Rules.size())
		return 0;
	m_Rules.erase(m_Rules.begin() + selected);
	Save();
	ShowRules((std::min)(selected, (int)m_Rules.size() - 1));
	return 0;
}

LRESULT CProcessRulesDlg::OnClear(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Runner.ClearRecent();
	ShowRecent();
	return 0;
}

LRESULT CProcessRulesDlg::OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(IDCANCEL);
	return 0;
}

LRESULT CProcessRuleDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetDlgItemText(IDC_PRULE_PATTERN, m_Rule.Pattern.c_str());
	m_Priority.Attach(GetDlgItem(IDC_PRULE_PRIORITY));
	m_Priority.AddString(L"Don't change");
	m_Priority.SetCurSel(0);
	auto& priorities = ProcessRule::Priorities();
	for (int i = 0; i < (int)priorities.size(); i++) {
		m_Priority.AddString(priorities[i].Text);
		if (m_Rule.Priority == priorities[i].Class)
			m_Priority.SetCurSel(i + 1);
	}
	m_Efficiency.Attach(GetDlgItem(IDC_PRULE_EFFICIENCY));
	m_Efficiency.AddString(L"Don't change");
	m_Efficiency.AddString(L"On");
	m_Efficiency.AddString(L"Off");
	m_Efficiency.SetCurSel(m_Rule.Efficiency ? (*m_Rule.Efficiency ? 1 : 2) : 0);
	CheckDlgButton(IDC_PRULE_AFFINITY, m_Rule.Affinity ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_PRULE_CPUSETS, m_Rule.CpuSets ? BST_CHECKED : BST_UNCHECKED);
	UpdateProcessors();
	CenterWindow(GetParent());
	return TRUE;
}

void CProcessRuleDlg::UpdateProcessors() {
	SetDlgItemText(IDC_PRULE_AFFINITY_TEXT, m_Rule.Affinity ? ProcessRule::ProcessorsText(*m_Rule.Affinity).c_str() : L"Don't change");
	SetDlgItemText(IDC_PRULE_CPUSETS_TEXT, m_Rule.CpuSets ? ProcessRule::ProcessorsText(*m_Rule.CpuSets).c_str() : L"Don't change");
	GetDlgItem(IDC_PRULE_AFFINITY_CHOOSE).EnableWindow(m_Rule.Affinity.has_value());
	GetDlgItem(IDC_PRULE_CPUSETS_CHOOSE).EnableWindow(m_Rule.CpuSets.has_value());
}

LRESULT CProcessRuleDlg::OnAffinityCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (IsDlgButtonChecked(IDC_PRULE_AFFINITY)) {
		// every processor, until some are chosen
		DWORD_PTR mask = 0, systemMask = 0;
		::GetProcessAffinityMask(::GetCurrentProcess(), &mask, &systemMask);
		m_Rule.Affinity = (uint64_t)systemMask;
	}
	else
		m_Rule.Affinity.reset();
	UpdateProcessors();
	return 0;
}

LRESULT CProcessRuleDlg::OnCpuSetsCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (IsDlgButtonChecked(IDC_PRULE_CPUSETS))
		m_Rule.CpuSets = std::vector<GROUP_AFFINITY>{};
	else
		m_Rule.CpuSets.reset();
	UpdateProcessors();
	return 0;
}

LRESULT CProcessRuleDlg::OnChooseAffinity(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	DWORD_PTR mask = 0, systemMask = 0;
	::GetProcessAffinityMask(::GetCurrentProcess(), &mask, &systemMask);
	CAffinityDlg dlg(L"Process Rule", (DWORD_PTR)m_Rule.Affinity.value_or(systemMask), systemMask);
	if (dlg.DoModal(m_hWnd) == IDOK) {
		m_Rule.Affinity = (uint64_t)dlg.GetMask();
		UpdateProcessors();
	}
	return 0;
}

LRESULT CProcessRuleDlg::OnChooseCpuSets(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::vector<CAffinityDlg::Group> choices;
	int first = 0;
	auto current = m_Rule.CpuSets.value_or(std::vector<GROUP_AFFINITY>{});
	for (auto& g : WinSys::SystemInfo::GetProcessorGroups()) {
		// none set: every processor
		KAFFINITY selected = g.ActiveMask;
		if (!current.empty()) {
			auto it = std::ranges::find(current, g.Group, &GROUP_AFFINITY::Group);
			selected = it == current.end() ? 0 : it->Mask & g.ActiveMask;
		}
		choices.push_back({ g.Group, g.ActiveMask, selected, true, first });
		first += g.ActiveCount;
	}
	CAffinityDlg dlg(L"Process Rule", choices, false, L"OK sets the processors as the process's default CPU sets.");
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	// every processor: none set
	std::vector<GROUP_AFFINITY> masks;
	bool all = true;
	for (auto& g : dlg.GetGroups()) {
		all &= g.Selected == g.Available;
		if (g.Selected)
			masks.push_back({ g.Selected, g.Number });
	}
	if (all)
		masks.clear();
	m_Rule.CpuSets = std::move(masks);
	UpdateProcessors();
	return 0;
}

LRESULT CProcessRuleDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_PRULE_PATTERN, text);
	m_Rule.Pattern = (PCWSTR)text.Trim();
	if (m_Rule.Pattern.empty()) {
		AtlMessageBox(m_hWnd, L"Name the images the rule is for: notepad.exe, or with wildcards, game*.exe.", L"Process Rule", MB_ICONWARNING);
		GotoDlgCtrl(GetDlgItem(IDC_PRULE_PATTERN));
		return 0;
	}
	int priority = m_Priority.GetCurSel();
	m_Rule.Priority.reset();
	if (priority > 0)
		m_Rule.Priority = ProcessRule::Priorities()[priority - 1].Class;
	int efficiency = m_Efficiency.GetCurSel();
	m_Rule.Efficiency.reset();
	if (efficiency > 0)
		m_Rule.Efficiency = efficiency == 1;
	if (!m_Rule.HasAction()) {
		AtlMessageBox(m_hWnd, L"Choose something for the rule to set.", L"Process Rule", MB_ICONWARNING);
		return 0;
	}
	// applied to the running processes as soon as it's saved, and nothing puts them back
	std::wstring warning;
	if (m_Rule.MatchesEverything())
		warning = std::format(L"This rule matches every process you can open, and is applied to the running ones at once: {}. Nothing puts them back when it's removed.",
			m_Rule.DescribeActions());
	else if (m_Rule.Priority == WinSys::ProcessPriorityClass::Realtime)
		warning = L"A process at Realtime priority can starve the rest of the system, input included. The rule is applied at once to the running processes it matches.";
	if (!warning.empty() && AtlMessageBox(m_hWnd, (warning + L"\n\nSave the rule?").c_str(), L"Process Rule", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
		return 0;
	EndDialog(IDOK);
	return 0;
}

LRESULT CProcessRuleDlg::OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(IDCANCEL);
	return 0;
}
