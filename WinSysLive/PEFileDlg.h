#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include "PEView.h"

//
// A module's image details (the Modules list's Image Details...): a modal, resizable dialog over CPEView, the Image
// page's views of a PE file (Headers, Sections, Imports, Exports, Debug), Open in TotalPE, Copy and Close. The title
// names the file.
//
class CPEFileDlg :
	public CDialogImpl<CPEFileDlg>,
	public CDialogResize<CPEFileDlg>,
	public CDpiDialogResize<CPEFileDlg> {
public:
	enum { IDD = IDD_PEFILE };

	explicit CPEFileDlg(std::wstring path) : m_Path(std::move(path)) {}

	BEGIN_DLGRESIZE_MAP(CPEFileDlg)
		DLGRESIZE_CONTROL(IDC_PE_SUMMARY, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PE_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_PE_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PE_TOTALPE, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_PE_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CPEFileDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_PE_VIEW, CBN_SELCHANGE, OnViewChanged)
		COMMAND_ID_HANDLER(IDC_PE_TOTALPE, OnTotalPE)
		COMMAND_ID_HANDLER(IDC_PE_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		NOTIFY_HANDLER(IDC_PE_LIST, LVN_COLUMNCLICK, OnColumnClick)
		CHAIN_MSG_MAP(CDpiDialogResize<CPEFileDlg>)
		CHAIN_MSG_MAP(CDialogResize<CPEFileDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		SetIcon(AtlLoadIconImage(IDI_MODULES, LR_SHARED, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)), TRUE);
		SetIcon(AtlLoadIconImage(IDI_MODULES, LR_SHARED, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)), FALSE);
		auto name = m_Path.substr(m_Path.rfind(L'\\') + 1);
		SetWindowText(std::format(L"{} - Image Details", name).c_str());
		DlgResize_Init(true, true);
		InitDpi();
		m_View.Init(m_hWnd, m_Path);
		CenterWindow(GetParent());
		return TRUE;
	}
	LRESULT OnViewChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		m_View.OnViewChanged();
		return 0;
	}
	LRESULT OnTotalPE(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		m_View.OnTotalPE();
		return 0;
	}
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		m_View.OnCopy();
		return 0;
	}
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		EndDialog(wID);
		return 0;
	}
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
		m_View.OnColumnClick(reinterpret_cast<NMLISTVIEW*>(pnmh)->iSubItem);
		return 0;
	}

	std::wstring m_Path;
	CPEView m_View;
};
