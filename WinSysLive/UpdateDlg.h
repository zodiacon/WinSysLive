#pragma once

#include "UpdateChecker.h"
#include "UpdateInstaller.h"

//
// A newer release found: Download and Install downloads and verifies it (UpdateInstaller::PrepareAsync) with a progress
// bar, then becomes Install and Restart, which ends the dialog with IDOK (the frame installs: GetFolder). Release Page
// opens the release on GitHub. Closing cancels a download (the thread ends by itself; its messages to a closed dialog
// are dropped).
//
class CUpdateDlg : public CDialogImpl<CUpdateDlg> {
public:
	enum { IDD = IDD_UPDATE };

	explicit CUpdateDlg(UpdateChecker::UpdateInfo const& info);

	std::wstring const& GetFolder() const {
		return m_Folder;
	}

	BEGIN_MSG_MAP(CUpdateDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(UpdateInstaller::WM_UPDATE_PROGRESS, OnProgress)
		MESSAGE_HANDLER(UpdateInstaller::WM_UPDATE_PREPARED, OnPrepared)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_UPDATE_PAGE, OnReleasePage)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnProgress(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnPrepared(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnReleasePage(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	enum class State { Offer, Downloading, Ready, Failed };
	void SetState(State state, std::wstring const& status = {});

	UpdateChecker::UpdateInfo m_Info;
	State m_State{ State::Offer };
	std::shared_ptr<std::atomic<bool>> m_Cancel;
	std::wstring m_Folder;
	CProgressBarCtrl m_Progress;
};
