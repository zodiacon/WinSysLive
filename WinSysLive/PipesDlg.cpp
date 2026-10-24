#include "pch.h"
#include "PipesDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>

LRESULT CPipesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_PIPE, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_PIPE, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_PM_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Kind", 70, LVCFMT_LEFT }, { L"Name", 420, LVCFMT_LEFT }, { L"Instances", 70, LVCFMT_RIGHT },
		{ L"Maximum", 80, LVCFMT_RIGHT }, { L"Created", 140, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	// the kinds' icons, in the window's small icon size; the list owns the image list (no LVS_SHAREIMAGELISTS)
	int size = ::GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	CImageList images;
	images.Create(size, size, ILC_COLOR32 | ILC_MASK, 2, 0);
	for (auto id : { IDI_PIPE, IDI_MAILSLOT })
		images.AddIcon((HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(id), IMAGE_ICON, size, size, LR_SHARED));
	m_List.SetImageList(images, LVSIL_SMALL);
	::SendDlgItemMessage(m_hWnd, IDC_PM_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name");

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CPipesDlg::Load() {
	CWaitCursor wait;
	m_Entries.clear();
	m_PipesError = m_MailslotsError = 0;
	if (auto pipes = WinSys::Pipes::EnumPipes())
		m_Entries = std::move(*pipes);
	else
		m_PipesError = ::GetLastError();
	// appended one by one: a range insert of this WinSys type (built without exceptions) warned C4530 under LTCG
	if (auto mailslots = WinSys::Pipes::EnumMailslots())
		for (auto& m : *mailslots)
			m_Entries.push_back(std::move(m));
	else
		m_MailslotsError = ::GetLastError();
	ShowRows();
}

std::wstring CPipesDlg::GetText(WinSys::PipeEntry const& e, Column column) const {
	switch (column) {
		case Column::Kind: return e.Mailslot ? L"Mailslot" : L"Pipe";
		case Column::Name: return e.Name;
		case Column::Instances: return e.Mailslot ? L"" : FormatHelper::FormatNumber(e.Instances);
		case Column::MaxInstances: return e.Mailslot ? L"" : e.MaxInstances < 0 ? std::wstring(L"Unlimited") : FormatHelper::FormatNumber(e.MaxInstances);
		case Column::Created: return FormatHelper::FormatDateTime(e.Created);
	}
	return L"";
}

void CPipesDlg::ShowRows() {
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int pipes = 0, mailslots = 0, row = 0;
	for (int i = 0; i < (int)m_Entries.size(); i++) {
		auto& e = m_Entries[i];
		(e.Mailslot ? mailslots : pipes)++;
		if (!m_Filter.empty()) {
			auto name = e.Name + (e.Mailslot ? L" mailslot" : L" pipe");
			::CharLowerBuff(name.data(), (DWORD)name.size());
			if (name.find(m_Filter) == std::wstring::npos)
				continue;
		}
		// image 0 is the pipe's, 1 the mailslot's
		m_List.InsertItem(row, GetText(e, Column::Kind).c_str(), e.Mailslot ? 1 : 0);
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(row, c, GetText(e, (Column)c).c_str());
		m_List.SetItemData(row, i);
		row++;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	m_List.SetRedraw(TRUE);

	auto status = std::format(L"{} pipes, {} mailslots", FormatHelper::FormatNumber(pipes), FormatHelper::FormatNumber(mailslots));
	if (!m_Filter.empty())
		status += std::format(L"; {} match the filter", FormatHelper::FormatNumber(row));
	if (m_PipesError)
		status += L"; pipes: " + WinSys::Helpers::GetErrorText(m_PipesError);
	if (m_MailslotsError)
		status += L"; mailslots: " + WinSys::Helpers::GetErrorText(m_MailslotsError);
	SetDlgItemText(IDC_PM_STATUS, status.c_str());
}

int CALLBACK CPipesDlg::Compare(LPARAM index1, LPARAM index2, LPARAM param) {
	auto self = reinterpret_cast<CPipesDlg*>(param);
	auto& a = self->m_Entries[index1];
	auto& b = self->m_Entries[index2];
	auto compare = [](auto const& x, auto const& y) { return x < y ? -1 : y < x ? 1 : 0; };
	int result;
	switch ((Column)self->m_SortColumn) {
		case Column::Kind: result = compare(a.Mailslot, b.Mailslot); break;
		case Column::Instances: result = compare(a.Instances, b.Instances); break;
		// unlimited (-1) sorts as the most
		case Column::MaxInstances: result = compare((uint64_t)a.MaxInstances, (uint64_t)b.MaxInstances); break;
		case Column::Created: result = compare(a.Created, b.Created); break;
		default: result = 0; break;
	}
	if (result == 0)
		result = ::_wcsicmp(a.Name.c_str(), b.Name.c_str());
	return self->m_SortAscending ? result : -result;
}

LRESULT CPipesDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto lv = reinterpret_cast<NMLISTVIEW*>(hdr);
	if (lv->iSubItem == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = lv->iSubItem;
		m_SortAscending = true;
	}
	m_List.SortItems(Compare, (LPARAM)this);
	return 0;
}

LRESULT CPipesDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_PM_FILTER, text);
	m_Filter = (PCWSTR)text;
	::CharLowerBuff(m_Filter.data(), (DWORD)m_Filter.size());
	ShowRows();
	return 0;
}

LRESULT CPipesDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CPipesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the selected rows, else every row shown; the column names first
	bool selected = m_List.GetSelectedCount() > 0;
	std::wstring text = L"Kind\tName\tInstances\tMaximum\tCreated\r\n";
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& e = m_Entries[m_List.GetItemData(i)];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(e, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CPipesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
