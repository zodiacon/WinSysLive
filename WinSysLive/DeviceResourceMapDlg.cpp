#include "pch.h"
#include "DeviceResourceMapDlg.h"
#include "DeviceResourcesDlg.h"
#include "FilterBox.h"
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <unordered_set>

using namespace WinSys;

namespace {
	const struct {
		PCWSTR Name;
		int Width;
	} s_Columns[] = {
		{ L"Resource", 100 }, { L"Setting", 290 }, { L"Size", 90 }, { L"Device", 330 }, { L"Flags", 260 },
	};
}

LRESULT CDeviceResourceMapDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_DEVICE, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_DEVICE, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
	::SendDlgItemMessage(m_hWnd, IDC_RM_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter: type, setting, device or flags");

	m_List.Attach(GetDlgItem(IDC_RM_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _ColumnCount; i++)
		m_List.InsertColumn(i, s_Columns[i].Name, LVCFMT_LEFT, ::MulDiv(s_Columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	ShowRows();
	return TRUE;
}

std::pair<int, int64_t> CDeviceResourceMapDlg::OrderKey(DeviceResource const& r) {
	// memory and large memory together: they're the same address space
	int type = r.Type == DeviceResourceType::LargeMemory ? (int)DeviceResourceType::Memory : (int)r.Type;
	if (r.Type == DeviceResourceType::Interrupt) {
		// line-based by number, then the message-signaled ones (negative) by their magnitude
		auto value = (int32_t)r.Value;
		return { type, value >= 0 ? (int64_t)value : 0x100000000LL - value };
	}
	if (r.Type == DeviceResourceType::Dma)
		return { type, (int64_t)r.Value };
	return { type, (int64_t)r.Start };
}

void CDeviceResourceMapDlg::Load() {
	CWaitCursor wait;
	auto start = ::GetTickCount64();
	m_All.clear();
	if (auto devices = Devices::Enum(false))
		m_Devices = std::move(*devices);
	for (int i = 0; i < (int)m_Devices.size(); i++) {
		auto& d = m_Devices[i];
		if (!d.Present || d.InstanceId.empty())
			continue;
		auto resources = Devices::GetResources(d.InstanceId);
		if (!resources)
			continue;
		for (auto& r : *resources) {
			Row row{ r, i };
			auto cells = CDeviceResourcesDlg::ResourceCells(r);
			row.Cells = { cells[0], cells[1], cells[2], d.Name, cells[3] };
			m_All.push_back(std::move(row));
		}
	}
	// the default order, the rows' order in m_All; a device's own order kept within the same key
	std::ranges::stable_sort(m_All, [](auto& a, auto& b) {
		auto ka = OrderKey(a.Resource), kb = OrderKey(b.Resource);
		if (ka != kb)
			return ka < kb;
		// a range before the narrower ones in it (a bridge window before its devices')
		return a.Resource.End > b.Resource.End;
		});
	m_LoadMs = (DWORD)(::GetTickCount64() - start);
}

void CDeviceResourceMapDlg::ShowRows() {
	CString text;
	GetDlgItemText(IDC_RM_FILTER, text);
	// lowercase, as CFilterBox::Contains takes it
	std::wstring filter = (PCWSTR)text.Trim().MakeLower();
	m_Rows.clear();
	for (auto& row : m_All) {
		if (!filter.empty() && std::ranges::none_of(row.Cells, [&](auto& cell) { return CFilterBox::Contains(cell, filter); }))
			continue;
		m_Rows.push_back(&row);
	}
	if (m_SortColumn >= 0) {
		auto column = m_SortColumn;
		std::ranges::stable_sort(m_Rows, [&](Row const* a, Row const* b) {
			int result;
			if (column == Setting) {
				auto ka = OrderKey(a->Resource), kb = OrderKey(b->Resource);
				result = ka < kb ? -1 : kb < ka ? 1 : 0;
			}
			else if (column == Size) {
				auto sa = a->Resource.End - a->Resource.Start, sb = b->Resource.End - b->Resource.Start;
				result = sa < sb ? -1 : sb < sa ? 1 : 0;
			}
			else
				result = ::_wcsicmp(a->Cells[column].c_str(), b->Cells[column].c_str());
			return m_SortAscending ? result < 0 : result > 0;
			});
	}
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
	m_List.Invalidate();

	std::unordered_set<int> devices;
	for (auto row : m_Rows)
		devices.insert(row->Device);
	auto status = m_Rows.size() == m_All.size()
		? std::format(L"{} resources of {} devices (read in {} ms)", m_All.size(), devices.size(), m_LoadMs)
		: std::format(L"{} of {} resources, of {} devices", m_Rows.size(), m_All.size(), devices.size());
	SetDlgItemText(IDC_RM_STATUS, status.c_str());
}

LRESULT CDeviceResourceMapDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ShowRows();
	return 0;
}

LRESULT CDeviceResourceMapDlg::OnGetDispInfo(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMLVDISPINFO*>(pnmh);
	auto& item = di->item;
	if (item.iItem < 0 || item.iItem >= (int)m_Rows.size() || item.iSubItem < 0 || item.iSubItem >= _ColumnCount)
		return 0;
	if (item.mask & LVIF_TEXT)
		::StringCchCopy(item.pszText, item.cchTextMax, m_Rows[item.iItem]->Cells[item.iSubItem].c_str());
	return 0;
}

LRESULT CDeviceResourceMapDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	auto column = reinterpret_cast<NMLISTVIEW*>(pnmh)->iSubItem;
	m_SortAscending = column == m_SortColumn ? !m_SortAscending : true;
	m_SortColumn = column;
	ShowRows();
	return 0;
}

void CDeviceResourceMapDlg::ShowDevice(int row) {
	if (row < 0 || row >= (int)m_Rows.size() || !m_ShowDevice)
		return;
	m_ShowDevice(m_Devices[m_Rows[row]->Device], m_hWnd);
}

LRESULT CDeviceResourceMapDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	ShowDevice(reinterpret_cast<NMITEMACTIVATE*>(pnmh)->iItem);
	return 0;
}

LRESULT CDeviceResourceMapDlg::OnListKeyDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
	if (reinterpret_cast<NMLVKEYDOWN*>(pnmh)->wVKey == VK_RETURN)
		ShowDevice(m_List.GetNextItem(-1, LVNI_SELECTED));
	return 0;
}

LRESULT CDeviceResourceMapDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	for (int c = 0; c < _ColumnCount; c++)
		text += (c ? L"\t" : L"") + std::wstring(s_Columns[c].Name);
	text += L"\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < (int)m_Rows.size(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		for (int c = 0; c < _ColumnCount; c++)
			text += (c ? L"\t" : L"") + m_Rows[i]->Cells[c];
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CDeviceResourceMapDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
