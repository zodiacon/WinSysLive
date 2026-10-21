#include "pch.h"
#include "ServiceInstallDlg.h"
#include <WinSys/ServiceManager.h>
#include <WinSys/Service.h>
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

namespace {
	struct Type {
		PCWSTR Name;
		ServiceType Value;
	};
	const Type s_Types[] = {
		{ L"Service (own process)", ServiceType::Win32OwnProcess },
		{ L"Service (shared process)", ServiceType::Win32SharedProcess },
		{ L"Kernel driver", ServiceType::KernelDriver },
		{ L"File system driver", ServiceType::FileSystemDriver },
	};

	struct StartType {
		PCWSTR Name;
		ServiceStartType Value;
		bool Delayed;
		bool DriverOnly;
	};
	const StartType s_StartTypes[] = {
		{ L"Automatic", ServiceStartType::Auto, false, false },
		{ L"Automatic (Delayed Start)", ServiceStartType::Auto, true, false },
		{ L"Manual", ServiceStartType::Demand, false, false },
		{ L"Disabled", ServiceStartType::Disabled, false, false },
		{ L"Boot (drivers)", ServiceStartType::Boot, false, true },
		{ L"System (drivers)", ServiceStartType::System, false, true },
	};

	std::wstring GetText(CWindow win) {
		CString text;
		win.GetWindowText(text);
		text.Trim();
		return std::wstring(text);
	}
}

LRESULT CServiceInstallDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CComboBox type(GetDlgItem(IDC_SVCI_TYPE));
	for (auto& t : s_Types)
		type.AddString(t.Name);
	type.SetCurSel(m_Driver ? 2 : 0);
	CComboBox start(GetDlgItem(IDC_SVCI_STARTTYPE));
	for (auto& t : s_StartTypes)
		start.AddString(t.Name);
	// Manual
	start.SetCurSel(2);
	CComboBox error(GetDlgItem(IDC_SVCI_ERRORCONTROL));
	for (auto name : { L"Ignore", L"Normal", L"Severe", L"Critical" })
		error.AddString(name);
	error.SetCurSel((int)ServiceErrorControl::Normal);
	if (m_Driver)
		SetWindowText(L"New Driver");
	BOOL handled;
	OnTypeChanged(0, 0, nullptr, handled);
	CenterWindow(GetParent());
	GotoDlgCtrl(GetDlgItem(IDC_SVCI_NAME));
	return FALSE;
}

bool CServiceInstallDlg::IsDriverType() const {
	int sel = CComboBox(GetDlgItem(IDC_SVCI_TYPE)).GetCurSel();
	return sel >= 0 && (s_Types[sel].Value == ServiceType::KernelDriver || s_Types[sel].Value == ServiceType::FileSystemDriver);
}

LRESULT CServiceInstallDlg::OnTypeChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// a driver runs in the kernel: no account
	bool driver = IsDriverType();
	for (UINT id : { IDC_SVCI_ACCOUNT, IDC_SVCI_PASSWORD })
		GetDlgItem(id).EnableWindow(!driver);
	return 0;
}

LRESULT CServiceInstallDlg::OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		IsDriverType() ? L"Drivers (*.sys)\0*.sys\0All Files (*.*)\0*.*\0" : L"Programs (*.exe)\0*.exe\0All Files (*.*)\0*.*\0", m_hWnd);
	if (dlg.DoModal() != IDOK)
		return 0;
	std::wstring path = dlg.m_szFileName;
	// a path with spaces is quoted, or the SCM would look for a program at its first space
	if (path.find(L' ') != std::wstring::npos)
		path = L"\"" + path + L"\"";
	SetDlgItemText(IDC_SVCI_PATH, path.c_str());
	return 0;
}

bool CServiceInstallDlg::Invalid(UINT id, PCWSTR text) {
	AtlMessageBox(m_hWnd, text, IDR_MAINFRAME, MB_ICONWARNING);
	GotoDlgCtrl(GetDlgItem(id));
	return false;
}

LRESULT CServiceInstallDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ServiceInstallParams params{};
	params.ServiceName = GetText(GetDlgItem(IDC_SVCI_NAME));
	if (params.ServiceName.empty() || params.ServiceName.find_first_of(L"/\\") != std::wstring::npos) {
		Invalid(IDC_SVCI_NAME, L"Type the service's name (no slashes or backslashes).");
		return 0;
	}
	params.DisplayName = GetText(GetDlgItem(IDC_SVCI_DISPLAYNAME));
	if (params.DisplayName.empty())
		params.DisplayName = params.ServiceName;
	params.ImagePath = GetText(GetDlgItem(IDC_SVCI_PATH));
	if (params.ImagePath.empty()) {
		Invalid(IDC_SVCI_PATH, L"Type or browse for the service's executable (a driver's .sys file).");
		return 0;
	}
	// an unquoted path with spaces that is a file as it stands
	if (params.ImagePath[0] != L'"' && params.ImagePath.find(L' ') != std::wstring::npos
		&& ::GetFileAttributes(params.ImagePath.c_str()) != INVALID_FILE_ATTRIBUTES)
		params.ImagePath = L"\"" + params.ImagePath + L"\"";
	auto& type = s_Types[CComboBox(GetDlgItem(IDC_SVCI_TYPE)).GetCurSel()];
	auto& start = s_StartTypes[CComboBox(GetDlgItem(IDC_SVCI_STARTTYPE)).GetCurSel()];
	bool driver = IsDriverType();
	if (start.DriverOnly && !driver) {
		Invalid(IDC_SVCI_STARTTYPE, L"Only drivers can be loaded at boot or system start.");
		return 0;
	}
	if (start.Delayed && driver) {
		Invalid(IDC_SVCI_STARTTYPE, L"A driver can't have a delayed start.");
		return 0;
	}
	params.ServiceType = type.Value;
	params.StartupType = start.Value;
	params.DelayedAutoStart = start.Delayed;
	params.ErrorControl = (ServiceErrorControl)CComboBox(GetDlgItem(IDC_SVCI_ERRORCONTROL)).GetCurSel();
	if (!driver) {
		params.AccountName = GetText(GetDlgItem(IDC_SVCI_ACCOUNT));
		CString password;
		GetDlgItemText(IDC_SVCI_PASSWORD, password);
		params.Password = password;
	}
	params.Dependencies = GetText(GetDlgItem(IDC_SVCI_DEPENDENCIES));

	auto service = ServiceManager::Install(params);
	SecureZeroMemory(params.Password.data(), params.Password.size() * sizeof(WCHAR));
	if (!service) {
		auto error = ::GetLastError();
		auto text = std::format(L"Creating the service failed: {}", Helpers::GetErrorText(error));
		if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
			text += L"\nCreating services requires running as administrator (File > Run as Administrator).";
		AtlMessageBox(m_hWnd, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	m_Name = params.ServiceName;
	// created: what fails from here is reported, and the dialog closes
	std::wstring problems;
	if (auto description = GetText(GetDlgItem(IDC_SVCI_DESCRIPTION)); !description.empty() && !service->SetDescription(description))
		problems += std::format(L"\nSetting the description: {}", Helpers::GetErrorText(::GetLastError()));
	if (IsDlgButtonChecked(IDC_SVCI_START) == BST_CHECKED && !service->Start())
		problems += std::format(L"\nStarting it: {}", Helpers::GetErrorText(::GetLastError()));
	if (!problems.empty())
		AtlMessageBox(m_hWnd, std::format(L"Service {} was created, but:{}", m_Name, problems).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
	EndDialog(IDOK);
	return 0;
}

LRESULT CServiceInstallDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	EndDialog(wID);
	return 0;
}
