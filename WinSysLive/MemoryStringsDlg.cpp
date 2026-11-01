#include "pch.h"
#include "MemoryStringsDlg.h"
#include "FormatHelper.h"
#include "ListExport.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

namespace {
	// the scan's progress, as percent changes, and Stop
	struct Progress : WinSys::StringScanProgress {
		Progress(HWND hWnd, std::stop_token stop) : m_hWnd(hWnd), m_Stop(stop) {}
		bool OnProgress(uint64_t scanned, uint64_t total) override {
			int percent = total ? int(scanned * 100 / total) : 100;
			if (percent != m_Percent) {
				m_Percent = percent;
				::PostMessage(m_hWnd, CMemoryStringsDlg::WM_STRINGS_PROGRESS, percent, 0);
			}
			return !m_Stop.stop_requested();
		}
		HWND m_hWnd;
		std::stop_token m_Stop;
		int m_Percent{ -1 };
	};
}

LRESULT CMemoryStringsDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_List.Attach(GetDlgItem(IDC_MSTR_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Address", 130, LVCFMT_RIGHT }, { L"Kind", 60, LVCFMT_LEFT }, { L"Length", 60, LVCFMT_RIGHT },
		{ L"Region", 200, LVCFMT_LEFT }, { L"String", 480, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, ::MulDiv(columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	SetDlgItemInt(IDC_MSTR_MINLEN, m_State.Options.MinLength, FALSE);
	CheckDlgButton(IDC_MSTR_ASCII, m_State.Options.Ascii ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_MSTR_UNICODE, m_State.Options.Unicode ? BST_CHECKED : BST_UNCHECKED);
	bool whole = m_State.WholeSpace || !m_Region;
	CheckRadioButton(IDC_MSTR_REGION, IDC_MSTR_WHOLE, whole ? IDC_MSTR_WHOLE : IDC_MSTR_REGION);
	GetDlgItem(IDC_MSTR_REGION).EnableWindow(m_Region.has_value());
	::SendDlgItemMessage(m_hWnd, IDC_MSTR_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by text or address");
	SetDlgItemText(IDC_MSTR_FILTER, m_State.Filter.c_str());

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	ShowRows();
	EnableControls();
	// the first time, scan at once with the defaults
	if (m_State.Scope.empty())
		StartScan();
	else
		UpdateStatus();
	return TRUE;
}

LRESULT CMemoryStringsDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	// the thread reads with the page's handle and writes our members: it's stopped (at its next chunk) and joined
	if (m_Worker.joinable()) {
		m_Worker.request_stop();
		m_Worker.join();
	}
	bHandled = FALSE;
	return 0;
}

bool CMemoryStringsDlg::ReadOptions() {
	BOOL ok = FALSE;
	auto min = GetDlgItemInt(IDC_MSTR_MINLEN, &ok, FALSE);
	if (!ok || min < 2 || min > 1000) {
		AtlMessageBox(m_hWnd, L"The minimum length is 2 to 1000 characters.", IDR_MAINFRAME, MB_ICONWARNING);
		GetDlgItem(IDC_MSTR_MINLEN).SetFocus();
		return false;
	}
	bool ascii = IsDlgButtonChecked(IDC_MSTR_ASCII) == BST_CHECKED;
	bool unicode = IsDlgButtonChecked(IDC_MSTR_UNICODE) == BST_CHECKED;
	if (!ascii && !unicode) {
		AtlMessageBox(m_hWnd, L"Choose ASCII, Unicode or both.", IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}
	m_State.Options.MinLength = min;
	m_State.Options.Ascii = ascii;
	m_State.Options.Unicode = unicode;
	m_State.WholeSpace = IsDlgButtonChecked(IDC_MSTR_WHOLE) == BST_CHECKED || !m_Region;
	return true;
}

void CMemoryStringsDlg::StartScan() {
	if (m_Scanning || !ReadOptions())
		return;
	uint64_t from = 0, to = WinSys::ProcessMemory::MaxAddress();
	if (!m_State.WholeSpace) {
		from = m_Region->first;
		to = m_Region->second;
		m_State.Scope = std::format(L"the region at 0x{:X}", from);
	}
	else
		m_State.Scope = L"the address space";
	m_Scanning = true;
	m_Stopped = false;
	m_Percent = 0;
	EnableControls();
	UpdateStatus();
	m_Worker = std::jthread([this, hWnd = m_hWnd, from, to, options = m_State.Options](std::stop_token stop) {
		Progress progress(hWnd, stop);
		bool truncated = false;
		auto strings = WinSys::ProcessMemory::FindStrings(m_hProcess, from, to, options, &progress, &truncated);
		{
			std::lock_guard lock(m_ResultLock);
			m_Result = std::move(strings);
			m_ResultTruncated = truncated;
		}
		::PostMessage(hWnd, WM_STRINGS_DONE, 0, 0);
		});
}

void CMemoryStringsDlg::StopScan() {
	if (m_Scanning) {
		m_Stopped = true;
		m_Worker.request_stop();
	}
}

LRESULT CMemoryStringsDlg::OnProgress(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_Scanning) {
		m_Percent = (int)wParam;
		UpdateStatus();
	}
	return 0;
}

LRESULT CMemoryStringsDlg::OnDone(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_Worker.joinable())
		m_Worker.join();
	m_Scanning = false;
	{
		std::lock_guard lock(m_ResultLock);
		m_State.Results = std::move(m_Result);
		m_State.Truncated = m_ResultTruncated;
	}
	ShowRows();
	EnableControls();
	UpdateStatus();
	return 0;
}

void CMemoryStringsDlg::EnableControls() {
	SetDlgItemText(IDC_MSTR_SCAN, m_Scanning ? L"&Stop" : L"&Scan");
	for (auto id : { IDC_MSTR_MINLEN, IDC_MSTR_ASCII, IDC_MSTR_UNICODE, IDC_MSTR_WHOLE })
		GetDlgItem(id).EnableWindow(!m_Scanning);
	GetDlgItem(IDC_MSTR_REGION).EnableWindow(!m_Scanning && m_Region.has_value());
	bool selected = m_List.GetSelectedCount() > 0;
	GetDlgItem(IDC_MSTR_GOTO).EnableWindow(selected);
	GetDlgItem(IDC_MSTR_COPY).EnableWindow(!m_Shown.empty());
	GetDlgItem(IDC_MSTR_SAVE).EnableWindow(!m_Shown.empty());
}

void CMemoryStringsDlg::UpdateStatus() {
	std::wstring text;
	if (m_Scanning)
		text = std::format(L"Scanning {}... {}%", m_State.Scope, m_Percent);
	else if (!m_State.Scope.empty()) {
		size_t wide = std::ranges::count_if(m_State.Results, [](auto& s) { return s.Wide; });
		text = std::format(L"{} strings in {} ({} ASCII, {} Unicode)", FormatHelper::FormatNumber((int64_t)m_State.Results.size()),
			m_State.Scope, FormatHelper::FormatNumber(int64_t(m_State.Results.size() - wide)), FormatHelper::FormatNumber((int64_t)wide));
		if (m_State.Truncated)
			text += m_Stopped || m_State.Results.size() < m_State.Options.MaxResults ? L"; stopped before the end"
				: L"; the limit was reached: scan a region, or raise the minimum length";
		if (!m_State.Filter.empty())
			text += std::format(L"; {} match the filter", FormatHelper::FormatNumber((int64_t)m_Shown.size()));
	}
	SetDlgItemText(IDC_MSTR_STATUS, text.c_str());
}

std::wstring CMemoryStringsDlg::GetText(WinSys::MemoryString const& s, Column column) const {
	switch (column) {
		case Column::Address: return std::format(L"0x{:X}", s.Address);
		case Column::Kind: return s.Wide ? L"Unicode" : L"ASCII";
		case Column::Length: return std::to_wstring(s.Length);
		case Column::Region: return m_Describe ? m_Describe(s.Address) : L"";
		case Column::String: return s.Length > s.Text.size() ? s.Text + L"..." : s.Text;
	}
	return L"";
}

void CMemoryStringsDlg::ShowRows() {
	auto& results = m_State.Results;
	m_Shown.clear();
	m_Shown.reserve(results.size());
	auto& filter = m_State.Filter;	// lowercase
	for (int i = 0; i < (int)results.size(); i++) {
		if (!filter.empty()) {
			auto text = results[i].Text;
			::CharLowerBuff(text.data(), (DWORD)text.size());
			if (text.find(filter) == std::wstring::npos) {
				auto address = std::format(L"{:x}", results[i].Address);
				auto bare = filter.starts_with(L"0x") ? filter.substr(2) : filter;
				if (bare.empty() || address.find(bare) == std::wstring::npos)
					continue;
			}
		}
		m_Shown.push_back(i);
	}
	auto column = (Column)m_SortColumn;
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : b < a ? 1 : 0; };
	std::ranges::stable_sort(m_Shown, [&](int i1, int i2) {
		auto& a = results[i1];
		auto& b = results[i2];
		int result;
		switch (column) {
			case Column::Kind: result = compare(a.Wide, b.Wide); break;
			case Column::Length: result = compare(a.Length, b.Length); break;
			case Column::Region: result = ::_wcsicmp(GetText(a, column).c_str(), GetText(b, column).c_str()); break;
			case Column::String: result = ::_wcsicmp(a.Text.c_str(), b.Text.c_str()); break;
			default: result = 0; break;
		}
		if (result == 0)
			result = compare(a.Address, b.Address);
		return m_SortAscending ? result < 0 : result > 0;
		});
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.SetItemCountEx((int)m_Shown.size(), 0);
	m_List.Invalidate();
	EnableControls();
}

LRESULT CMemoryStringsDlg::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMLVDISPINFO*>(hdr);
	auto& item = di->item;
	if ((item.mask & LVIF_TEXT) && item.iItem < (int)m_Shown.size()) {
		m_Text = GetText(m_State.Results[m_Shown[item.iItem]], (Column)item.iSubItem);
		item.pszText = m_Text.data();
	}
	return 0;
}

LRESULT CMemoryStringsDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		m_SortAscending = true;
	}
	ShowRows();
	return 0;
}

LRESULT CMemoryStringsDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	GetDlgItem(IDC_MSTR_GOTO).EnableWindow(m_List.GetSelectedCount() > 0);
	return 0;
}

LRESULT CMemoryStringsDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*hdr*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnGoTo(0, 0, nullptr, handled);
}

LRESULT CMemoryStringsDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_MSTR_FILTER, text);
	m_State.Filter = (PCWSTR)text;
	::CharLowerBuff(m_State.Filter.data(), (DWORD)m_State.Filter.size());
	ShowRows();
	UpdateStatus();
	return 0;
}

LRESULT CMemoryStringsDlg::OnScan(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_Scanning)
		StopScan();
	else
		StartScan();
	return 0;
}

LRESULT CMemoryStringsDlg::OnGoTo(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// Enter (IDOK): in the minimum length it scans, on the list it goes to the string, elsewhere nothing
	if (wID == IDOK) {
		auto focus = GetFocus();
		if (focus == GetDlgItem(IDC_MSTR_MINLEN)) {
			StartScan();
			return 0;
		}
		if (focus != m_List)
			return 0;
	}
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || selected >= (int)m_Shown.size())
		return 0;
	auto& s = m_State.Results[m_Shown[selected]];
	m_GoToAddress = s.Address;
	m_GoToLength = (size_t)s.Length * (s.Wide ? 2 : 1);
	EndDialog(IDOK);
	return 0;
}

std::wstring CMemoryStringsDlg::RowsText(bool selectedOnly) const {
	std::wstring text = L"Address\tKind\tLength\tRegion\tString\r\n";
	for (int i = 0; i < (int)m_Shown.size(); i++) {
		if (selectedOnly && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& s = m_State.Results[m_Shown[i]];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(s, (Column)c);
		text += L"\r\n";
	}
	return text;
}

LRESULT CMemoryStringsDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CWaitCursor wait;
	ClipboardHelper::CopyText(m_hWnd, RowsText(m_List.GetSelectedCount() > 0).c_str());
	return 0;
}

LRESULT CMemoryStringsDlg::OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	SuspendResumeHook sr;
	CSimpleFileDialog dlg(FALSE, L"txt", L"Strings.txt", OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER,
		L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0", m_hWnd);
	if (dlg.DoModal(m_hWnd) != IDOK)
		return 0;
	std::wstring path = dlg.m_szFileName;
	CWaitCursor wait;
	if (!ListExport::Save(path, RowsText(false)))
		AtlMessageBox(m_hWnd, std::format(L"Failed to write {}: {}", path, WinSys::Helpers::GetErrorText(::GetLastError())).c_str(),
			IDR_MAINFRAME, MB_ICONERROR);
	else
		SetDlgItemText(IDC_MSTR_STATUS, std::format(L"{} strings saved to {}", FormatHelper::FormatNumber((int64_t)m_Shown.size()), path).c_str());
	return 0;
}

LRESULT CMemoryStringsDlg::OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(IDCANCEL);
	return 0;
}
