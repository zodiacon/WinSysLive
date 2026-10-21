#include "pch.h"
#include "AffinityDlg.h"
#include <bit>

namespace {
	const int PerRow = 8;
	const int MaxBits = (int)sizeof(KAFFINITY) * 8;

	bool HasBit(KAFFINITY mask, int bit) {
		return (mask & ((KAFFINITY)1 << bit)) != 0;
	}
}

CAffinityDlg::CAffinityDlg(std::wstring title, DWORD_PTR mask, DWORD_PTR systemMask, bool thread) :
	m_Title(std::move(title)), m_Groups{ { 0, systemMask, mask & systemMask } }, m_Thread(thread) {
}

CAffinityDlg::CAffinityDlg(std::wstring title, std::vector<Group> groups, bool thread, std::wstring note) :
	m_Title(std::move(title)), m_Note(std::move(note)), m_Groups(std::move(groups)), m_Thread(thread) {
}

DWORD_PTR CAffinityDlg::GetMask() const {
	return m_Groups.empty() ? 0 : m_Groups[0].Selected;
}

LRESULT CAffinityDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetWindowText(std::format(L"Processor Affinity: {}", m_Title).c_str());
	if (m_Thread)
		SetDlgItemText(IDC_AFF_LABEL, L"The processors the thread may run on:");

	// one group: no combo box or note, and the check boxes take their place
	bool groups = m_Groups.size() > 1;
	GetDlgItem(IDC_AFF_AREA).GetWindowRect(&m_Area);
	ScreenToClient(&m_Area);
	int shift = 0;
	if (groups) {
		CComboBox combo(GetDlgItem(IDC_AFF_GROUP));
		for (size_t i = 0; i < m_Groups.size(); i++)
			combo.AddString(L"");
		SetDlgItemText(IDC_AFF_NOTE, m_Note.c_str());
		CRect note;
		GetDlgItem(IDC_AFF_NOTE).GetWindowRect(&note);
		ScreenToClient(&note);
		CRect gap(0, 0, 0, 4);
		MapDialogRect(&gap);
		shift = note.bottom + gap.bottom - m_Area.top;
	}
	else {
		for (UINT id : { IDC_AFF_GROUP_LABEL, IDC_AFF_GROUP, IDC_AFF_NOTE })
			GetDlgItem(id).ShowWindow(SW_HIDE);
	}
	m_Area.OffsetRect(0, shift);

	// the grid is sized for the largest group, so switching groups doesn't resize the dialog
	CRect cell(0, 0, 44, 13);
	MapDialogRect(&cell);
	m_Cell = cell.Size();
	int most = 0;
	for (auto& g : m_Groups)
		most = (std::max)(most, (int)std::popcount(g.Available));
	int columns = (std::min)(most, PerRow), rows = (most + PerRow - 1) / PerRow;
	int dx = (std::max)(0, (int)(m_Area.left + columns * m_Cell.cx - m_Area.right));
	// the template's area is where the grid was before the shift
	int dy = m_Area.top + rows * m_Cell.cy - (m_Area.bottom - shift);

	// the buttons move below the grid (OK and Cancel to the right), and the dialog takes the difference
	for (UINT id : { IDC_AFF_ALL, IDC_AFF_NONE, IDOK, IDCANCEL }) {
		CWindow button(GetDlgItem(id));
		CRect rc;
		button.GetWindowRect(&rc);
		ScreenToClient(&rc);
		rc.OffsetRect(id == IDOK || id == IDCANCEL ? dx : 0, dy);
		button.SetWindowPos(nullptr, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (groups && dx > 0) {
		// the combo box and the note widen with the grid
		for (UINT id : { IDC_AFF_GROUP, IDC_AFF_NOTE }) {
			CWindow win(GetDlgItem(id));
			CRect rc;
			win.GetWindowRect(&rc);
			ScreenToClient(&rc);
			win.SetWindowPos(nullptr, 0, 0, rc.Width() + dx, rc.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
	CRect rc;
	GetWindowRect(&rc);
	SetWindowPos(nullptr, 0, 0, rc.Width() + dx, rc.Height() + dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	// a check box for each bit any group has, hidden until a group shows it
	KAFFINITY bits = 0;
	for (auto& g : m_Groups)
		bits |= g.Available;
	auto font = GetFont();
	HWND after = GetDlgItem(IDC_AFF_ALL).GetWindow(GW_HWNDPREV);
	for (int bit = 0; bit < MaxBits; bit++) {
		if (!HasBit(bits, bit))
			continue;
		auto& check = m_Checks[bit];
		CRect rc(0, 0, m_Cell.cx - 4, m_Cell.cy - 2);
		check.Create(m_hWnd, rc, L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 0, FirstCheckId + bit);
		// before the buttons in the tab order
		check.SetWindowPos(after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		after = check;
		check.SetFont(font);
	}

	// the group with a selection first (a thread's), else the first
	auto it = std::ranges::find_if(m_Groups, [](auto& g) { return g.Selected != 0; });
	ShowGroup(it == m_Groups.end() ? 0 : (int)(it - m_Groups.begin()));
	UpdateCounts();
	CenterWindow(GetParent());
	return TRUE;
}

void CAffinityDlg::ShowGroup(int index) {
	if (m_Shown >= 0)
		m_Groups[m_Shown].Selected = ReadMask();
	m_Shown = index;
	auto& group = m_Groups[index];
	int count = 0;
	for (int bit = 0; bit < MaxBits; bit++) {
		auto& check = m_Checks[bit];
		if (!check)
			continue;
		if (!HasBit(group.Available, bit)) {
			check.ShowWindow(SW_HIDE);
			continue;
		}
		int x = m_Area.left + (count % PerRow) * m_Cell.cx, y = m_Area.top + (count / PerRow) * m_Cell.cy;
		check.SetWindowPos(nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		check.SetWindowText(std::format(L"CPU {}", group.FirstProcessor + bit).c_str());
		check.SetCheck(HasBit(group.Selected, bit) ? BST_CHECKED : BST_UNCHECKED);
		check.EnableWindow(group.Enabled);
		check.ShowWindow(SW_SHOWNA);
		count++;
	}
	GetDlgItem(IDC_AFF_ALL).EnableWindow(group.Enabled);
	GetDlgItem(IDC_AFF_NONE).EnableWindow(group.Enabled);
	if (m_Groups.size() > 1)
		CComboBox(GetDlgItem(IDC_AFF_GROUP)).SetCurSel(index);
}

KAFFINITY CAffinityDlg::ReadMask() const {
	if (m_Shown < 0)
		return 0;
	KAFFINITY mask = 0;
	auto& group = m_Groups[m_Shown];
	for (int bit = 0; bit < MaxBits; bit++)
		if (HasBit(group.Available, bit) && IsDlgButtonChecked(FirstCheckId + bit) == BST_CHECKED)
			mask |= (KAFFINITY)1 << bit;
	return mask;
}

void CAffinityDlg::KeepOneGroup() {
	if (!m_Thread || ReadMask() == 0)
		return;
	for (int i = 0; i < (int)m_Groups.size(); i++)
		if (i != m_Shown)
			m_Groups[i].Selected = 0;
}

void CAffinityDlg::UpdateCounts() {
	m_Groups[m_Shown].Selected = ReadMask();
	bool any = false;
	for (auto& g : m_Groups)
		any |= g.Selected != 0;
	GetDlgItem(IDOK).EnableWindow(any);
	if (m_Groups.size() < 2)
		return;
	CComboBox combo(GetDlgItem(IDC_AFF_GROUP));
	for (int i = 0; i < (int)m_Groups.size(); i++) {
		auto& g = m_Groups[i];
		auto text = std::format(L"Group {}: {} of {} processors", g.Number, std::popcount(g.Selected), std::popcount(g.Available));
		if (!g.Enabled)
			text += L" (not available)";
		combo.DeleteString(i);
		combo.InsertString(i, text.c_str());
	}
	combo.SetCurSel(m_Shown);
}

LRESULT CAffinityDlg::OnSelect(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the shown group's
	for (int bit = 0; bit < MaxBits; bit++)
		if (m_Checks[bit] && HasBit(m_Groups[m_Shown].Available, bit))
			m_Checks[bit].SetCheck(wID == IDC_AFF_ALL ? BST_CHECKED : BST_UNCHECKED);
	KeepOneGroup();
	UpdateCounts();
	return 0;
}

LRESULT CAffinityDlg::OnCheck(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	KeepOneGroup();
	UpdateCounts();
	return 0;
}

LRESULT CAffinityDlg::OnGroupChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int index = CComboBox(GetDlgItem(IDC_AFF_GROUP)).GetCurSel();
	if (index >= 0 && index != m_Shown) {
		ShowGroup(index);
		UpdateCounts();
	}
	return 0;
}

LRESULT CAffinityDlg::OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (wID == IDOK)
		m_Groups[m_Shown].Selected = ReadMask();
	EndDialog(wID);
	return 0;
}
