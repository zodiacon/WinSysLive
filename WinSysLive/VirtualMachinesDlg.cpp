#include "pch.h"
#include "VirtualMachinesDlg.h"
#include "VirtualMachineLookup.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

using namespace WinSys;

namespace {
	const UINT_PTR RefreshTimerId = 1;

	struct ColumnInfo {
		PCWSTR Name;
		int Width;	// at 96 DPI
		int Format;
	};
	const ColumnInfo s_Columns[] = {
		{ L"Name", 160, LVCFMT_LEFT },
		{ L"Kind", 175, LVCFMT_LEFT },
		{ L"State", 110, LVCFMT_LEFT },
		{ L"CPU (%)", 65, LVCFMT_RIGHT },
		{ L"Processors", 75, LVCFMT_RIGHT },
		{ L"Memory", 90, LVCFMT_RIGHT },
		{ L"Worker PID", 75, LVCFMT_RIGHT },
		{ L"Memory Process", 160, LVCFMT_LEFT },
		{ L"Started", 140, LVCFMT_LEFT },
		{ L"ID", 270, LVCFMT_LEFT },
	};

	bool StartsWith(std::wstring const& text, PCWSTR prefix) {
		return _wcsnicmp(text.c_str(), prefix, wcslen(prefix)) == 0;
	}
}

LRESULT CVirtualMachinesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_VIRTUALMACHINE, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_VIRTUALMACHINE, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_List.Attach(GetDlgItem(IDC_VM_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < (int)Column::_Count; i++)
		m_List.InsertColumn(i, s_Columns[i].Name, s_Columns[i].Format, ::MulDiv(s_Columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	// what the lookup has (the Processes tab's column may have read it), then a fresh read
	m_Read = VirtualMachineLookup::Get().Read;
	Load();
	VirtualMachineLookup::Refresh(m_hWnd, WM_VM_READ);
	SetTimer(RefreshTimerId, 2000);
	return TRUE;
}

void CVirtualMachinesDlg::Load() {
	auto result = VirtualMachineLookup::Get();
	m_Rows.clear();
	for (auto& s : result.Systems) {
		Row row;
		row.System = s;
		m_Rows.push_back(std::move(row));
	}
	// the processes of each, from the snapshot
	for (auto& p : m_ProcessManager.GetProcesses()) {
		auto& name = p->GetImageName();
		bool worker = _wcsicmp(name.c_str(), L"vmwp.exe") == 0, memory = StartsWith(name, L"vmmem");
		if (!worker && !memory)
			continue;
		auto id = VirtualMachineLookup::IdOf(m_ProcessManager, *p);
		auto it = std::ranges::find_if(m_Rows, [&](auto& r) { return r.System.Id == id; });
		if (id.empty() || it == m_Rows.end())
			continue;
		if (worker) {
			it->WorkerPid = p->Id;
			it->Started = p->Key.Created;
		}
		else {
			it->MemoryPid = p->Id;
			it->Cpu = p->CPU;
			it->Cells[(int)Column::MemoryProcess] = name + L" (" + std::to_wstring(p->Id) + L")";
		}
	}
	for (auto& r : m_Rows) {
		auto& s = r.System;
		r.Cells[(int)Column::Name] = VirtualMachines::DisplayName(s);
		r.Cells[(int)Column::Kind] = VirtualMachines::KindText(s);
		r.Cells[(int)Column::State] = VirtualMachines::StateText(s);
		r.Cells[(int)Column::Cpu] = r.MemoryPid ? (r.Cpu > 0 ? FormatHelper::FormatCpu(r.Cpu) : L"0") : L"";
		r.Cells[(int)Column::Processors] = s.ProcessorCount ? std::to_wstring(s.ProcessorCount) : L"";
		r.Cells[(int)Column::Memory] = s.AssignedMemory ? FormatHelper::FormatSize((int64_t)s.AssignedMemory) : L"";
		r.Cells[(int)Column::Worker] = r.WorkerPid ? std::to_wstring(r.WorkerPid) : L"";
		r.Cells[(int)Column::Started] = r.Started ? FormatHelper::FormatDateTime(r.Started) : L"";
		r.Cells[(int)Column::Id] = s.Id;
	}
	Fill();

	std::wstring status;
	if (!m_Read)
		status = L"Reading...";
	else if (result.Error)
		status = L"The virtual machines can't be read: " + Helpers::GetErrorText(result.Error)
			+ (SecurityHelper::IsRunningElevated() ? L"" : L" (Hyper-V's compute service and WMI need administrator rights)");
	else {
		auto running = std::ranges::count_if(m_Rows, [](auto& r) { return r.WorkerPid != 0; });
		status = std::format(L"{} virtual machines and containers, {} with a worker process; CPU: the memory process's, where "
			L"Windows counts the virtual processors' time; read every 2 seconds", m_Rows.size(), running);
	}
	SetDlgItemText(IDC_VM_STATUS, status.c_str());
}

void CVirtualMachinesDlg::Fill() {
	// the selection, by ID
	std::vector<std::wstring> selected;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED)) {
		CString id;
		m_List.GetItemText(i, (int)Column::Id, id);
		selected.emplace_back((PCWSTR)id);
	}
	std::vector<int> order((int)m_Rows.size());
	for (int i = 0; i < (int)order.size(); i++)
		order[i] = i;
	if (m_SortColumn >= 0) {
		auto column = (Column)m_SortColumn;
		auto compare = [](auto a, auto b) { return a < b ? -1 : a > b ? 1 : 0; };
		std::ranges::stable_sort(order, [&](int ia, int ib) {
			auto& a = m_Rows[ia];
			auto& b = m_Rows[ib];
			int result;
			switch (column) {
				case Column::Cpu: result = compare(a.Cpu, b.Cpu); break;
				case Column::Processors: result = compare(a.System.ProcessorCount, b.System.ProcessorCount); break;
				case Column::Memory: result = compare(a.System.AssignedMemory, b.System.AssignedMemory); break;
				case Column::Worker: result = compare(a.WorkerPid, b.WorkerPid); break;
				case Column::Started: result = compare(a.Started, b.Started); break;
				default: result = ::_wcsicmp(a.Cells[m_SortColumn].c_str(), b.Cells[m_SortColumn].c_str()); break;
			}
			return m_SortAscending ? result < 0 : result > 0;
			});
	}
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (int i = 0; i < (int)order.size(); i++) {
		auto& row = m_Rows[order[i]];
		m_List.InsertItem(i, row.Cells[0].c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, row.Cells[c].c_str());
		m_List.SetItemData(i, order[i]);
		if (std::ranges::find(selected, row.System.Id) != selected.end())
			m_List.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate();
}

CVirtualMachinesDlg::Row const* CVirtualMachinesDlg::SelectedRow() const {
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(selected);
	return index < m_Rows.size() ? &m_Rows[index] : nullptr;
}

LRESULT CVirtualMachinesDlg::OnTimer(UINT /*uMsg*/, WPARAM id, LPARAM /*lParam*/, BOOL& bHandled) {
	if (id != RefreshTimerId) {
		bHandled = FALSE;
		return 0;
	}
	// the CPU and processes from the snapshot now; the systems when the read ends
	Load();
	VirtualMachineLookup::Refresh(m_hWnd, WM_VM_READ);
	return 0;
}

LRESULT CVirtualMachinesDlg::OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Read = true;
	Load();
	return 0;
}

LRESULT CVirtualMachinesDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	VirtualMachineLookup::Refresh(m_hWnd, WM_VM_READ);
	return 0;
}

LRESULT CVirtualMachinesDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = column;
		m_SortAscending = true;
	}
	Fill();
	return 0;
}

LRESULT CVirtualMachinesDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	auto row = SelectedRow();
	if (!row)
		return 0;
	BOOL handled;
	return OnGoToProcess(0, row->WorkerPid ? ID_VM_GOTOWORKER : ID_VM_GOTOMEMORY, nullptr, handled);
}

LRESULT CVirtualMachinesDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
		if (selected >= 0)
			m_List.GetItemRect(selected, &rc, LVIR_LABEL);
		else
			m_List.GetClientRect(&rc);
		pt = selected >= 0 ? rc.CenterPoint() : rc.TopLeft();
		m_List.ClientToScreen(&pt);
	}
	auto row = SelectedRow();
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (row && row->WorkerPid ? 0 : MF_GRAYED), ID_VM_GOTOWORKER, L"Go to &Worker Process");
	menu.AppendMenu(MF_STRING | (row && row->MemoryPid ? 0 : MF_GRAYED), ID_VM_GOTOMEMORY, L"Go to &Memory Process");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (m_List.GetItemCount() ? 0 : MF_GRAYED), IDC_VM_COPY, m_List.GetSelectedCount() ? L"&Copy" : L"&Copy All");
	menu.SetMenuDefaultItem(row && !row->WorkerPid ? ID_VM_GOTOMEMORY : ID_VM_GOTOWORKER);
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CVirtualMachinesDlg::OnGoToProcess(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = SelectedRow();
	auto pid = !row ? 0 : wID == ID_VM_GOTOMEMORY ? row->MemoryPid : row->WorkerPid;
	if (!pid || !m_GoToProcess)
		return 0;
	KillTimer(RefreshTimerId);
	EndDialog(IDOK);
	m_GoToProcess(pid);
	return 0;
}

LRESULT CVirtualMachinesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	for (int c = 0; c < (int)Column::_Count; c++)
		text += (c ? L"\t" : L"") + std::wstring(s_Columns[c].Name);
	text += L"\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& row = m_Rows[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + row.Cells[c];
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CVirtualMachinesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	KillTimer(RefreshTimerId);
	EndDialog(wID);
	return 0;
}
