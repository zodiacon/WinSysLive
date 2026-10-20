#include "pch.h"
#include "WaitChainDlg.h"
#include <WinSys/Processes.h>
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>

using namespace WinSys;

CWaitChainDlg::CWaitChainDlg(uint32_t pid, std::wstring name, std::vector<uint32_t> tids) :
	m_Pid(pid), m_Name(std::move(name)), m_Tids(std::move(tids)) {
}

LRESULT CWaitChainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetWindowText((m_Tids.size() == 1 ? std::format(L"Wait Chain: Thread {} ({}, {})", m_Tids[0], m_Name, m_Pid)
		: std::format(L"Wait Chains: {} ({})", m_Name, m_Pid)).c_str());
	m_List.Attach(GetDlgItem(IDC_WAITCHAIN_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	// the dialog may run on a property sheet's thread: an image list of its own (also for the rows' indentation)
	m_Images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 4);
	for (auto id : { IDI_THREADS, IDI_OBJECT, IDI_MUTEX, IDI_ALPC })
		m_Images.AddIcon(AtlLoadIconImage(id, LR_DEFAULTCOLOR, 16, 16));
	m_List.SetImageList(m_Images, LVSIL_SMALL);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.InsertColumn(0, L"Thread or Object", LVCFMT_LEFT, width(420));
	m_List.InsertColumn(1, L"Status", LVCFMT_LEFT, width(110));
	m_List.InsertColumn(2, L"Wait Time", LVCFMT_RIGHT, width(90));
	m_List.InsertColumn(3, L"Context Switches", LVCFMT_RIGHT, width(110));

	DlgResize_Init(true, true);
	CenterWindow(GetParent());
	Read();
	return TRUE;
}

void CWaitChainDlg::Read() {
	GetDlgItem(IDC_WAITCHAIN_REFRESH).EnableWindow(FALSE);
	SetDlgItemText(IDC_WAITCHAIN_STATUS, L"Reading the wait chains...");
	if (m_Worker.joinable())
		m_Worker.join();
	m_Worker = std::jthread([this, hWnd = m_hWnd](std::stop_token stop) {
		std::vector<ThreadChain> chains;
		for (auto tid : m_Tids) {
			if (stop.stop_requested())
				return;
			ThreadChain chain{ tid, WaitChainTraversal::GetThreadWaitChain(tid) };
			if (!chain.Chain)
				chain.Error = ::GetLastError();
			chains.push_back(std::move(chain));
		}
		std::unordered_map<uint32_t, std::wstring> names;
		for (auto& [pid, name] : Process::EnumProcessNames())
			names[pid] = name;
		m_Chains = std::move(chains);
		m_ProcessNames = std::move(names);
		::PostMessage(hWnd, WM_WAITCHAINS_READ, 0, 0);
		});
}

std::wstring CWaitChainDlg::NodeText(WaitChainNode const& node) const {
	if (node.Type == WaitObjectType::Thread) {
		auto it = m_ProcessNames.find(node.ProcessId);
		auto name = it == m_ProcessNames.end() ? std::wstring(L"?") : it->second;
		// WCT knows only the process (PidOnly): out-of-process COM or a process wait
		if (node.ThreadId == 0)
			return std::format(L"Process {} ({})", name, node.ProcessId);
		return std::format(L"Thread {} ({}, {})", node.ThreadId, name, node.ProcessId);
	}
	std::wstring text = WaitChainTraversal::TypeToString(node.Type);
	if (!node.Name.empty())
		text += L": " + node.Name;
	if (node.Alertable)
		text += L" (alertable)";
	return text;
}

std::wstring CWaitChainDlg::FormatWaitTime(uint32_t ms) {
	if (ms < 1000)
		return std::format(L"{} ms", ms);
	auto seconds = ms / 1000;
	if (seconds < 60)
		return std::format(L"{}.{} s", seconds, ms % 1000 / 100);
	return std::format(L"{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

int CWaitChainDlg::NodeImage(WaitChainNode const& node) const {
	switch (node.Type) {
		case WaitObjectType::Thread: return 0;
		case WaitObjectType::Mutex: return 2;
		case WaitObjectType::Alpc: return 3;
	}
	return 1;
}

LRESULT CWaitChainDlg::OnChainsRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	GetDlgItem(IDC_WAITCHAIN_REFRESH).EnableWindow(TRUE);
	ShowChains();
	return 0;
}

void CWaitChainDlg::ShowChains() {
	// deadlocks first, then the chains through other threads, then the rest, each in thread order
	auto rank = [](ThreadChain const& c) {
		if (!c.Chain)
			return 3;
		if (c.Chain->Cycle)
			return 0;
		return std::ranges::count_if(c.Chain->Nodes, [](auto& n) { return n.Type == WaitObjectType::Thread; }) > 1 ? 1 : 2;
	};
	auto chains = m_Chains;
	std::ranges::stable_sort(chains, {}, rank);

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	m_DeadlockRows.clear();
	int deadlocks = 0, throughOthers = 0, unreadable = 0;
	auto addRow = [&](int indent, int image, std::wstring const& text, std::wstring const& status, std::wstring const& wait,
		std::wstring const& switches, bool deadlock) {
		LVITEM item{ LVIF_TEXT | LVIF_INDENT | LVIF_IMAGE };
		item.iItem = m_List.GetItemCount();
		item.iIndent = indent;
		item.iImage = image;
		item.pszText = const_cast<PWSTR>(text.c_str());
		int n = m_List.InsertItem(&item);
		m_List.SetItemText(n, 1, status.c_str());
		m_List.SetItemText(n, 2, wait.c_str());
		m_List.SetItemText(n, 3, switches.c_str());
		m_DeadlockRows.push_back(deadlock);
	};
	for (auto& c : chains) {
		if (!c.Chain) {
			unreadable++;
			addRow(0, 0, std::format(L"Thread {}", c.ThreadId), L"Error", L"", L"", false);
			addRow(1, 1, Helpers::GetErrorText(c.Error), L"", L"", L"", false);
			continue;
		}
		auto& chain = *c.Chain;
		if (chain.Cycle)
			deadlocks++;
		else if (rank(c) == 1)
			throughOthers++;
		for (int i = 0; i < (int)chain.Nodes.size(); i++) {
			auto& node = chain.Nodes[i];
			bool thread = node.Type == WaitObjectType::Thread;
			// a thread's wait time means something only while it waits
			bool waiting = thread && node.ThreadId && node.Status == WaitObjectStatus::Blocked;
			addRow(i, NodeImage(node), NodeText(node), WaitChainTraversal::StatusToString(node.Status),
				waiting ? FormatWaitTime(node.WaitTime) : L"", thread && node.ThreadId ? std::to_wstring(node.ContextSwitches) : L"",
				chain.Cycle);
		}
	}
	m_List.SetRedraw(TRUE);

	auto text = m_Tids.size() == 1 ? std::wstring() : std::format(L"{} threads", m_Tids.size());
	auto add = [&](std::wstring const& part) { text += (text.empty() ? L"" : L"; ") + part; };
	if (deadlocks)
		add(std::format(L"{} deadlocked", deadlocks));
	if (throughOthers)
		add(std::format(L"{} waiting on another thread", throughOthers));
	if (unreadable)
		add(std::format(L"{} couldn't be read", unreadable));
	if (!deadlocks && !throughOthers && !unreadable)
		add(L"no thread waits on another thread");
	SetDlgItemText(IDC_WAITCHAIN_STATUS, text.c_str());
	GetDlgItem(IDC_WAITCHAIN_COPY).EnableWindow(m_List.GetItemCount() > 0);
}

LRESULT CWaitChainDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (row < m_DeadlockRows.size() && m_DeadlockRows[row]) {
				// a deadlock, in the colors of the lists' exited rows
				bool dark = WTLHelper::IsDarkMode();
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CWaitChainDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Read();
	return 0;
}

LRESULT CWaitChainDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the rows, indented by their depth, the columns tab separated
	std::wstring text;
	CString cell;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		LVITEM item{ LVIF_INDENT };
		item.iItem = i;
		m_List.GetItem(&item);
		text += std::wstring(item.iIndent * 2, L' ');
		for (int col = 0; col < 4; col++) {
			m_List.GetItemText(i, col, cell);
			text += (col ? L"\t" : L"") + std::wstring(cell);
		}
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CWaitChainDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a read in progress stops after its current thread (the jthread joins it when the dialog goes)
	m_Worker.request_stop();
	EndDialog(wID);
	return 0;
}
