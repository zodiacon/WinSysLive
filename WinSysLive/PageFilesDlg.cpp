#include "pch.h"
#include "PageFilesDlg.h"
#include "FormatHelper.h"
#include <WinSys/SystemInfo.h>
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	// FormatBytes leaves zero empty
	std::wstring Size(uint64_t bytes) {
		return bytes ? FormatHelper::FormatBytes(bytes) : L"0 B";
	}

	std::wstring Percent(uint64_t part, uint64_t whole) {
		return whole ? std::format(L"{:.1f}%", 100.0 * part / whole) : L"";
	}

	PCWSTR YesNo(bool value) {
		return value ? L"Yes" : L"No";
	}
}

LRESULT CPageFilesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_PF_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Page File", 200, LVCFMT_LEFT }, { L"Size", 90, LVCFMT_RIGHT }, { L"In Use", 90, LVCFMT_RIGHT },
		{ L"Usage", 60, LVCFMT_RIGHT }, { L"Peak", 90, LVCFMT_RIGHT }, { L"Minimum", 90, LVCFMT_RIGHT },
		{ L"Maximum", 90, LVCFMT_RIGHT },
	};
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	m_Details.Attach(GetDlgItem(IDC_PF_DETAILS));
	m_Details.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	m_Details.InsertColumn(0, L"Property", LVCFMT_LEFT, width(200));
	m_Details.InsertColumn(1, L"Value", LVCFMT_LEFT, width(480));
	ListViewHelper::EnableColumnDpiScaling(m_Details);

	// the sections' titles
	LOGFONT lf;
	CFontHandle(m_Details.GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&lf);

	m_CopyList = m_List;
	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CPageFilesDlg::Load() {
	CWaitCursor wait;
	auto files = WinSys::PageFiles::Enum();
	m_List.DeleteAllItems();
	uint64_t size = 0, inUse = 0;
	for (int i = 0; i < (int)files.size(); i++) {
		auto& f = files[i];
		m_List.InsertItem(i, f.Name.c_str());
		m_List.SetItemText(i, 1, Size(f.Size).c_str());
		m_List.SetItemText(i, 2, Size(f.InUse).c_str());
		m_List.SetItemText(i, 3, Percent(f.InUse, f.Size).c_str());
		m_List.SetItemText(i, 4, Size(f.Peak).c_str());
		m_List.SetItemText(i, 5, f.Minimum ? Size(f.Minimum).c_str() : L"");
		m_List.SetItemText(i, 6, f.Maximum ? Size(f.Maximum).c_str() : L"");
		size += f.Size;
		inUse += f.InUse;
	}

	m_Rows.clear();
	auto section = [&](PCWSTR title) { m_Rows.push_back({ title, L"", true }); };
	auto row = [&](std::wstring name, std::wstring value) { m_Rows.push_back({ std::move(name), std::move(value) }); };

	// what the page files are for: the commit limit is physical memory and the page files' current sizes
	section(L"Commit");
	if (auto perf = WinSys::SystemInfo::GetPerformanceInformation()) {
		SYSTEM_INFO si;
		::GetSystemInfo(&si);
		uint64_t page = si.dwPageSize;
		row(L"Commit charge", std::format(L"{} ({} of the limit)", Size(perf->CommittedPages * page),
			Percent(perf->CommittedPages, perf->CommitLimit)));
		row(L"Commit limit", Size(perf->CommitLimit * page));
		row(L"Peak commit charge", Size(perf->PeakCommitment * page));
	}
	MEMORYSTATUSEX ms{ sizeof(ms) };
	if (::GlobalMemoryStatusEx(&ms))
		row(L"Physical memory", Size(ms.ullTotalPhys));
	row(L"Page file space in use", files.empty() ? L"(no page file)" : std::format(L"{} of {} ({})", Size(inUse), Size(size), Percent(inUse, size)));

	// the note beside the title: the first column is narrow
	m_Rows.push_back({ L"Configuration", L"Changes apply at the next boot", true });
	if (auto config = WinSys::PageFiles::GetConfiguration()) {
		if (config->Files.empty())
			row(L"Page files", L"None");
		else if (config->Automatic)
			row(L"Page files", L"Managed automatically for all drives");
		else
			for (auto& f : config->Files)
				row(f.Path, f.SystemManaged ? std::wstring(L"System managed size")
					: std::format(L"Initial {} MB, maximum {} MB", FormatHelper::FormatNumber(f.InitialMB), FormatHelper::FormatNumber(f.MaximumMB)));
		std::wstring existing;
		for (auto& name : config->Existing)
			existing += (existing.empty() ? L"" : L", ") + name;
		row(L"Created at boot", existing.empty() ? L"(none)" : existing);
		row(L"Cleared at shutdown", YesNo(config->ClearAtShutdown));
		row(L"Kernel and drivers paged", config->DisablePagingExecutive ? L"No (DisablePagingExecutive)" : L"Yes");
	}
	else
		row(L"Configuration", WinSys::Helpers::GetErrorText(::GetLastError()));

	// the swap file (packaged apps' working sets are swapped out to it as a whole), on the system drive
	section(L"Swap File");
	WCHAR drive[MAX_PATH];
	if (::GetEnvironmentVariable(L"SystemDrive", drive, _countof(drive))) {
		auto path = std::wstring(drive) + L"\\swapfile.sys";
		WIN32_FIND_DATA fd;
		if (auto hFind = ::FindFirstFile(path.c_str(), &fd); hFind != INVALID_HANDLE_VALUE) {
			::FindClose(hFind);
			row(path, Size(((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow));
		}
		else
			row(path, L"(none)");
	}

	m_Details.DeleteAllItems();
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		m_Details.InsertItem(i, m_Rows[i].Name.c_str());
		m_Details.SetItemText(i, 1, m_Rows[i].Value.c_str());
	}
	SetDlgItemText(IDC_PF_STATUS, files.empty() ? L"No page file in use" : std::format(L"{} page file{} in use", files.size(),
		files.size() == 1 ? L"" : L"s").c_str());
}

LRESULT CPageFilesDlg::OnDetailsCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	// the sections' titles bold and shaded, as the handle sheet's General page has them
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (row < m_Rows.size() && m_Rows[row].Header) {
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

LRESULT CPageFilesDlg::OnListFocus(int /*idCtrl*/, LPNMHDR hdr, BOOL& bHandled) {
	if (hdr->hwndFrom == m_List || hdr->hwndFrom == m_Details)
		m_CopyList = hdr->hwndFrom;
	bHandled = FALSE;
	return 0;
}

LRESULT CPageFilesDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

std::wstring CPageFilesDlg::ListText(CListViewCtrl& list) {
	std::wstring text;
	auto header = list.GetHeader();
	int columns = header.GetItemCount();
	WCHAR buffer[512];
	for (int c = 0; c < columns; c++) {
		LVCOLUMN col{ LVCF_TEXT };
		col.pszText = buffer;
		col.cchTextMax = _countof(buffer);
		list.GetColumn(c, &col);
		text += (c ? L"\t" : L"") + std::wstring(buffer);
	}
	text += L"\r\n";
	for (int i = 0; i < list.GetItemCount(); i++) {
		for (int c = 0; c < columns; c++) {
			list.GetItemText(i, c, buffer, _countof(buffer));
			text += (c ? L"\t" : L"") + std::wstring(buffer);
		}
		text += L"\r\n";
	}
	return text;
}

LRESULT CPageFilesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	if (m_CopyList == m_Details) {
		// the sections as [titles], the properties tab separated
		for (auto& r : m_Rows)
			text += r.Header ? L"[" + r.Name + L"]\r\n" : r.Name + L"\t" + r.Value + L"\r\n";
	}
	else
		text = ListText(m_List);
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CPageFilesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
