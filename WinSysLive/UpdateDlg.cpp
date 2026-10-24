#include "pch.h"
#include "resource.h"
#include "UpdateDlg.h"

CUpdateDlg::CUpdateDlg(UpdateChecker::UpdateInfo const& info) : m_Info(info) {
}

LRESULT CUpdateDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	SetIcon(AtlLoadIconImage(IDR_MAINFRAME, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
	SetIcon(AtlLoadIconImage(IDR_MAINFRAME, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
	CenterWindow(GetParent());
	m_Progress.Attach(GetDlgItem(IDC_UPDATE_PROGRESS));
	m_Progress.SetRange32(0, 1000);
	SetDlgItemText(IDC_UPDATE_TEXT, std::format(L"WinSysLive {} is available (you have {}).", m_Info.Latest, m_Info.Current).c_str());
	SetState(State::Offer);
	return TRUE;
}

void CUpdateDlg::SetState(State state, std::wstring const& status) {
	m_State = state;
	CButton ok(GetDlgItem(IDOK));
	m_Progress.ShowWindow(state == State::Downloading || state == State::Ready ? SW_SHOW : SW_HIDE);
	ok.EnableWindow(state != State::Downloading);
	ok.SetWindowText(state == State::Ready ? L"&Install and Restart" : state == State::Failed ? L"&Try Again" : L"&Download and Install");
	// the shield when installing will ask for elevation
	ok.SetElevationRequiredState(state == State::Ready && UpdateInstaller::NeedsElevation());
	SetDlgItemText(IDCANCEL, state == State::Downloading ? L"Cancel" : L"Close");
	auto text = status;
	if (text.empty() && state == State::Offer)
		text = L"Download and Install downloads the release from GitHub, checks its signature and restarts WinSysLive with it.";
	SetDlgItemText(IDC_UPDATE_STATUS, text.c_str());
}

LRESULT CUpdateDlg::OnProgress(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_State != State::Downloading)
		return 0;
	auto received = (uint64_t)wParam, total = (uint64_t)lParam;
	if (total)
		m_Progress.SetPos((int)(received * 1000 / total));
	SetDlgItemText(IDC_UPDATE_STATUS, total ? std::format(L"Downloading... {:.1f} of {:.1f} MB", received / 1048576.0, total / 1048576.0).c_str()
		: std::format(L"Downloading... {:.1f} MB", received / 1048576.0).c_str());
	return 0;
}

LRESULT CUpdateDlg::OnPrepared(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	std::unique_ptr<UpdateInstaller::Prepared> prepared(reinterpret_cast<UpdateInstaller::Prepared*>(lParam));
	if (m_State != State::Downloading)
		return 0;
	if (!prepared->Ok) {
		SetState(State::Failed, prepared->Error);
		return 0;
	}
	m_Folder = prepared->Folder;
	m_Progress.SetPos(1000);
	auto status = std::format(L"Version {}, signed by {}, is ready.", prepared->Version, prepared->Signer);
	if (UpdateInstaller::NeedsElevation())
		status += L" Installing it needs administrator rights.";
	SetState(State::Ready, status);
	return 0;
}

LRESULT CUpdateDlg::OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_State == State::Ready) {
		EndDialog(IDOK);
		return 0;
	}
	m_Cancel = std::make_shared<std::atomic<bool>>(false);
	m_Progress.SetPos(0);
	SetState(State::Downloading, L"Downloading...");
	UpdateInstaller::PrepareAsync(m_hWnd, m_Info.AssetUrl, m_Info.Latest, m_Cancel);
	return 0;
}

LRESULT CUpdateDlg::OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (m_Cancel)
		*m_Cancel = true;
	EndDialog(IDCANCEL);
	return 0;
}

LRESULT CUpdateDlg::OnReleasePage(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	if (!m_Info.Url.empty())
		::ShellExecute(m_hWnd, L"open", m_Info.Url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}
