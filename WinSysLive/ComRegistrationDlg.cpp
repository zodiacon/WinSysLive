#include "pch.h"
#include "ComRegistrationDlg.h"
#include "RegistryEditor.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <aclui.h>

#pragma comment(lib, "aclui")

using namespace WinSys;

namespace {
	//
	// A COM permission (an app ID's LaunchPermission or AccessPermission, or the machine's default) for the ACL editor,
	// read-only: the rights as dcomcnfg names them, each with COM_RIGHTS_EXECUTE, as COM writes them
	//
	class CComPermissionInfo : public ISecurityInformation {
	public:
		CComPermissionInfo(std::vector<uint8_t> sd, std::wstring name, bool launch)
			: m_Sd(std::move(sd)), m_Name(std::move(name)), m_Launch(launch) {
		}
		virtual ~CComPermissionInfo() = default;

		STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
			if (riid == IID_IUnknown || riid == IID_ISecurityInformation) {
				*ppv = static_cast<ISecurityInformation*>(this);
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		STDMETHOD_(ULONG, AddRef)() override {
			return ++m_RefCount;
		}
		STDMETHOD_(ULONG, Release)() override {
			auto count = --m_RefCount;
			if (count == 0)
				delete this;
			return count;
		}

		STDMETHOD(GetObjectInformation)(PSI_OBJECT_INFO info) override {
			*info = {};
			info->dwFlags = SI_READONLY | SI_ADVANCED | SI_NO_ACL_PROTECT | SI_NO_TREE_APPLY;
			info->hInstance = _Module.GetResourceInstance();
			info->pszObjectName = m_Name.data();
			return S_OK;
		}
		STDMETHOD(GetSecurity)(SECURITY_INFORMATION, PSECURITY_DESCRIPTOR* ppSD, BOOL) override {
			// the editor frees it with LocalFree
			auto copy = ::LocalAlloc(LPTR, m_Sd.size());
			if (!copy)
				return E_OUTOFMEMORY;
			memcpy(copy, m_Sd.data(), m_Sd.size());
			*ppSD = copy;
			return S_OK;
		}
		STDMETHOD(SetSecurity)(SECURITY_INFORMATION, PSECURITY_DESCRIPTOR) override {
			return E_ACCESSDENIED;
		}
		STDMETHOD(GetAccessRights)(const GUID*, DWORD, PSI_ACCESS* access, ULONG* count, ULONG* defaultAccess) override {
			static SI_ACCESS launch[] = {
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL, L"Local Launch", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_REMOTE, L"Remote Launch", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_ACTIVATE_LOCAL, L"Local Activation", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_ACTIVATE_REMOTE, L"Remote Activation", SI_ACCESS_GENERAL },
			};
			static SI_ACCESS accessRights[] = {
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_LOCAL, L"Local Access", SI_ACCESS_GENERAL },
				{ &GUID_NULL, COM_RIGHTS_EXECUTE | COM_RIGHTS_EXECUTE_REMOTE, L"Remote Access", SI_ACCESS_GENERAL },
			};
			*access = m_Launch ? launch : accessRights;
			*count = m_Launch ? _countof(launch) : _countof(accessRights);
			*defaultAccess = 0;
			return S_OK;
		}
		STDMETHOD(MapGeneric)(const GUID*, UCHAR*, ACCESS_MASK*) override {
			return S_OK;
		}
		STDMETHOD(GetInheritTypes)(PSI_INHERIT_TYPE*, ULONG*) override {
			return E_NOTIMPL;
		}
		STDMETHOD(PropertySheetPageCallback)(HWND, UINT, SI_PAGE_TYPE) override {
			return S_OK;
		}

	private:
		std::vector<uint8_t> m_Sd;
		std::wstring m_Name;
		bool m_Launch;
		ULONG m_RefCount{ 1 };
	};

	std::wstring Lower(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}
}

LRESULT CComRegistrationDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_COM, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_COM, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);

	auto dpi = ::GetDpiForWindow(m_hWnd);
	auto width = [&](int w) { return ::MulDiv(w, dpi, 96); };
	m_List.Attach(GetDlgItem(IDC_CR_LIST));
	m_List.ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
	static const struct {
		PCWSTR Name;
		int Width;
	} columns[] = {
		{ L"Name", 220 }, { L"CLSID", 270 }, { L"ProgID", 180 }, { L"Type", 120 }, { L"Server", 330 }, { L"Threading", 80 },
		{ L"App ID", 270 }, { L"App Name", 160 }, { L"Service", 110 }, { L"Run As", 110 }, { L"Registered", 130 },
		{ L"View", 55 }, { L"Signature", 200 }, { L"Package", 250 },
	};
	static_assert(_countof(columns) == (int)Column::_Count);
	for (int i = 0; i < _countof(columns); i++)
		m_List.InsertColumn(i, columns[i].Name, LVCFMT_LEFT, width(columns[i].Width));
	ListViewHelper::EnableColumnDpiScaling(m_List);
	CreateImages();
	::SendDlgItemMessage(m_hWnd, IDC_CR_FILTER, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name, CLSID, ProgID, server, app ID or package");
	CheckDlgButton(IDC_CR_32BIT, BST_CHECKED);
	m_Signatures.Init(m_hWnd, WM_COMREG_SIGNED);
	m_AclTheming.Install(m_hWnd);

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	Load();
	return TRUE;
}

void CComRegistrationDlg::CreateImages() {
	auto size = ::GetSystemMetricsForDpi(SM_CXSMICON, ::GetDpiForWindow(m_hWnd));
	CImageList images;
	images.Create(size, size, ILC_COLOR32 | ILC_MASK, 4, 0);
	// ImageOf's order
	for (auto id : { IDI_MODULES, IDI_COM_SERVER, IDI_COM_SERVICE, IDI_OBJECT }) {
		HICON icon = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(id), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
		images.AddIcon(icon);
		::DestroyIcon(icon);
	}
	// the list shares it (LVS_SHAREIMAGELISTS): m_Images owns it, the old one destroyed once it's replaced
	m_List.SetImageList(images, LVSIL_SMALL);
	m_Images.Attach(images.Detach());
}

int CComRegistrationDlg::ImageOf(ComServerType type) {
	switch (type) {
		case ComServerType::InProc: case ComServerType::Surrogate: case ComServerType::Handler: return 0;
		case ComServerType::LocalServer: return 1;
		case ComServerType::Service: return 2;
	}
	return 3;
}

LRESULT CComRegistrationDlg::OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	CreateImages();
	// the layout's handlers too
	bHandled = FALSE;
	return 0;
}

void CComRegistrationDlg::Load() {
	if (m_Load)
		return;
	GetDlgItem(IDC_CR_REFRESH).EnableWindow(FALSE);
	SetDlgItemText(IDC_CR_STATUS, L"Reading the registered classes...");
	// the thread holds the state, not the dialog, so closing needn't wait for it
	m_Load = std::make_shared<LoadState>();
	std::thread([state = m_Load, hWnd = m_hWnd] {
		// packaged classes' display names may need the resource loader (SHLoadIndirectString)
		auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		auto classes = ComRegistry::EnumClasses(false);
		auto wow64 = ComRegistry::EnumClasses(true);
		// a loop: a range insert's exception handling warns (C4530) where the app is built without exceptions
		classes.reserve(classes.size() + wow64.size());
		for (auto& c : wow64)
			classes.push_back(std::move(c));
		if (SUCCEEDED(com))
			::CoUninitialize();
		{
			std::lock_guard lock(state->Lock);
			state->Classes = std::move(classes);
			state->Done = true;
		}
		if (::IsWindow(hWnd))
			::PostMessage(hWnd, WM_COMREG_READ, 0, 0);
		}).detach();
}

LRESULT CComRegistrationDlg::OnRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (!m_Load)
		return 0;
	{
		std::lock_guard lock(m_Load->Lock);
		m_Classes = std::move(m_Load->Classes);
	}
	m_Load.reset();
	GetDlgItem(IDC_CR_REFRESH).EnableWindow(TRUE);
	ShowRows();
	return 0;
}

LRESULT CComRegistrationDlg::OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// the verified rows' text and colors; many arrive at once, so one repaint for those queued
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_COMREG_SIGNED, WM_COMREG_SIGNED, PM_REMOVE))
		;
	m_List.Invalidate(FALSE);
	return 0;
}

std::wstring CComRegistrationDlg::FileOf(ComClassRegistration const& c) {
	auto& file = c.ServerFile;
	if (file.empty() || c.Type == ComServerType::Service)
		return L"";
	WCHAR system[MAX_PATH];
	// a 32-bit DLL's System32 is SysWOW64; a local server (started by rpcss) isn't redirected
	bool wow = c.Wow64 && c.Type != ComServerType::LocalServer;
	if (!(wow ? ::GetSystemWow64Directory(system, _countof(system)) : ::GetSystemDirectory(system, _countof(system))))
		return file;
	if (file.find(L'\\') == std::wstring::npos)
		return std::wstring(system) + L"\\" + file;
	if (wow) {
		WCHAR native[MAX_PATH];
		auto length = ::GetSystemDirectory(native, _countof(native));
		if (length && file.size() > length && ::_wcsnicmp(file.c_str(), native, length) == 0 && file[length] == L'\\')
			return system + file.substr(length);
	}
	return file;
}

CComRegistrationDlg::Concern CComRegistrationDlg::GetConcern(ComClassRegistration const& c) const {
	if (c.ServerMissing)
		return Concern::Broken;
	if (auto file = FileOf(c); !file.empty())
		if (auto info = m_Signatures.GetResult(file); info && info->Status == SignatureStatus::Invalid)
			return Concern::Broken;
	if (c.OverridesMachine)
		return Concern::Override;
	if (c.PerUser)
		return Concern::PerUser;
	return Concern::None;
}

std::wstring CComRegistrationDlg::GetText(ComClassRegistration const& c, Column column, bool verify) {
	switch (column) {
		case Column::Name: return c.Name;
		case Column::Clsid: return c.Clsid;
		case Column::ProgId: return c.ProgId;
		case Column::Type: return ComRegistry::TypeText(c.Type);
		case Column::Server: return c.ServerMissing ? c.Server + L" (missing)" : c.Server;
		case Column::Threading: return c.ThreadingModel;
		case Column::AppId: return c.AppId;
		case Column::AppName: return c.AppIdName;
		case Column::Service: return c.ServiceName;
		case Column::RunAs: return c.RunAs;
		case Column::Registered:
			if (!c.Package.empty())
				return L"Package";
			return c.OverridesMachine ? L"User (over machine)" : c.PerUser ? L"User" : L"Machine";
		case Column::View: return c.Wow64 ? L"32-bit" : L"64-bit";
		case Column::Signature:
		{
			auto file = c.ServerMissing ? L"" : FileOf(c);
			if (file.empty())
				return L"";
			auto info = m_Signatures.GetResult(file);
			if (!info) {
				if (!verify)
					return L"";
				m_Signatures.Verify(file);
				return L"Checking...";
			}
			switch (info->Status) {
				case SignatureStatus::Valid: return info->Signer.empty() ? L"Signed" : L"Signed: " + info->Signer;
				case SignatureStatus::NotSigned: return L"Not signed";
				case SignatureStatus::Invalid: return L"Invalid: " + Helpers::GetErrorText(info->Error);
			}
			return L"Can't read: " + Helpers::GetErrorText(info->Error);
		}
		case Column::Package: return c.Package;
	}
	return L"";
}

void CComRegistrationDlg::ShowRows() {
	bool wow64 = IsDlgButtonChecked(IDC_CR_32BIT) == BST_CHECKED;
	bool suspicious = IsDlgButtonChecked(IDC_CR_SUSPICIOUS) == BST_CHECKED;
	auto selected = GetSelected();
	auto selectedKey = selected ? selected->Clsid + (selected->Wow64 ? L"32" : L"64") + selected->Package : L"";
	m_Rows.clear();
	int missing = 0, perUser = 0, overrides = 0, packaged = 0;
	for (int i = 0; i < (int)m_Classes.size(); i++) {
		auto& c = m_Classes[i];
		missing += c.ServerMissing;
		perUser += c.PerUser;
		overrides += c.OverridesMachine;
		packaged += !c.Package.empty();
		if (c.Wow64 && !wow64)
			continue;
		if (suspicious && GetConcern(c) == Concern::None) {
			// the unsigned too, once known
			auto file = FileOf(c);
			auto info = file.empty() ? std::nullopt : m_Signatures.GetResult(file);
			if (!info || info->Status == SignatureStatus::Valid)
				continue;
		}
		if (!m_Filter.empty()) {
			auto text = Lower(c.Name + L"\n" + c.Clsid + L"\n" + c.ProgId + L"\n" + c.Server + L"\n" + c.AppId + L"\n" + c.AppIdName + L"\n" + c.Package);
			if (text.find(m_Filter) == std::wstring::npos)
				continue;
		}
		m_Rows.push_back(i);
	}
	Sort();
	m_List.SetItemCountEx((int)m_Rows.size(), LVSICF_NOSCROLL);
	m_List.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	for (int r = 0; r < (int)m_Rows.size() && !selectedKey.empty(); r++) {
		auto& c = m_Classes[m_Rows[r]];
		if (c.Clsid + (c.Wow64 ? L"32" : L"64") + c.Package == selectedKey) {
			m_List.SetItemState(r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_List.EnsureVisible(r, FALSE);
			break;
		}
	}
	m_List.Invalidate();

	auto status = std::format(L"{} classes ({} packaged); {} per user, {} of them over the machine's; {} with a missing server",
		m_Classes.size(), packaged, perUser, overrides, missing);
	if (m_Rows.size() != m_Classes.size())
		status += std::format(L"; {} shown", m_Rows.size());
	SetDlgItemText(IDC_CR_STATUS, status.c_str());
}

void CComRegistrationDlg::Sort() {
	// each row's text once (not twice per comparison)
	auto column = (Column)m_SortColumn;
	std::vector<std::pair<std::wstring, int>> texts;
	texts.reserve(m_Rows.size());
	for (auto i : m_Rows)
		texts.push_back({ GetText(m_Classes[i], column), i });
	std::ranges::stable_sort(texts, [&](auto const& a, auto const& b) {
		// empty values last whichever the direction (a thousand classes have no name)
		if (a.first.empty() != b.first.empty())
			return b.first.empty();
		int result = ::_wcsicmp(a.first.c_str(), b.first.c_str());
		if (result == 0)
			result = ::_wcsicmp(m_Classes[a.second].Clsid.c_str(), m_Classes[b.second].Clsid.c_str());
		return m_SortAscending ? result < 0 : result > 0;
		});
	for (size_t r = 0; r < texts.size(); r++)
		m_Rows[r] = texts[r].second;
}

LRESULT CComRegistrationDlg::OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto di = reinterpret_cast<NMLVDISPINFO*>(hdr);
	auto& item = di->item;
	if (item.iItem < 0 || item.iItem >= (int)m_Rows.size())
		return 0;
	auto& c = m_Classes[m_Rows[item.iItem]];
	if (item.mask & LVIF_IMAGE)
		item.iImage = item.iSubItem == 0 ? ImageOf(c.Type) : -1;
	if (item.mask & LVIF_TEXT) {
		auto text = GetText(c, (Column)item.iSubItem, true);
		wcsncpy_s(item.pszText, item.cchTextMax, text.c_str(), _TRUNCATE);
	}
	return 0;
}

LRESULT CComRegistrationDlg::OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
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

LRESULT CComRegistrationDlg::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	auto cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
		{
			auto row = (size_t)cd->nmcd.dwItemSpec;
			if (row >= m_Rows.size())
				break;
			bool dark = WTLHelper::IsDarkMode();
			switch (GetConcern(m_Classes[m_Rows[row]])) {
				case Concern::Broken: cd->clrTextBk = dark ? RGB(128, 0, 0) : RGB(255, 160, 160); break;
				case Concern::Override: cd->clrTextBk = dark ? RGB(140, 70, 0) : RGB(255, 200, 120); break;
				case Concern::PerUser: cd->clrTextBk = dark ? RGB(100, 90, 0) : RGB(255, 255, 160); break;
				default: return CDRF_DODEFAULT;
			}
			cd->clrText = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
			break;
		}
	}
	return CDRF_DODEFAULT;
}

ComClassRegistration const* CComRegistrationDlg::GetSelected() const {
	if (!m_List)
		return nullptr;
	int row = m_List.GetNextItem(-1, LVNI_FOCUSED | LVNI_SELECTED);
	if (row < 0)
		row = m_List.GetNextItem(-1, LVNI_SELECTED);
	return row >= 0 && row < (int)m_Rows.size() ? &m_Classes[m_Rows[row]] : nullptr;
}

std::wstring CComRegistrationDlg::EditorPath(ComClassRegistration const& c) {
	if (!c.Package.empty())
		return L"Computer\\HKEY_CLASSES_ROOT\\PackagedCom\\Package\\" + c.Package + L"\\Class\\" + c.Clsid;
	return std::wstring(L"Computer\\") + (c.PerUser ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE") + L"\\SOFTWARE\\Classes\\"
		+ (c.Wow64 ? L"WOW6432Node\\" : L"") + L"CLSID\\" + c.Clsid;
}

LRESULT CComRegistrationDlg::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the focused row
		RECT rc{};
		int row = m_List.GetNextItem(-1, LVNI_FOCUSED);
		if (row >= 0 && m_List.GetItemRect(row, &rc, LVIR_LABEL))
			pt = { rc.left, rc.bottom };
		m_List.ClientToScreen(&pt);
	}
	auto c = GetSelected();
	bool file = c && !c->ServerMissing && !FileOf(*c).empty();
	// the permissions: the app ID's, else the machine's defaults (a packaged class's aren't in the registry)
	bool permissions = c && c->Package.empty();
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (file ? 0 : MF_GRAYED), ID_COMREG_OPENLOCATION, L"Open File &Location");
	menu.AppendMenu(MF_STRING | (c ? 0 : MF_GRAYED), ID_COMREG_OPENKEY, L"Open in &Registry Editor");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (permissions ? 0 : MF_GRAYED), ID_COMREG_LAUNCHPERMISSIONS,
		c && !c->HasLaunchPermission ? L"La&unch Permissions (default)..." : L"La&unch Permissions...");
	menu.AppendMenu(MF_STRING | (permissions ? 0 : MF_GRAYED), ID_COMREG_ACCESSPERMISSIONS,
		c && !c->HasAccessPermission ? L"&Access Permissions (default)..." : L"&Access Permissions...");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDC_CR_COPY, m_List.GetSelectedCount() ? L"&Copy" : L"&Copy All");
	menu.AppendMenu(MF_STRING | (c ? 0 : MF_GRAYED), ID_COMREG_COPYCLSID, L"Copy CLS&ID");
	auto id = (UINT)::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hWnd, nullptr);
	if (id)
		PostMessage(WM_COMMAND, id);
	return 0;
}

LRESULT CComRegistrationDlg::OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	BOOL handled;
	return OnOpenKey(0, 0, nullptr, handled);
}

LRESULT CComRegistrationDlg::OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto c = GetSelected();
	auto file = c ? FileOf(*c) : L"";
	if (file.empty())
		return 0;
	// Explorer with the file selected
	auto args = L"/select,\"" + file + L"\"";
	::ShellExecute(m_hWnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CComRegistrationDlg::OnOpenKey(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (auto c = GetSelected())
		OpenRegistryEditor(m_hWnd, EditorPath(*c));
	return 0;
}

LRESULT CComRegistrationDlg::OnPermissions(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto c = GetSelected();
	if (!c || !c->Package.empty())
		return 0;
	bool launch = wID == ID_COMREG_LAUNCHPERMISSIONS;
	bool isDefault = false;
	auto sd = ComRegistry::GetPermission(c->AppId, launch, c->Wow64, isDefault);
	if (sd.empty()) {
		AtlMessageBox(m_hWnd, launch
			? L"Neither the class's app ID nor the machine (DefaultLaunchPermission) sets launch permissions: COM's built-in defaults apply."
			: L"Neither the class's app ID nor the machine (DefaultAccessPermission) sets access permissions: COM's built-in defaults apply.",
			IDR_MAINFRAME, MB_ICONINFORMATION);
		return 0;
	}
	// the editor's title is "Permissions for <name>"
	auto name = !c->AppIdName.empty() ? c->AppIdName : !c->AppId.empty() ? c->AppId : c->Name.empty() ? c->Clsid : c->Name;
	name = isDefault ? std::wstring(launch ? L"the Machine's Default Launch" : L"the Machine's Default Access")
		: name + (launch ? L" (Launch)" : L" (Access)");
	auto info = new CComPermissionInfo(std::move(sd), std::move(name), launch);
	::EditSecurity(m_hWnd, info);
	info->Release();
	return 0;
}

LRESULT CComRegistrationDlg::OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CString text;
	GetDlgItemText(IDC_CR_FILTER, text);
	m_Filter = Lower((PCWSTR)text);
	ShowRows();
	return 0;
}

LRESULT CComRegistrationDlg::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Load();
	return 0;
}

LRESULT CComRegistrationDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// the selected rows, else every row shown; the column names first
	bool selected = m_List.GetSelectedCount() > 0;
	std::wstring text;
	for (int c = 0; c < (int)Column::_Count; c++) {
		WCHAR name[64]{};
		LVCOLUMN column{ LVCF_TEXT };
		column.pszText = name;
		column.cchTextMax = _countof(name);
		m_List.GetColumn(c, &column);
		text += (c ? L"\t" : L"") + std::wstring(name);
	}
	text += L"\r\n";
	for (int r = 0; r < (int)m_Rows.size(); r++) {
		if (selected && !(m_List.GetItemState(r, LVIS_SELECTED) & LVIS_SELECTED))
			continue;
		for (int c = 0; c < (int)Column::_Count; c++)
			text += (c ? L"\t" : L"") + GetText(m_Classes[m_Rows[r]], (Column)c);
		text += L"\r\n";
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CComRegistrationDlg::OnCopyClsid(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	std::wstring text;
	for (int r = m_List.GetNextItem(-1, LVNI_SELECTED); r >= 0; r = m_List.GetNextItem(r, LVNI_SELECTED))
		text += m_Classes[m_Rows[r]].Clsid + L"\r\n";
	if (!text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CComRegistrationDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
