#include "pch.h"
#include "HandleSheet.h"
#include <ListViewhelper.h>
#include "FormatHelper.h"
#include "HandleIcons.h"
#include "ObjectSecurity.h"
#include <WinSys/AccessMask.h>
#include <WinSys/Helpers.h>
#include <ClipboardHelper.h>
#include <WTLHelper.h>

using namespace WinSys;

namespace {
	// flags named in a list, the unnamed rest in hex
	std::wstring FormatFlags(uint32_t value, std::initializer_list<std::pair<uint32_t, PCWSTR>> names) {
		std::wstring text;
		for (auto& [flag, name] : names)
			if ((value & flag) == flag) {
				text += (text.empty() ? L"" : L", ") + std::wstring(name);
				value &= ~flag;
			}
		if (value)
			text += (text.empty() ? L"" : L", ") + std::format(L"0x{:X}", value);
		return text;
	}

	std::wstring FormatCount(uint64_t value) {
		return FormatHelper::FormatNumber((int64_t)value);
	}

	// bytes, with the exact count
	std::wstring FormatSize(uint64_t bytes) {
		if (bytes < 1024)
			return std::format(L"{} bytes", bytes);
		return std::format(L"{} ({} bytes)", FormatHelper::FormatBytes(bytes), FormatCount(bytes));
	}

	PCWSTR MachineToString(uint16_t machine) {
		switch (machine) {
			case IMAGE_FILE_MACHINE_I386: return L"x86";
			case IMAGE_FILE_MACHINE_AMD64: return L"x64";
			case IMAGE_FILE_MACHINE_ARM64: return L"ARM64";
			case IMAGE_FILE_MACHINE_ARMNT: return L"ARM";
		}
		return L"";
	}

	PCWSTR SubsystemToString(uint16_t subsystem) {
		switch (subsystem) {
			case IMAGE_SUBSYSTEM_NATIVE: return L"Native";
			case IMAGE_SUBSYSTEM_WINDOWS_GUI: return L"Windows GUI";
			case IMAGE_SUBSYSTEM_WINDOWS_CUI: return L"Windows Console";
			case IMAGE_SUBSYSTEM_EFI_APPLICATION: return L"EFI Application";
			case IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER: return L"EFI Boot Service Driver";
			case IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER: return L"EFI Runtime Driver";
			case IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION: return L"Boot Application";
		}
		return L"";
	}

	PCWSTR ImpersonationLevelToString(SECURITY_IMPERSONATION_LEVEL level) {
		switch (level) {
			case SecurityAnonymous: return L"Anonymous";
			case SecurityIdentification: return L"Identification";
			case SecurityImpersonation: return L"Impersonation";
			case SecurityDelegation: return L"Delegation";
		}
		return L"";
	}

	std::wstring FormatExitCode(std::optional<uint32_t> const& code) {
		return code ? std::format(L"Exited (0x{:X})", *code) : L"Running";
	}
}

namespace {
	// rows with section titles; for the Details tab, the first section's name is the tab's title instead of a row
	struct RowBuilder {
		explicit RowBuilder(std::wstring* title = nullptr) : Title(title) {}

		void Section(std::wstring name) {
			if (Title) {
				if (Title->empty())
					*Title = std::move(name);
				return;
			}
			Rows.push_back({ std::move(name), L"", true });
		}
		void Add(std::wstring name, std::wstring value) {
			Rows.push_back({ std::move(name), std::move(value) });
		}

		std::vector<CHandleListPage::Row> Rows;
		std::wstring* Title;
	};
}

CHandleSheet::CHandleSheet(HANDLE hProcess, uint32_t pid, std::wstring processImage, HandleEntry const& entry,
	std::wstring name, std::function<std::wstring(DWORD)> processName) :
	m_hProcess(hProcess), m_Pid(pid), m_ProcessImage(std::move(processImage)), m_Entry(entry), m_Name(std::move(name)),
	m_Type(ObjectTypes::GetName(entry.TypeIndex)), m_ProcessName(std::move(processName)),
	m_General(*this, L"General"), m_TypeDetails(*this, L"Details") {
	m_Title = std::format(L"Handle 0x{:X} ({}) in {} ({})", m_Entry.Value, m_Type, m_ProcessImage, m_Pid);
	SetTitle(m_Title.c_str());
	m_psh.dwFlags |= PSH_NOCONTEXTHELP;

	// the type's icon for the sheet and the details tab
	auto typeIcon = GetHandleIcon(m_Type, m_Name);
	m_Icon = AtlLoadIconImage(typeIcon, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	m_SmallIcon = AtlLoadIconImage(typeIcon, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	m_psh.dwFlags |= PSH_USEHICON;
	m_psh.hIcon = m_SmallIcon;
	m_General.SetPageIcon(IDI_HANDLES);
	m_TypeDetails.SetPageIcon(typeIcon);

	// the object's address, unless the entry came from the system handle table (the Handles page's Address column):
	// looked up there, if the value still has the same type and access
	if (!m_Entry.Object) {
		HandleEnumerator enumerator;
		if (auto handles = enumerator.EnumProcessHandlesWithAddresses(m_Pid)) {
			auto it = std::ranges::find_if(*handles, [&](auto const& h) {
				return h.Value == m_Entry.Value && h.TypeIndex == m_Entry.TypeIndex && h.Access == m_Entry.Access;
				});
			if (it != handles->end())
				m_Entry.Object = it->Object;
		}
	}

	Query();
	m_General.SetRows(GeneralRows());
	m_TypeDetails.SetRows(DetailRows(m_DetailsTitle));
	m_TypeDetails.SetPageTitle(m_DetailsTitle);

	// the order is the tabs'
	AddPage(m_General);
	// the ACL editor's page, on a duplicate of the handle (with its access); it reports its own errors
	auto mapping = m_Details ? m_Details->TypeStatistics.GenericMapping : GENERIC_MAPPING{};
	m_Security = new CObjectSecurityInfo(ObjectInspector::OpenHandle(m_hProcess, m_Entry.Value, m_Type), m_Type,
		m_Name.empty() ? std::format(L"{} 0x{:X}", m_Type, m_Entry.Value) : m_Name, m_Entry.Access, mapping);
	if (auto page = ::CreateSecurityPage(m_Security); page && AddPage(page))
		m_SecurityTab = 1;
	AddPage(m_TypeDetails);
}

void CHandleSheet::OnSheetInitialized() {
	BaseSheet::OnSheetInitialized();
	SetIcon(m_Icon, TRUE);
	SetIcon(m_SmallIcon, FALSE);
	if (m_SecurityTab >= 0)
		SetSecurityTabIcon(m_hWnd, m_SecurityTab);
	// the ACL editor's dialogs, when they come
	m_AclTheming.Install(m_hWnd);
}

CHandleSheet::~CHandleSheet() {
	if (m_Security)
		m_Security->Release();
}

void CHandleSheet::Query() {
	m_Details = ObjectInspector::QueryHandle(m_hProcess, m_Entry.Value, m_Type);
	m_Error = m_Details ? 0 : ::GetLastError();
}

void CHandleSheet::Refresh() {
	Query();
	m_General.SetRows(GeneralRows());
	// the tab keeps its title: the type's is the same
	std::wstring title;
	m_TypeDetails.SetRows(DetailRows(title));
}

std::wstring CHandleSheet::ProcessText(DWORD pid) const {
	auto name = m_ProcessName ? m_ProcessName(pid) : std::wstring();
	return name.empty() ? std::to_wstring(pid) : std::format(L"{} ({})", name, pid);
}

std::wstring CHandleSheet::Decode(uint32_t access) const {
	return std::format(L"0x{:08X}: {}", access, AccessMaskDecoder::Decode(m_Type, access));
}

std::vector<CHandleSheet::Row> CHandleSheet::GeneralRows() const {
	RowBuilder rows;
	rows.Section(L"Handle");
	rows.Add(L"Name", m_Name);
	rows.Add(L"Type", m_Type);
	rows.Add(L"Handle", std::format(L"0x{:X}", m_Entry.Value));
	if (m_Entry.Object)
		rows.Add(L"Object Address", std::format(L"0x{:X}", (ULONG_PTR)m_Entry.Object));
	rows.Add(L"Granted Access", Decode(m_Entry.Access));
	rows.Add(L"Attributes", FormatFlags(m_Entry.Attributes, { { OBJ_INHERIT, L"Inherit" }, { 0x1, L"Protect from close" } }));

	if (!m_Details) {
		rows.Section(L"Object");
		rows.Add(L"Not Available", m_Error == ERROR_INVALID_HANDLE
			? std::wstring(L"The handle isn't there anymore (or is another object's now)")
			: L"The object can't be queried: " + Helpers::GetErrorText(m_Error));
		return rows.Rows;
	}

	auto& d = *m_Details;
	rows.Section(L"Object");
	rows.Add(L"Handles", FormatCount(d.Handles));
	rows.Add(L"References", FormatCount(d.References));
	rows.Add(L"Paged Pool Charge", FormatSize(d.PagedPoolCharge));
	rows.Add(L"Non-Paged Pool Charge", FormatSize(d.NonPagedPoolCharge));
	if (auto flags = d.Attributes & (OBJ_PERMANENT | OBJ_EXCLUSIVE))
		rows.Add(L"Object Attributes", FormatFlags(flags, { { OBJ_PERMANENT, L"Permanent" }, { OBJ_EXCLUSIVE, L"Exclusive" } }));
	if (d.CreationTime)
		rows.Add(L"Created", FormatHelper::FormatDateTime(d.CreationTime));

	auto& t = d.TypeStatistics;
	rows.Section(L"Type: " + m_Type);
	rows.Add(L"Objects", std::format(L"{} (peak {})", FormatCount(t.Objects), FormatCount(t.PeakObjects)));
	rows.Add(L"Handles", std::format(L"{} (peak {})", FormatCount(t.Handles), FormatCount(t.PeakHandles)));
	rows.Add(L"Valid Access", Decode(t.ValidAccessMask));
	rows.Add(L"Generic Read", Decode(t.GenericMapping.GenericRead));
	rows.Add(L"Generic Write", Decode(t.GenericMapping.GenericWrite));
	rows.Add(L"Generic Execute", Decode(t.GenericMapping.GenericExecute));
	rows.Add(L"Generic All", Decode(t.GenericMapping.GenericAll));
	rows.Add(L"Pool", (t.PoolType & 1) ? L"Paged" : L"Non-Paged");
	rows.Add(L"Security Required", t.SecurityRequired ? L"Yes" : L"No");
	return rows.Rows;
}

std::vector<CHandleSheet::Row> CHandleSheet::DetailRows(std::wstring& title) const {
	// the first section's name is the tab's title
	RowBuilder rows(&title);
	if (!m_Details) {
		title = L"Details";
		return {};
	}
	auto& d = *m_Details;
	if (d.Event) {
		rows.Section(L"Event");
		rows.Add(L"Kind", d.Event->Notification ? L"Notification (manual reset)" : L"Synchronization (auto reset)");
		rows.Add(L"State", d.Event->Signaled ? L"Signaled" : L"Not signaled");
	}
	else if (d.Mutant) {
		auto& m = *d.Mutant;
		rows.Section(L"Mutex");
		rows.Add(L"State", m.CurrentCount > 0 ? std::wstring(L"Free")
			: m.CurrentCount == 0 ? std::wstring(L"Owned") : std::format(L"Owned (acquired {} times)", 1 - m.CurrentCount));
		if (m.OwnerThreadId)
			rows.Add(L"Owner", std::format(L"Thread {} in {}", m.OwnerThreadId, ProcessText(m.OwnerProcessId)));
		rows.Add(L"Abandoned", m.Abandoned ? L"Yes" : L"No");
	}
	else if (d.Semaphore) {
		rows.Section(L"Semaphore");
		rows.Add(L"Current Count", FormatCount(d.Semaphore->Count));
		rows.Add(L"Limit", FormatCount(d.Semaphore->Maximum));
	}
	else if (d.Timer) {
		rows.Section(L"Timer");
		rows.Add(L"State", d.Timer->Signaled ? L"Signaled" : L"Not signaled");
		if (!d.Timer->Signaled && d.Timer->Remaining > 0)
			rows.Add(L"Due In", FormatHelper::FormatTimeSpan(d.Timer->Remaining));
	}
	else if (d.IoCompletion) {
		rows.Section(L"I/O Completion");
		rows.Add(L"Queued Completions", FormatCount(d.IoCompletion->Depth));
	}
	else if (d.Section) {
		auto& s = *d.Section;
		rows.Section(L"Section");
		rows.Add(L"Size", FormatSize(s.Size));
		rows.Add(L"Attributes", FormatFlags(s.Attributes, { { SEC_IMAGE_NO_EXECUTE, L"Image (no execute)" }, { SEC_IMAGE, L"Image" },
			{ SEC_PROTECTED_IMAGE, L"Protected Image" }, { SEC_FILE, L"File" }, { SEC_COMMIT, L"Commit" }, { SEC_RESERVE, L"Reserve" },
			{ SEC_NOCACHE, L"No Cache" }, { SEC_LARGE_PAGES, L"Large Pages" }, { SEC_WRITECOMBINE, L"Write Combine" },
			{ 0x200000, L"Based" }, { 0x400000, L"No Change" } }));
		if (s.Image) {
			rows.Add(L"Machine", MachineToString(s.Machine));
			rows.Add(L"Subsystem", SubsystemToString(s.Subsystem));
			rows.Add(L"Entry Point", std::format(L"0x{:X}", s.EntryPoint));
			rows.Add(L"Image File Size", FormatSize(s.ImageFileSize));
		}
	}
	else if (d.File) {
		auto& f = *d.File;
		rows.Section(f.Pipe ? L"Named Pipe" : L"File");
		if (f.TimedOut) {
			rows.Add(L"State", L"Busy: the queries didn't return (synchronous I/O in progress)");
			return rows.Rows;
		}
		if (f.DeviceType) {
			std::wstring type = ObjectInspector::DeviceTypeToString(*f.DeviceType);
			rows.Add(L"Device Type", type.empty() ? std::format(L"0x{:X}", *f.DeviceType) : type);
			if (f.DeviceCharacteristics)
				rows.Add(L"Device Characteristics", FormatFlags(f.DeviceCharacteristics, { { FILE_REMOVABLE_MEDIA, L"Removable" },
					{ FILE_READ_ONLY_DEVICE, L"Read-only" }, { FILE_FLOPPY_DISKETTE, L"Floppy" }, { FILE_WRITE_ONCE_MEDIA, L"Write Once" },
					{ FILE_REMOTE_DEVICE, L"Remote" }, { FILE_DEVICE_IS_MOUNTED, L"Mounted" }, { FILE_VIRTUAL_VOLUME, L"Virtual" },
					{ FILE_DEVICE_SECURE_OPEN, L"Secure Open" }, { FILE_CHARACTERISTIC_PNP_DEVICE, L"PnP" },
					{ FILE_DEVICE_ALLOW_APPCONTAINER_TRAVERSAL, L"AppContainer Traversal" },
					{ FILE_PORTABLE_DEVICE, L"Portable" } }));
		}
		if (f.Mode) {
			auto mode = *f.Mode;
			std::wstring io = (mode & FILE_SYNCHRONOUS_IO_ALERT) ? L"Synchronous (alertable)"
				: (mode & FILE_SYNCHRONOUS_IO_NONALERT) ? L"Synchronous" : L"Asynchronous";
			auto flags = FormatFlags(mode & ~(FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT), { { FILE_WRITE_THROUGH, L"Write Through" },
				{ FILE_SEQUENTIAL_ONLY, L"Sequential" }, { FILE_NO_INTERMEDIATE_BUFFERING, L"No Buffering" }, { FILE_DELETE_ON_CLOSE, L"Delete on Close" } });
			rows.Add(L"I/O", flags.empty() ? io : io + L", " + flags);
			// the I/O manager keeps the position for synchronous files only
			if (f.Position && (mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT)) && !f.Pipe && !f.Directory)
				rows.Add(L"Position", FormatCount(*f.Position));
		}
		if (f.Pipe) {
			auto& p = *f.Pipe;
			rows.Add(L"End", p.End == FILE_PIPE_SERVER_END ? L"Server" : L"Client");
			rows.Add(L"State", p.State == FILE_PIPE_DISCONNECTED_STATE ? L"Disconnected" : p.State == FILE_PIPE_LISTENING_STATE ? L"Listening"
				: p.State == FILE_PIPE_CONNECTED_STATE ? L"Connected" : p.State == FILE_PIPE_CLOSING_STATE ? L"Closing" : L"");
			rows.Add(L"Mode", std::wstring(p.Type == FILE_PIPE_MESSAGE_TYPE ? L"Message" : L"Byte stream") + L", "
				+ (p.Configuration == FILE_PIPE_INBOUND ? L"inbound" : p.Configuration == FILE_PIPE_OUTBOUND ? L"outbound" : L"duplex"));
			rows.Add(L"Instances", p.MaximumInstances == FILE_PIPE_UNLIMITED_INSTANCES ? std::format(L"{} (unlimited)", p.CurrentInstances)
				: std::format(L"{} of {}", p.CurrentInstances, p.MaximumInstances));
			rows.Add(L"Read Data Available", FormatCount(p.ReadDataAvailable));
			rows.Add(L"Inbound Quota", FormatCount(p.InboundQuota));
			rows.Add(L"Outbound Quota", std::format(L"{} ({} available)", FormatCount(p.OutboundQuota), FormatCount(p.WriteQuotaAvailable)));
			return rows.Rows;
		}
		if (f.Directory)
			rows.Add(L"Directory", L"Yes");
		else if (f.Size) {
			rows.Add(L"Size", FormatSize(*f.Size));
			rows.Add(L"Allocation", FormatSize(*f.Allocation));
		}
		if (f.Links > 1)
			rows.Add(L"Hard Links", FormatCount(f.Links));
		if (f.DeletePending)
			rows.Add(L"Delete Pending", L"Yes");
	}
	else if (d.Key) {
		rows.Section(L"Key");
		rows.Add(L"Last Written", FormatHelper::FormatDateTime(d.Key->LastWriteTime));
		if (d.Key->SubKeys)
			rows.Add(L"Subkeys", FormatCount(*d.Key->SubKeys));
		if (d.Key->Values)
			rows.Add(L"Values", FormatCount(*d.Key->Values));
	}
	else if (d.SymbolicLink) {
		rows.Section(L"Symbolic Link");
		rows.Add(L"Target", d.SymbolicLink->Target);
	}
	else if (d.Process) {
		auto& p = *d.Process;
		rows.Section(L"Process");
		rows.Add(L"Process", ProcessText(p.Id));
		rows.Add(L"Image Path", p.ImagePath);
		rows.Add(L"Started", FormatHelper::FormatDateTime(p.CreateTime));
		rows.Add(L"State", FormatExitCode(p.ExitCode));
	}
	else if (d.Thread) {
		auto& t = *d.Thread;
		rows.Section(L"Thread");
		rows.Add(L"Thread ID", std::to_wstring(t.Id));
		rows.Add(L"Process", ProcessText(t.ProcessId));
		rows.Add(L"Started", FormatHelper::FormatDateTime(t.CreateTime));
		rows.Add(L"State", FormatExitCode(t.ExitCode));
	}
	else if (d.Token) {
		auto& t = *d.Token;
		rows.Section(L"Token");
		rows.Add(L"User", t.User.empty() ? t.UserSid : std::format(L"{} ({})", t.User, t.UserSid));
		rows.Add(L"Logon Session", std::format(L"{:X}:{:X}", t.LogonSession.HighPart, t.LogonSession.LowPart));
		rows.Add(L"Session", std::to_wstring(t.SessionId));
		rows.Add(L"Kind", t.Primary ? std::wstring(L"Primary") : std::format(L"Impersonation ({})", ImpersonationLevelToString(t.ImpersonationLevel)));
		rows.Add(L"Integrity", FormatHelper::IntegrityLevelToString(t.Integrity));
		rows.Add(L"Elevated", t.Elevated ? L"Yes" : L"No");
	}
	else if (d.Job) {
		auto& j = *d.Job;
		rows.Section(L"Job");
		if (!j.Name.empty())
			rows.Add(L"Name", j.Name);
		rows.Add(L"Active Processes", FormatCount(j.ActiveProcesses));
		rows.Add(L"Total Processes", FormatCount(j.TotalProcesses));
		rows.Add(L"Terminated Processes", FormatCount(j.TerminatedProcesses));
	}
	else {
		// these types' details need access the handle doesn't have (a section's SECTION_QUERY, an event's QUERY_STATE...)
		static PCWSTR const queried[] = { L"Event", L"Mutant", L"Semaphore", L"Timer", L"IoCompletion", L"Section", L"Key",
			L"SymbolicLink", L"Token", L"Job", L"Process", L"Thread" };
		if (std::ranges::contains(queried, m_Type, [](PCWSTR type) { return std::wstring_view(type); })) {
			rows.Section(m_Type);
			rows.Add(L"Not Available", L"The handle's access doesn't allow the query");
		}
	}
	if (title.empty())
		title = L"Details";
	if (rows.Rows.empty())
		rows.Add(L"No Details", L"Nothing more is known of this type's objects");
	return rows.Rows;
}

CHandleListPage::CHandleListPage(CHandleSheet& sheet, PCWSTR title) : m_Sheet(sheet) {
	SetPageTitle(title);
}

void CHandleListPage::SetPageTitle(std::wstring title) {
	m_Title = std::move(title);
	SetTitle(m_Title.c_str());
}

void CHandleListPage::SetPageIcon(UINT id) {
	m_psp.dwFlags |= PSP_USEICONID;
	m_psp.pszIcon = MAKEINTRESOURCE(id);
}

void CHandleListPage::SetRows(std::vector<Row> rows) {
	m_Rows = std::move(rows);
	if (m_hWnd)
		Show();
}

LRESULT CHandleListPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_List.Attach(GetDlgItem(IDC_HANDLEPROPS_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	auto dpi = ::GetDpiForWindow(m_hWnd);
	m_List.InsertColumn(0, L"Property", LVCFMT_LEFT, ::MulDiv(180, dpi, 96));
	m_List.InsertColumn(1, L"Value", LVCFMT_LEFT, ::MulDiv(300, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(m_List);

	// the sections' titles
	LOGFONT lf;
	CFontHandle(m_List.GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&lf);

	DlgResize_Init(false, false, 0);
	InitDpi();
	Show();
	return TRUE;
}

LRESULT CHandleListPage::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// the controls first
	CDialogResize<CHandleListPage>::OnSize(uMsg, wParam, lParam, bHandled);
	FitValueColumn();
	return 0;
}

void CHandleListPage::FitValueColumn() {
	if (!m_List)
		return;
	CRect rc;
	m_List.GetClientRect(&rc);
	m_List.SetColumnWidth(1, (std::max)(rc.Width() - m_List.GetColumnWidth(0), ::MulDiv(100, ::GetDpiForWindow(m_hWnd), 96)));
}

void CHandleListPage::Show() {
	int top = m_List.GetTopIndex();
	// a refresh keeps the selected row, by its place and name
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	std::wstring selectedName = selected >= 0 && selected < (int)m_Rows.size() ? m_Rows[selected].Name : L"";
	m_List.SetRedraw(FALSE);
	m_List.DeleteAllItems();
	for (auto& row : m_Rows) {
		int n = m_List.InsertItem(m_List.GetItemCount(), row.Name.c_str());
		m_List.SetItemText(n, 1, row.Value.c_str());
	}
	// a refresh keeps the place
	if (top > 0 && top < m_List.GetItemCount()) {
		m_List.EnsureVisible(m_List.GetItemCount() - 1, FALSE);
		m_List.EnsureVisible(top, FALSE);
	}
	if (selected >= 0 && selected < (int)m_Rows.size() && m_Rows[selected].Name == selectedName)
		m_List.SetItemState(selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.SetRedraw(TRUE);
	// the rows may have brought the vertical scroll bar (shown once redrawing is on)
	FitValueColumn();
	UpdateCopyButtons();
}

LRESULT CHandleListPage::OnCustomDraw(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
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

LRESULT CHandleListPage::OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	m_Sheet.Refresh();
	return 0;
}

LRESULT CHandleListPage::OnCopy(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	Copy(wID);
	return 0;
}

LRESULT CHandleListPage::OnItemChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	if (reinterpret_cast<NMLISTVIEW*>(hdr)->uChanged & LVIF_STATE)
		UpdateCopyButtons();
	return 0;
}

LRESULT CHandleListPage::OnListKeyDown(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
	if (reinterpret_cast<NMLVKEYDOWN*>(hdr)->wVKey == 'C' && ::GetKeyState(VK_CONTROL) < 0)
		Copy(IDC_HANDLEPROPS_COPY);
	return 0;
}

LRESULT CHandleListPage::OnContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_List) {
		bHandled = FALSE;
		return 0;
	}
	CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: below the selected row, else the list's corner
		CRect rc;
		int row = m_List.GetNextItem(-1, LVNI_SELECTED);
		if (row < 0 || !m_List.GetItemRect(row, &rc, LVIR_LABEL))
			rc.SetRectEmpty();
		pt = { rc.left, rc.bottom };
		m_List.ClientToScreen(&pt);
	}
	else {
		// the header's menu comes here too
		CRect header;
		m_List.GetHeader().GetWindowRect(&header);
		if (header.PtInRect(pt))
			return 0;
		CPoint client(pt);
		m_List.ScreenToClient(&client);
		if (int row = m_List.HitTest(client, nullptr); row >= 0)
			m_List.SelectItem(row);
	}
	CMenu menu;
	menu.CreatePopupMenu();
	for (auto [id, text] : { std::pair{ IDC_HANDLEPROPS_COPY, L"&Copy" }, std::pair{ IDC_HANDLEPROPS_COPYVALUE, L"Copy &Value" },
		std::pair{ IDC_HANDLEPROPS_COPYALL, L"Copy &All" } })
		menu.AppendMenu(MF_STRING | (CopyText(id).empty() ? MF_GRAYED : 0), id, text);
	if (auto id = (UINT)menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd))
		Copy(id);
	return 0;
}

std::wstring CHandleListPage::CopyText(UINT id) const {
	if (id == IDC_HANDLEPROPS_COPYALL) {
		// the sections as [titles], the properties tab separated
		std::wstring text;
		for (auto& row : m_Rows) {
			if (row.Header)
				text += (text.empty() ? L"[" : L"\r\n[") + row.Name + L"]\r\n";
			else
				text += row.Name + L"\t" + row.Value + L"\r\n";
		}
		return text;
	}
	int selected = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (selected < 0 || selected >= (int)m_Rows.size())
		return L"";
	auto& row = m_Rows[selected];
	if (id == IDC_HANDLEPROPS_COPYVALUE)
		return row.Header ? L"" : row.Value;
	return row.Header ? L"[" + row.Name + L"]" : row.Name + L"\t" + row.Value;
}

void CHandleListPage::Copy(UINT id) {
	if (auto text = CopyText(id); !text.empty())
		ClipboardHelper::CopyText(m_hWnd, text.c_str());
}

void CHandleListPage::UpdateCopyButtons() {
	for (UINT id : { IDC_HANDLEPROPS_COPY, IDC_HANDLEPROPS_COPYVALUE, IDC_HANDLEPROPS_COPYALL })
		GetDlgItem(id).EnableWindow(!CopyText(id).empty());
}
