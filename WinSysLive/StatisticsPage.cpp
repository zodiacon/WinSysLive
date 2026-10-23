#include "pch.h"
#include "StatisticsPage.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ClipboardHelper.h>

namespace {
	// the snapshot's threads are SYSTEM_EXTENDED_THREAD_INFORMATION
	int64_t SumContextSwitches(SYSTEM_PROCESS_INFORMATION const& info) {
		auto threads = reinterpret_cast<SYSTEM_EXTENDED_THREAD_INFORMATION const*>(info.Threads);
		int64_t total = 0;
		for (ULONG i = 0; i < info.NumberOfThreads; i++)
			total += threads[i].ThreadInfo.ContextSwitches;
		return total;
	}
}

CProcStatsPage::CProcStatsPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Props(props), m_Snapshot(snapshot) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_CHART);
}

LRESULT CProcStatsPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_STATS_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);
	// at 96 DPI: the column manager scales them
	cm->AddColumn(L"Counter", LVCFMT_LEFT, 200);
	cm->AddColumn(L"Value", LVCFMT_RIGHT, 150);
	cm->AddColumn(L"Change", LVCFMT_RIGHT, 120);

	// one handle for the queried values, as the page asks every sample
	if (auto hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_LIMITED_INFORMATION))
		m_Process = std::make_unique<WinSys::Process>(hProcess.release());
	SetDlgItemText(IDC_STATS_STATUS, std::format(L"Change: since the previous sample (every {} ms)", m_Props.UpdateInterval).c_str());

	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcStatsPage::OnSetActive() {
	// the sheet's last snapshot; the changes start over, as the sheet feeds the page only while it's active
	m_Fresh = true;
	OnSample(m_Snapshot);
	return 0;
}

void CProcStatsPage::OnSample(ProcessSnapshot const& snapshot) {
	auto info = snapshot.Process();
	if (!info) {
		// once the process is gone, its last values stay
		if (snapshot.Exited())
			SetDlgItemText(IDC_STATS_STATUS, L"The process has exited: its last values");
		return;
	}
	auto rows = Collect(*info);
	if (!m_Fresh && rows.size() == m_Rows.size())
		for (size_t i = 0; i < rows.size(); i++)
			if (rows[i].Kind != Kind::Header && rows[i].Kind != Kind::Text && rows[i].Known && m_Rows[i].Known)
				rows[i].Change = rows[i].Value - m_Rows[i].Value;
	m_Fresh = false;
	m_Rows = std::move(rows);
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	m_List.Invalidate();
}

std::vector<CProcStatsPage::Row> CProcStatsPage::Collect(SYSTEM_PROCESS_INFORMATION const& info) const {
	std::vector<Row> rows;
	auto header = [&](PCWSTR name) { rows.push_back({ name, Kind::Header }); };
	auto add = [&](PCWSTR name, Kind kind, int64_t value) { rows.push_back({ name, kind, value }); };
	auto text = [&](PCWSTR name, std::wstring value) { rows.push_back({ name, Kind::Text, 0, std::move(value) }); };
	// a value queried through the handle; unknown without one
	auto queried = [&](PCWSTR name, std::optional<int64_t> value) {
		rows.push_back({ name, Kind::Number, value.value_or(0), {}, {}, value.has_value() });
	};
	auto process = m_Process.get();

	header(L"CPU");
	text(L"Priority", FormatHelper::FormatPriority(info.BasePriority));
	add(L"Kernel time", Kind::Time, info.KernelTime.QuadPart);
	add(L"User time", Kind::Time, info.UserTime.QuadPart);
	add(L"Total time", Kind::Time, info.KernelTime.QuadPart + info.UserTime.QuadPart);
	add(L"Cycles", Kind::Number, (int64_t)info.CycleTime);
	add(L"Context switches", Kind::Number, SumContextSwitches(info));
	add(L"Threads", Kind::Number, info.NumberOfThreads);
	add(L"Peak threads", Kind::Number, info.NumberOfThreadsHighWatermark);

	header(L"Memory");
	add(L"Private bytes", Kind::Bytes, (int64_t)info.PrivatePageCount);
	add(L"Peak private bytes", Kind::Bytes, (int64_t)info.PeakPagefileUsage);
	add(L"Virtual size", Kind::Bytes, (int64_t)info.VirtualSize);
	add(L"Peak virtual size", Kind::Bytes, (int64_t)info.PeakVirtualSize);
	add(L"Working set", Kind::Bytes, (int64_t)info.WorkingSetSize);
	add(L"Peak working set", Kind::Bytes, (int64_t)info.PeakWorkingSetSize);
	add(L"Page faults", Kind::Number, info.PageFaultCount);
	add(L"Paged pool", Kind::Bytes, (int64_t)info.QuotaPagedPoolUsage);
	add(L"Peak paged pool", Kind::Bytes, (int64_t)info.QuotaPeakPagedPoolUsage);
	add(L"Non-paged pool", Kind::Bytes, (int64_t)info.QuotaNonPagedPoolUsage);
	add(L"Peak non-paged pool", Kind::Bytes, (int64_t)info.QuotaPeakNonPagedPoolUsage);
	if (process) {
		auto priority = process->GetMemoryPriority();
		text(L"Memory priority", priority >= 0 ? std::to_wstring(priority) : L"");
	}

	header(L"I/O");
	add(L"Reads", Kind::Number, info.ReadOperationCount.QuadPart);
	add(L"Read bytes", Kind::Bytes, info.ReadTransferCount.QuadPart);
	add(L"Writes", Kind::Number, info.WriteOperationCount.QuadPart);
	add(L"Write bytes", Kind::Bytes, info.WriteTransferCount.QuadPart);
	add(L"Other", Kind::Number, info.OtherOperationCount.QuadPart);
	add(L"Other bytes", Kind::Bytes, info.OtherTransferCount.QuadPart);
	if (process)
		text(L"I/O priority", FormatHelper::IoPriorityToString(process->GetIoPriority()));

	header(L"Handles and Objects");
	add(L"Handles", Kind::Number, info.HandleCount);
	queried(L"Peak handles", process ? process->GetPeakHandleCount() : std::nullopt);
	// GetGuiResources gives 0 for a process it can't query, as for one without GUI objects
	auto gui = [&](auto get) { return process ? std::optional<int64_t>((process->*get)()) : std::nullopt; };
	queried(L"GDI objects", gui(&WinSys::Process::GetGdiObjectCount));
	queried(L"Peak GDI objects", gui(&WinSys::Process::GetPeakGdiObjectCount));
	queried(L"USER objects", gui(&WinSys::Process::GetUserObjectCount));
	queried(L"Peak USER objects", gui(&WinSys::Process::GetPeakUserObjectCount));
	return rows;
}

std::wstring CProcStatsPage::FormatValue(Kind kind, int64_t value) {
	switch (kind) {
		case Kind::Bytes: {
			// FormatSize is in KB, which hides changes under one: those in bytes
			if (value > -1024 && value < 1024)
				return FormatHelper::FormatNumber(value) + L" B";
			return FormatHelper::FormatSize(value);
		}
		case Kind::Time: return FormatHelper::FormatTimeSpan(value);
	}
	return FormatHelper::FormatNumber(value);
}

CString CProcStatsPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	auto& r = m_Rows[row];
	switch (col) {
		case 0: return r.Name;
		case 1:
			if (r.Kind == Kind::Header)
				return L"";
			if (r.Kind == Kind::Text)
				return r.Text.c_str();
			return r.Known ? FormatValue(r.Kind, r.Value).c_str() : L"";
		case 2:
			if (!r.Change || *r.Change == 0)
				return L"";
			// a time's change as a time, so negative ones (none expected) as a number
			if (r.Kind == Kind::Time && *r.Change > 0)
				return (L"+" + FormatHelper::FormatTimeSpan(*r.Change)).c_str();
			return ((*r.Change > 0 ? L"+" : L"") + FormatValue(r.Kind == Kind::Time ? Kind::Number : r.Kind, *r.Change)).c_str();
	}
	return L"";
}

LRESULT CProcStatsPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// the sections' titles bold and shaded, as the handle sheet's General page has them
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			// the list's font changes with the DPI: the bold one follows
			if (auto font = m_List.GetFont(); font != m_BoldFrom) {
				m_BoldFrom = font;
				LOGFONT lf;
				CFontHandle(font).GetLogFont(&lf);
				lf.lfWeight = FW_BOLD;
				if (!m_BoldFont.IsNull())
					m_BoldFont.DeleteObject();
				m_BoldFont.CreateFontIndirect(&lf);
			}
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (row < m_Rows.size() && m_Rows[row].Kind == Kind::Header) {
				bool dark = WTLHelper::IsDarkMode();
				cd->clrTextBk = dark ? RGB(60, 64, 72) : RGB(225, 232, 242);
				::SelectObject(cd->nmcd.hdc, m_BoldFont);
				return CDRF_NEWFONT;
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CProcStatsPage::OnCopy(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto line = [&](int row) {
		auto& r = m_Rows[row];
		if (r.Kind == Kind::Header)
			return L"[" + std::wstring(r.Name) + L"]";
		std::wstring text(GetColumnText(m_List, row, 0));
		for (int col = 1; col <= 2; col++)
			text += L"\t" + std::wstring(GetColumnText(m_List, row, col));
		return text;
	};
	std::wstring text;
	if (wID == IDC_STATS_COPYALL) {
		for (int row = 0; row < (int)m_Rows.size(); row++)
			text += line(row) + L"\r\n";
	}
	else if (int row = m_List.GetSelectedIndex(); row >= 0 && row < (int)m_Rows.size())
		text = line(row);
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
