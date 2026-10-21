#include "pch.h"
#include "TcpIpPage.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ClipboardHelper.h>

CProcTcpIpPage::CProcTcpIpPage(ProcessProperties const& props, ProcessSnapshot const& snapshot) : m_Props(props), m_Snapshot(snapshot) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(IDI_NETWORK);
}

LRESULT CProcTcpIpPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_TCPIP_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
	auto cm = GetColumnManager(m_List);
	// widths at 96 DPI: the column manager scales them to the list's DPI
	cm->AddColumn(L"Protocol", LVCFMT_LEFT, 65, Column::Protocol);
	cm->AddColumn(L"Local Address", LVCFMT_LEFT, 150, Column::LocalAddress);
	cm->AddColumn(L"Local Port", LVCFMT_RIGHT, 70, Column::LocalPort);
	cm->AddColumn(L"Remote Address", LVCFMT_LEFT, 150, Column::RemoteAddress);
	cm->AddColumn(L"Remote Port", LVCFMT_RIGHT, 80, Column::RemotePort);
	cm->AddColumn(L"State", LVCFMT_LEFT, 90, Column::State);
	cm->AddColumn(L"Created", LVCFMT_LEFT, 150, Column::Created);
	UpdateButtons();

	InitDynamicLayout(false, false);
	return TRUE;
}

int CProcTcpIpPage::OnSetActive() {
	// the sheet's last snapshot; the sheet feeds the page only while it's active
	OnSample(m_Snapshot);
	return 0;
}

void CProcTcpIpPage::OnSample(ProcessSnapshot const& snapshot) {
	if (m_Exited)
		return;
	if (snapshot.Exited()) {
		// the last connections stay, as they were
		m_Exited = true;
		std::erase_if(m_Rows, [](auto& c) { return c.Closed; });
		for (auto& c : m_Rows)
			c.NewTicks = 0;
		m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
		SetDlgItemText(IDC_TCPIP_STATUS, L"The process has exited");
		return;
	}

	auto selected = GetSelectedKeys();
	if (!Load())
		return;

	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (auto si = GetSortInfo(m_List); si)
		DoSort(si);
	SelectConnections(selected);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());

	auto tcp = std::ranges::count_if(m_Rows, [](auto& c) { return !c.Closed && c.IsTcp(); });
	auto udp = std::ranges::count_if(m_Rows, [](auto& c) { return !c.Closed && !c.IsTcp(); });
	SetDlgItemText(IDC_TCPIP_STATUS, std::format(L"{} TCP connection{}, {} UDP endpoint{}",
		tcp, tcp == 1 ? L"" : L"s", udp, udp == 1 ? L"" : L"s").c_str());
	GetDlgItem(IDC_TCPIP_COPYALL).EnableWindow(!m_Rows.empty());
	UpdateButtons();
}

bool CProcTcpIpPage::Load() {
	auto current = WinSys::ConnectionInfo::Read(m_Props.Id);
	if (!current) {
		SetDlgItemText(IDC_TCPIP_STATUS, std::format(L"Can't read the connections: {}",
			WinSys::Helpers::GetErrorText(::GetLastError())).c_str());
		return false;
	}
	// on the first read, all of them are new to the page, not opened
	ConnectionRows::Merge(m_Rows, std::move(*current), m_Loaded);
	m_Loaded = true;
	return true;
}

std::wstring CProcTcpIpPage::GetText(Connection const& c, Column column) const {
	switch (column) {
		case Column::Protocol: return WinSys::ConnectionInfo::ProtocolName(c.Id.Proto);
		case Column::LocalAddress: return c.LocalText;
		case Column::LocalPort: return std::to_wstring(c.Id.LocalPort);
		case Column::RemoteAddress: return c.RemoteText;
		case Column::RemotePort: return c.RemoteText.empty() ? L"" : std::to_wstring(c.Id.RemotePort);
		case Column::State: return WinSys::ConnectionInfo::StateName(c);
		case Column::Created: return c.Created ? FormatHelper::FormatDateTime(c.Created) : L"";
	}
	return L"";
}

CString CProcTcpIpPage::GetColumnText(HWND, int row, int col) const {
	if (row >= (int)m_Rows.size())
		return L"";
	return GetText(m_Rows[row], GetColumnManager(m_List)->GetColumnTag<Column>(col)).c_str();
}

void CProcTcpIpPage::DoSort(SortInfo const* si) {
	auto selected = GetSelectedKeys();

	auto column = GetColumnManager(m_List)->GetColumnTag<Column>(si->SortColumn);
	auto compare = [](auto const& a, auto const& b) { return a < b ? -1 : (b < a ? 1 : 0); };
	std::ranges::stable_sort(m_Rows, [&](Connection const& c1, Connection const& c2) {
		int result = 0;
		switch (column) {
			case Column::Protocol: result = compare(c1.Id.Proto, c2.Id.Proto); break;
			// by the address's bytes, so 10.0.0.9 comes before 10.0.0.10
			case Column::LocalAddress: result = compare(c1.Id.LocalAddress, c2.Id.LocalAddress); break;
			case Column::LocalPort: result = compare(c1.Id.LocalPort, c2.Id.LocalPort); break;
			case Column::RemoteAddress: result = compare(c1.Id.RemoteAddress, c2.Id.RemoteAddress); break;
			case Column::RemotePort: result = compare(c1.Id.RemotePort, c2.Id.RemotePort); break;
			case Column::State: result = ::_wcsicmp(GetText(c1, column).c_str(), GetText(c2, column).c_str()); break;
			case Column::Created: result = compare(c1.Created, c2.Created); break;
		}
		return si->SortAscending ? result < 0 : result > 0;
		});
	SelectConnections(selected);
}

std::vector<CProcTcpIpPage::Key> CProcTcpIpPage::GetSelectedKeys() const {
	// the focused row first, so it keeps the focus
	std::vector<Key> keys;
	int focused = m_List.GetNextItem(-1, LVNI_FOCUSED | LVNI_SELECTED);
	if (focused >= 0 && focused < (int)m_Rows.size())
		keys.push_back(ConnectionRows::KeyOf(m_Rows[focused]));
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size(); i = m_List.GetNextItem(i, LVNI_SELECTED))
		if (i != focused)
			keys.push_back(ConnectionRows::KeyOf(m_Rows[i]));
	return keys;
}

void CProcTcpIpPage::SelectConnections(std::vector<Key> const& keys) {
	// the list view keeps the selection by index, which new rows and sorting shift
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	for (size_t k = 0; k < keys.size(); k++) {
		auto it = std::ranges::find_if(m_Rows, [&](auto& c) { return ConnectionRows::KeyOf(c) == keys[k]; });
		if (it != m_Rows.end())
			m_List.SetItemState((int)(it - m_Rows.begin()), k == 0 ? LVIS_SELECTED | LVIS_FOCUSED : LVIS_SELECTED,
				LVIS_SELECTED | LVIS_FOCUSED);
	}
}

void CProcTcpIpPage::OnStateChanged(HWND, int, int, UINT, UINT) {
	// a range of rows selected with Shift
	UpdateButtons();
}

LRESULT CProcTcpIpPage::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& bHandled) {
	UpdateButtons();
	bHandled = FALSE;
	return 0;
}

void CProcTcpIpPage::UpdateButtons() {
	GetDlgItem(IDC_TCPIP_COPY).EnableWindow(m_List.GetSelectedCount() > 0);
}

LRESULT CProcTcpIpPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;

		case CDDS_ITEMPREPAINT:
		{
			auto index = (int)cd->nmcd.dwItemSpec;
			if (index < 0 || index >= (int)m_Rows.size())
				break;
			auto& row = m_Rows[index];
			bool dark = WTLHelper::IsDarkMode();
			// the colors of new and exited threads on the Threads page
			if (row.Closed) {
				cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			else if (row.NewTicks > 0) {
				cd->clrTextBk = dark ? RGB(0, 96, 0) : RGB(160, 255, 160);
				cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			}
			break;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CProcTcpIpPage::OnCopy(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a line per connection, its columns separated by tabs
	std::wstring text;
	for (int i = id == IDC_TCPIP_COPYALL ? 0 : m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0 && i < (int)m_Rows.size();
		i = id == IDC_TCPIP_COPYALL ? i + 1 : m_List.GetNextItem(i, LVNI_SELECTED)) {
		for (int c = 0; c < (int)Column::_Count; c++) {
			if (c > 0)
				text += L'\t';
			text += GetText(m_Rows[i], (Column)c);
		}
		text += L"\r\n";
	}
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}
