#include "pch.h"
#include "CertificatesDlg.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>
#include <wincrypt.h>
#include <cryptuiapi.h>

#pragma comment(lib, "cryptui")
#pragma comment(lib, "crypt32")

using namespace WinSys;

namespace {
	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	// a tree item's data: the location and the store's index plus one (0: the location itself)
	LPARAM ItemData(CertificateLocation location, int store) {
		return (LPARAM)location << 16 | (LPARAM)(store + 1);
	}

	int64_t Now() {
		FILETIME ft;
		::GetSystemTimeAsFileTime(&ft);
		return (int64_t)ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart;
	}

	// a physical store as certmgr's "Physical certificate stores" view names it
	std::wstring SourceName(std::wstring const& physical) {
		static const struct {
			PCWSTR Name, Text;
		} names[] = {
			{ L".Default", L"Registry" }, { L".GroupPolicy", L"Group Policy" }, { L".Enterprise", L"Enterprise" },
			{ L".AuthRoot", L"Third-Party" }, { L".SmartCard", L"Smart Card" }, { L".Flight", L"Windows Insider" },
		};
		for (auto& n : names)
			if (::_wcsicmp(physical.c_str(), n.Name) == 0)
				return n.Text;
		return physical;
	}

	std::wstring DateText(int64_t time) {
		if (time == 0)
			return L"";
		FILETIME ft{ (DWORD)time, (DWORD)(time >> 32) };
		SYSTEMTIME st, local;
		if (!::FileTimeToSystemTime(&ft, &st) || !::SystemTimeToTzSpecificLocalTime(nullptr, &st, &local))
			return L"";
		WCHAR date[64];
		::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, _countof(date), nullptr);
		return date;
	}
}

LRESULT CCertificatesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_CERTIFICATES, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_CERTIFICATES, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	m_Tree.Attach(GetDlgItem(IDC_CE_TREE));
	if (m_Images)
		m_Tree.SetImageList(m_Images, TVSIL_NORMAL);
	m_List.Attach(GetDlgItem(IDC_CE_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	static const struct {
		PCWSTR Name;
		int Width, Format;
	} columns[] = {
		{ L"Issued To", 240, LVCFMT_LEFT }, { L"Issued By", 220, LVCFMT_LEFT }, { L"Expires", 90, LVCFMT_LEFT },
		{ L"Concern", 300, LVCFMT_LEFT }, { L"Store", 200, LVCFMT_LEFT }, { L"Source", 90, LVCFMT_LEFT }, { L"Added", 90, LVCFMT_LEFT },
		{ L"Intended Purposes", 240, LVCFMT_LEFT }, { L"Friendly Name", 180, LVCFMT_LEFT }, { L"Key", 80, LVCFMT_LEFT },
		{ L"Signature", 100, LVCFMT_LEFT }, { L"Private Key", 70, LVCFMT_LEFT }, { L"Valid From", 90, LVCFMT_LEFT },
		{ L"Thumbprint", 300, LVCFMT_LEFT }, { L"Serial Number", 260, LVCFMT_LEFT },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, columns[i].Format, ::MulDiv(columns[i].Width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	CheckDlgButton(IDC_CE_CONCERNS, m_ConcernsOnly ? BST_CHECKED : BST_UNCHECKED);
	::SendDlgItemMessage(m_hWnd, IDC_CE_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name, thumbprint, serial number, purpose or concern");

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CCertificatesDlg::Load() {
	CWaitCursor wait;
	// the selection, by location and store name
	auto [selectedStore, selectedLocation] = GetSelection();
	std::wstring selectedName = selectedStore >= 0 ? m_Stores[selectedStore].Info.Name : L"";
	bool anySelected = m_Tree.GetSelectedItem() != nullptr;

	m_Program = Certificates::ReadRootProgram();
	m_Disallowed = Certificates::ReadDisallowed();
	m_Rows.clear();
	m_Stores.clear();
	auto now = Now();
	for (auto location : { CertificateLocation::LocalMachine, CertificateLocation::CurrentUser })
		for (auto& info : Certificates::EnumStores(location)) {
			Store store{ info };
			auto certs = Certificates::Read(location, info.Name);
			if (!certs)
				store.Error = ::GetLastError();
			else
				for (auto& c : *certs) {
					Row row{ std::move(c) };
					row.Assessment = Certificates::Assess(row.Cert, m_Program ? &*m_Program : nullptr, m_Disallowed ? &*m_Disallowed : nullptr, now);
					row.StoreName = info.DisplayName;
					if (row.Assessment.Concern != CertificateConcern::None)
						store.Concerns++;
					store.Rows.push_back(m_Rows.size());
					m_Rows.push_back(std::move(row));
				}
			m_Stores.push_back(std::move(store));
		}

	auto icon = [&](UINT id) { return m_IconIndex ? m_IconIndex(id) : -1; };
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	HTREEITEM select = nullptr, machineRoot = nullptr;
	for (auto location : { CertificateLocation::LocalMachine, CertificateLocation::CurrentUser }) {
		size_t count = 0, concerns = 0;
		for (auto& s : m_Stores)
			if (s.Info.Location == location) {
				count += s.Rows.size();
				concerns += s.Concerns;
			}
		auto text = std::wstring(Certificates::LocationName(location)) + L" (" + std::to_wstring(count)
			+ (concerns ? L", " + std::to_wstring(concerns) + L" of concern)" : L")");
		UINT image = location == CertificateLocation::LocalMachine ? IDI_SYSINFO : IDI_USER;
		auto parent = m_Tree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, text.c_str(),
			icon(image), icon(image), 0, 0, ItemData(location, -1), TVI_ROOT, TVI_LAST);
		if (anySelected && selectedStore < 0 && selectedLocation == location)
			select = parent;
		for (int i = 0; i < (int)m_Stores.size(); i++) {
			auto& s = m_Stores[i];
			if (s.Info.Location != location)
				continue;
			auto name = s.Info.DisplayName + L" (" + (s.Error ? std::wstring(L"can't be opened")
				: std::to_wstring(s.Rows.size()) + (s.Concerns ? L", " + std::to_wstring(s.Concerns) + L" of concern" : L"")) + L")";
			auto item = m_Tree.InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM, name.c_str(),
				icon(IDI_KEY), icon(IDI_KEY), 0, 0, ItemData(location, i), parent, TVI_LAST);
			if (selectedStore >= 0 && s.Info.Location == selectedLocation && s.Info.Name == selectedName)
				select = item;
			// at first, the machine's trusted roots
			if (location == CertificateLocation::LocalMachine && s.Info.Name == L"Root")
				machineRoot = item;
		}
		m_Tree.Expand(parent);
	}
	m_Tree.SetRedraw(TRUE);
	if (!select)
		select = machineRoot ? machineRoot : m_Tree.GetRootItem();
	if (select && select != m_Tree.GetSelectedItem()) {
		m_Tree.SelectItem(select);
		m_Tree.EnsureVisible(select);
	}
	else
		ShowRows();
	// EnsureVisible scrolls a long name into view sideways, cutting the locations' names
	m_Tree.SendMessage(WM_HSCROLL, SB_LEFT);
}

std::pair<int, CertificateLocation> CCertificatesDlg::GetSelection() const {
	auto item = m_Tree ? m_Tree.GetSelectedItem() : nullptr;
	if (!item)
		return { -1, CertificateLocation::LocalMachine };
	auto data = m_Tree.GetItemData(item);
	auto location = (CertificateLocation)(data >> 16);
	int store = (int)(data & 0xFFFF) - 1;
	return { store < (int)m_Stores.size() ? store : -1, location };
}

std::wstring CCertificatesDlg::GetText(Row const& row, Column column) const {
	auto& c = row.Cert;
	switch (column) {
		case Column::Subject: return c.Subject;
		case Column::Issuer: return c.Issuer;
		case Column::Expires: return DateText(c.NotAfter);
		case Column::Concern: return row.Assessment.Text;
		case Column::Store: return row.StoreName;
		case Column::Source: return SourceName(c.PhysicalStore);
		case Column::Added: return DateText(c.Added);
		case Column::Purposes:
		{
			if (c.Purposes.empty())
				return L"<All>";
			std::wstring text;
			for (auto& p : c.Purposes)
				text += (text.empty() ? L"" : L", ") + p;
			return text;
		}
		case Column::FriendlyName: return c.FriendlyName;
		case Column::Key: return c.KeyBits ? c.KeyAlgorithm + L" " + std::to_wstring(c.KeyBits) : c.KeyAlgorithm;
		case Column::Signature: return c.SignatureAlgorithm;
		case Column::PrivateKey: return c.HasPrivateKey ? L"Yes" : L"";
		case Column::ValidFrom: return DateText(c.NotBefore);
		case Column::Thumbprint: return c.Thumbprint;
		case Column::Serial: return c.SerialNumber;
	}
	return L"";
}

void CCertificatesDlg::ShowRows() {
	// the selection, by thumbprint and store
	std::wstring selected;
	if (auto row = GetSelectedRow())
		selected = row->Cert.Thumbprint + row->Cert.Store;

	m_Shown.clear();
	auto [store, location] = GetSelection();
	auto add = [&](Store const& s) {
		for (auto index : s.Rows) {
			auto& row = m_Rows[index];
			if (m_ConcernsOnly && row.Assessment.Concern == CertificateConcern::None)
				continue;
			if (!m_Filter.empty()) {
				bool match = false;
				for (auto column : { Column::Subject, Column::Issuer, Column::FriendlyName, Column::Thumbprint, Column::Serial, Column::Purposes, Column::Concern })
					if (Lowercase(GetText(row, column)).find(m_Filter) != std::wstring::npos) {
						match = true;
						break;
					}
				if (!match)
					continue;
			}
			m_Shown.push_back(index);
		}
	};
	if (store >= 0)
		add(m_Stores[store]);
	else if (m_Tree.GetSelectedItem())
		for (auto& s : m_Stores)
			if (s.Info.Location == location)
				add(s);

	auto column = (Column)m_SortColumn;
	std::ranges::stable_sort(m_Shown, [&](size_t a, size_t b) {
		auto& x = m_Rows[a];
		auto& y = m_Rows[b];
		int result;
		auto compare = [](auto p, auto q) { return p < q ? -1 : q < p ? 1 : 0; };
		switch (column) {
			case Column::Expires: result = compare(x.Cert.NotAfter, y.Cert.NotAfter); break;
			case Column::ValidFrom: result = compare(x.Cert.NotBefore, y.Cert.NotBefore); break;
			case Column::Added: result = compare(x.Cert.Added, y.Cert.Added); break;
			case Column::Concern:
				result = compare(x.Assessment.Concern, y.Assessment.Concern);
				if (result == 0)
					result = ::_wcsicmp(x.Assessment.Text.c_str(), y.Assessment.Text.c_str());
				break;
			case Column::Key:
				result = ::_wcsicmp(x.Cert.KeyAlgorithm.c_str(), y.Cert.KeyAlgorithm.c_str());
				if (result == 0)
					result = compare(x.Cert.KeyBits, y.Cert.KeyBits);
				break;
			default: result = ::_wcsicmp(GetText(x, column).c_str(), GetText(y, column).c_str()); break;
		}
		if (result == 0)
			result = ::_wcsicmp(x.Cert.Subject.c_str(), y.Cert.Subject.c_str());
		return m_SortAscending ? result < 0 : result > 0;
		});

	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	int select = -1;
	for (int i = 0; i < (int)m_Shown.size(); i++) {
		auto& row = m_Rows[m_Shown[i]];
		m_List.InsertItem(i, row.Cert.Subject.c_str());
		for (int c = 1; c < (int)Column::_Count; c++)
			m_List.SetItemText(i, c, GetText(row, (Column)c).c_str());
		m_List.SetItemData(i, i);
		if (select < 0 && !selected.empty() && row.Cert.Thumbprint + row.Cert.Store == selected)
			select = i;
	}
	if (select >= 0) {
		m_List.SetItemState(select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.EnsureVisible(select, FALSE);
	}
	m_List.SetRedraw(TRUE);
	m_List.Invalidate();
	UpdateStatus();
}

CCertificatesDlg::Row const* CCertificatesDlg::GetSelectedRow() const {
	if (!m_List)
		return nullptr;
	int sel = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (sel < 0)
		return nullptr;
	auto index = (size_t)m_List.GetItemData(sel);
	return index < m_Shown.size() ? &m_Rows[m_Shown[index]] : nullptr;
}

void CCertificatesDlg::UpdateStatus() {
	size_t concerns = 0;
	for (auto& s : m_Stores)
		concerns += s.Concerns;
	std::wstring text = std::to_wstring(m_Rows.size()) + L" certificates, " + std::to_wstring(concerns) + L" of concern; "
		+ std::to_wstring(m_Shown.size()) + L" shown. ";
	if (m_Program)
		text += L"Microsoft's root program: " + std::to_wstring(m_Program->Thumbprints.size()) + L" roots"
			+ (m_Program->LastSync ? L", synced " + DateText(m_Program->LastSync) : L"");
	else
		text += L"Microsoft's root program isn't cached (no root update yet, or turned off): roots aren't judged";
	SetDlgItemText(IDC_CE_STATUS, text.c_str());
	GetDlgItem(IDC_CE_VIEW).EnableWindow(GetSelectedRow() != nullptr);
}

LRESULT CCertificatesDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List || !GetSelectedRow()) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		m_List.GetItemRect(m_List.GetNextItem(-1, LVNI_SELECTED), &rc, LVIR_LABEL);
		pt = rc.CenterPoint();
		m_List.ClientToScreen(&pt);
	}
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, IDC_CE_VIEW, L"&View Certificate...");
	menu.SetMenuDefaultItem(IDC_CE_VIEW);
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_CE_COPY, L"&Copy");
	menu.AppendMenu(MF_STRING, IDC_CE_COPYTHUMBPRINT, L"Copy &Thumbprint");
	menu.TrackPopupMenu(TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return 0;
}

LRESULT CCertificatesDlg::OnTreeSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	ShowRows();
	return 0;
}

LRESULT CCertificatesDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto column = reinterpret_cast<NMLISTVIEW*>(hdr)->iSubItem;
	if (column == m_SortColumn)
		m_SortAscending = !m_SortAscending;
	else {
		m_SortColumn = column;
		// the worst concern and the latest dates first
		auto c = (Column)column;
		m_SortAscending = !(c == Column::Concern || c == Column::Added || c == Column::Expires || c == Column::ValidFrom);
	}
	ShowRows();
	return 0;
}

LRESULT CCertificatesDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnView(0, IDC_CE_VIEW, nullptr, handled);
}

LRESULT CCertificatesDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto index = (size_t)cd->nmcd.lItemlParam;
			if (index >= m_Shown.size())
				break;
			bool dark = WTLHelper::IsDarkMode();
			// as the COM Registration dialog's
			switch (m_Rows[m_Shown[index]].Assessment.Concern) {
				case CertificateConcern::Danger: cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160); break;
				case CertificateConcern::NotInProgram: cd->clrTextBk = dark ? RGB(140, 70, 0) : RGB(255, 200, 120); break;
				case CertificateConcern::Recent: cd->clrTextBk = dark ? RGB(100, 90, 0) : RGB(255, 255, 160); break;
				case CertificateConcern::Expired:
					cd->clrText = RGB(128, 128, 128);
					return CDRF_NEWFONT;
				default: return CDRF_DODEFAULT;
			}
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			return CDRF_NEWFONT;
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CCertificatesDlg::OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	GetDlgItem(IDC_CE_VIEW).EnableWindow(GetSelectedRow() != nullptr);
	return 0;
}

LRESULT CCertificatesDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_CE_FILTER, text);
	m_Filter = Lowercase((PCWSTR)text);
	ShowRows();
	return 0;
}

LRESULT CCertificatesDlg::OnConcernsOnly(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_ConcernsOnly = IsDlgButtonChecked(IDC_CE_CONCERNS) == BST_CHECKED;
	ShowRows();
	return 0;
}

LRESULT CCertificatesDlg::OnView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = GetSelectedRow();
	if (!row || row->Cert.Encoded.empty())
		return 0;
	auto cert = ::CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, row->Cert.Encoded.data(), (DWORD)row->Cert.Encoded.size());
	if (!cert)
		return 0;
	::CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT, cert, m_hWnd, nullptr, 0, nullptr);
	::CertFreeCertificateContext(cert);
	return 0;
}

LRESULT CCertificatesDlg::OnManager(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto [store, location] = GetSelection();
	// the machine's needs elevation (it asks for it)
	::ShellExecute(m_hWnd, L"open", location == CertificateLocation::LocalMachine ? L"certlm.msc" : L"certmgr.msc", nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CCertificatesDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CCertificatesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	LVCOLUMN col{ LVCF_TEXT };
	WCHAR name[64];
	col.pszText = name;
	col.cchTextMax = _countof(name);
	for (int c = 0; c < (int)Column::_Count; c++)
		if (m_List.GetColumn(c, &col))
			text += (c ? L"\t" : L"") + std::wstring(name);
	text += L"\r\n";
	bool selected = m_List.GetSelectedCount() > 0;
	for (int i = 0; i < m_List.GetItemCount(); i++) {
		if (selected && !(m_List.GetItemState(i, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		auto& row = m_Rows[m_Shown[m_List.GetItemData(i)]];
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(row, (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CCertificatesDlg::OnCopyThumbprint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto row = GetSelectedRow())
		ClipboardHelper::CopyText(m_hWnd, row->Cert.Thumbprint.c_str());
	return 0;
}

LRESULT CCertificatesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
