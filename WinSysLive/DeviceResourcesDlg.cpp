#include "pch.h"
#include "DeviceResourcesDlg.h"
#include "FormatHelper.h"
#include "Signature.h"
#include <ListViewhelper.h>
#include <ClipboardHelper.h>
#include <WinSys/FileSignature.h>
#include <WinSys/Helpers.h>
#include <cfgmgr32.h>
#include <array>

using namespace WinSys;

namespace {
	enum ResourceColumn { ResType, ResSetting, ResSize, ResFlags };
	enum DriverColumn { DrvRole, DrvService, DrvDescription, DrvVersion, DrvCompany, DrvSignature, DrvPath };

	std::wstring Range(uint64_t start, uint64_t end, int digits) {
		return std::format(L"0x{:0{}X} - 0x{:0{}X}", start, digits, end, digits);
	}

	std::wstring MemoryFlags(uint32_t flags) {
		std::wstring text = (flags & mMD_MemoryType) == fMD_RAM ? L"Read/write" : L"Read-only";
		if ((flags & mMD_Readable) == fMD_ReadDisallowed)
			text = L"Write-only";
		if (flags & fMD_PrefetchAllowed)
			text += L", prefetchable";
		if (flags & fMD_CombinedWriteAllowed)
			text += L", write combining";
		if (flags & fMD_Cacheable)
			text += L", cacheable";
		if (flags & fMD_WINDOW_DECODE)
			text += L", bridge window";
		return text;
	}

	// a list's only row, a note in a wide column (the first is narrow)
	void Note(CListViewCtrl& list, int column, std::wstring const& text) {
		list.InsertItem(0, L"");
		list.SetItemText(0, column, text.c_str());
	}

	// the cells of a resource's row
	std::array<std::wstring, 4> Cells(DeviceResource const& r) {
		std::array<std::wstring, 4> cells;
		cells[ResType] = Devices::ResourceTypeName(r.Type);
		switch (r.Type) {
			case DeviceResourceType::Memory:
			case DeviceResourceType::LargeMemory:
				cells[ResSetting] = Range(r.Start, r.End, r.End > 0xFFFFFFFF ? 16 : 8);
				cells[ResSize] = FormatHelper::FormatBytes(r.End - r.Start + 1);
				cells[ResFlags] = MemoryFlags(r.Flags);
				break;
			case DeviceResourceType::Io:
				cells[ResSetting] = Range(r.Start, r.End, 4);
				cells[ResSize] = std::to_wstring(r.End - r.Start + 1) + L" ports";
				cells[ResFlags] = (r.Flags & fIOD_PortType) == fIOD_Memory ? L"Memory mapped" : L"";
				break;
			case DeviceResourceType::Interrupt:
				// a message-signaled interrupt's number is negative, as Device Manager shows it (its 32-bit form in hex)
				if ((int32_t)r.Value < 0) {
					cells[ResSetting] = std::format(L"{} (0x{:08X})", (int32_t)r.Value, r.Value);
					cells[ResFlags] = L"Message signaled";
				}
				else {
					cells[ResSetting] = std::to_wstring(r.Value);
					cells[ResFlags] = std::wstring((r.Flags & mIRQD_Edge_Level) == fIRQD_Edge ? L"Edge" : L"Level")
						+ ((r.Flags & mIRQD_Share) == fIRQD_Share ? L", shared" : L", exclusive");
				}
				break;
			case DeviceResourceType::Dma:
				cells[ResSetting] = std::to_wstring(r.Value);
				break;
			case DeviceResourceType::BusNumber:
				cells[ResSetting] = r.Start == r.End ? std::to_wstring(r.Start) : std::format(L"{} - {}", r.Start, r.End);
				cells[ResSize] = std::to_wstring(r.End - r.Start + 1);
				break;
		}
		return cells;
	}
}

LRESULT CDeviceResourcesDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDI_DEVICE, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDI_DEVICE, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
	SetWindowText(std::format(L"Resources and Drivers: {}", m_Device.Name).c_str());
	// the title: the dialog's font, bold and larger
	LOGFONT lf;
	CFontHandle(GetFont()).GetLogFont(&lf);
	lf.lfWeight = FW_SEMIBOLD;
	lf.lfHeight = lf.lfHeight * 5 / 4;
	m_TitleFont.CreateFontIndirect(&lf);
	GetDlgItem(IDC_DR_TITLE).SetFont(m_TitleFont);
	SetDlgItemText(IDC_DR_TITLE, m_Device.Name.c_str());
	SetDlgItemText(IDC_DR_ID, m_Device.InstanceId.c_str());
	if (m_Icon)
		CStatic(GetDlgItem(IDC_DR_ICON)).SetIcon(m_Icon);

	m_Resources.Attach(GetDlgItem(IDC_DR_RESOURCES));
	m_DriverList.Attach(GetDlgItem(IDC_DR_DRIVERS));
	InitColumns(m_Resources, { { L"Resource", 90 }, { L"Setting", 280 }, { L"Size", 90 }, { L"Flags", 300 } });
	InitColumns(m_DriverList, { { L"Role", 120 }, { L"Service", 100 }, { L"Description", 200 }, { L"Version", 120 },
		{ L"Company", 150 }, { L"Signature", 220 }, { L"Path", 400 } });

	DlgResize_Init(true, true);
	InitDpi();
	CenterWindow(GetParent());
	LoadResources();
	LoadDrivers();
	GetDlgItem(IDC_DR_PROPERTIES).EnableWindow(m_Device.Present && m_Device.Depth > 0);
	return TRUE;
}

void CDeviceResourcesDlg::InitColumns(CListViewCtrl& list, std::initializer_list<std::pair<PCWSTR, int>> columns) {
	list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
	auto dpi = ::GetDpiForWindow(list.GetParent());
	int i = 0;
	for (auto& [name, width] : columns)
		list.InsertColumn(i++, name, LVCFMT_LEFT, ::MulDiv(width, dpi, 96));
	ListViewHelper::EnableColumnDpiScaling(list);
}

void CDeviceResourcesDlg::LoadResources() {
	auto resources = Devices::GetResources(m_Device.InstanceId);
	if (!resources) {
		Note(m_Resources, ResSetting, L"(can't be read: " + Helpers::GetErrorText(::GetLastError()) + L")");
		return;
	}
	if (resources->empty()) {
		Note(m_Resources, ResSetting, m_Device.Present ? L"(none: the device uses no hardware resources)" : L"(none: the device isn't connected)");
		return;
	}
	int row = 0;
	for (auto& r : *resources) {
		auto cells = Cells(r);
		m_Resources.InsertItem(row, cells[0].c_str());
		for (int c = 1; c < (int)cells.size(); c++)
			m_Resources.SetItemText(row, c, cells[c].c_str());
		row++;
	}
}

void CDeviceResourcesDlg::LoadDrivers() {
	auto stack = Devices::GetDriverStack(m_Device.InstanceId);
	if (!stack) {
		Note(m_DriverList, DrvPath, L"(can't be read: " + Helpers::GetErrorText(::GetLastError()) + L")");
		return;
	}
	m_Drivers = std::move(*stack);
	if (m_Drivers.empty()) {
		Note(m_DriverList, DrvPath, L"(none: no driver is installed for the device)");
		return;
	}
	std::vector<std::wstring> paths;
	for (int i = 0; i < (int)m_Drivers.size(); i++) {
		auto& d = m_Drivers[i];
		m_DriverList.InsertItem(i, Devices::DriverRoleName(d.Role));
		m_DriverList.SetItemText(i, DrvService, d.Service.c_str());
		m_DriverList.SetItemText(i, DrvPath, d.Path.empty() ? L"(no such service)" : d.Path.c_str());
		if (!d.Path.empty())
			m_DriverList.SetItemText(i, DrvSignature, L"Checking...");
		paths.push_back(d.Path);
	}
	// the files' versions and signatures on a thread (a catalog check can take a moment each)
	m_Worker = std::thread([this, paths = std::move(paths), hWnd = m_hWnd]() {
		::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		for (int i = 0; i < (int)paths.size() && !m_Cancel; i++) {
			if (paths[i].empty())
				continue;
			auto info = new FileInfo;
			info->Version = FileVersion::Read(paths[i]);
			info->Signature = ::GetFileAttributesW(paths[i].c_str()) == INVALID_FILE_ATTRIBUTES ? L"(file not found)"
				: FormatSignature(FileSignature::Verify(paths[i].c_str()));
			if (m_Cancel || !::PostMessage(hWnd, WM_FILEINFO, i, reinterpret_cast<LPARAM>(info)))
				delete info;
		}
		::CoUninitialize();
	});
}

LRESULT CDeviceResourcesDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	m_Cancel = true;
	if (m_Worker.joinable())
		m_Worker.join();
	// the details posted before the thread saw the cancel
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, WM_FILEINFO, WM_FILEINFO, PM_REMOVE))
		delete reinterpret_cast<FileInfo*>(msg.lParam);
	bHandled = FALSE;
	return 0;
}

LRESULT CDeviceResourcesDlg::OnFileInfo(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
	std::unique_ptr<FileInfo> info(reinterpret_cast<FileInfo*>(lParam));
	int row = (int)wParam;
	m_DriverList.SetItemText(row, DrvDescription, info->Version.Description.c_str());
	m_DriverList.SetItemText(row, DrvVersion, info->Version.Version.c_str());
	m_DriverList.SetItemText(row, DrvCompany, info->Version.Company.c_str());
	m_DriverList.SetItemText(row, DrvSignature, info->Signature.c_str());
	return 0;
}

int CDeviceResourcesDlg::DriverRow() const {
	if (m_Drivers.empty())
		return -1;
	if (auto selected = m_DriverList.GetNextItem(-1, LVNI_SELECTED); selected >= 0 && selected < (int)m_Drivers.size())
		return selected;
	auto it = std::ranges::find_if(m_Drivers, [](auto& d) { return d.Role == DriverRole::Function; });
	return it == m_Drivers.end() ? -1 : (int)(it - m_Drivers.begin());
}

LRESULT CDeviceResourcesDlg::OnDriverDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	if (m_DriverList.GetNextItem(-1, LVNI_SELECTED) >= 0)
		PostMessage(WM_COMMAND, IDC_DR_GOTODRIVER);
	return 0;
}

LRESULT CDeviceResourcesDlg::OnDriverChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
	auto row = DriverRow();
	GetDlgItem(IDC_DR_GOTODRIVER).EnableWindow(row >= 0 && !m_Drivers[row].Path.empty());
	return 0;
}

LRESULT CDeviceResourcesDlg::OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// Device Manager's property sheet for the device
	auto args = std::format(L"devmgr.dll,DeviceProperties_RunDLL /DeviceID {}", m_Device.InstanceId);
	::ShellExecute(m_hWnd, nullptr, L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	return 0;
}

LRESULT CDeviceResourcesDlg::OnGoToDriver(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto row = DriverRow();
	if (row < 0 || m_Drivers[row].Path.empty() || !m_GoToDriver)
		return 0;
	auto path = m_Drivers[row].Path;
	EndDialog(IDOK);
	m_GoToDriver(path);
	return 0;
}

std::wstring CDeviceResourcesDlg::ListText(CListViewCtrl& list) {
	std::wstring text;
	auto header = list.GetHeader();
	int columns = header.GetItemCount();
	for (int c = 0; c < columns; c++) {
		WCHAR name[64]{};
		HDITEM hdi{ HDI_TEXT };
		hdi.pszText = name;
		hdi.cchTextMax = _countof(name);
		header.GetItem(c, &hdi);
		text += (c ? L"\t" : L"") + std::wstring(name);
	}
	text += L"\r\n";
	for (int i = 0; i < list.GetItemCount(); i++) {
		for (int c = 0; c < columns; c++) {
			CString cell;
			list.GetItemText(i, c, cell);
			text += (c ? L"\t" : L"") + std::wstring(cell);
		}
		text += L"\r\n";
	}
	return text;
}

LRESULT CDeviceResourcesDlg::OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	auto text = std::format(L"{}\r\n{}\r\n\r\nResources:\r\n{}\r\nDrivers:\r\n{}", m_Device.Name, m_Device.InstanceId,
		ListText(m_Resources), ListText(m_DriverList));
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CDeviceResourcesDlg::OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
