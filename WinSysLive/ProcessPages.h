#pragma once

#include "resource.h"

//
// What a process's property sheet shows of it: copied from the snapshot on the UI thread, because the sheet runs on
// a thread of its own. The pages query anything else from the process themselves.
//
struct ProcessProperties {
	uint32_t Id{ 0 };
	int64_t CreateTime{ 0 };
	uint32_t ParentId{ 0 };
	uint32_t SessionId{ 0 };
	std::wstring ImageName, ImagePath, CommandLine, User, Description;
	std::wstring ParentName;	// empty if the parent isn't running (or its ID was reused)
};

//
// The General page: identity and origin of the process
//
class CProcGeneralPage :
	public CPropertyPageImpl<CProcGeneralPage>,
	public CDialogResize<CProcGeneralPage> {
public:
	enum { IDD = IDD_PROC_GENERAL };

	CProcGeneralPage(ProcessProperties const& props, HICON hIcon);

	BEGIN_MSG_MAP(CProcGeneralPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		CHAIN_MSG_MAP(CDialogResize<CProcGeneralPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcGeneralPage>)
	END_MSG_MAP()

	BEGIN_DLGRESIZE_MAP(CProcGeneralPage)
		DLGRESIZE_CONTROL(IDC_PROC_NAME, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_DESCRIPTION, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_PATH, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_COMMANDLINE, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_PID, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_PARENT, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_USER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_SESSION, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_PROC_STARTED, DLSZ_SIZE_X)
	END_DLGRESIZE_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	ProcessProperties const& m_Props;
	HICON m_hIcon;	// the sheet's
};

//
// A page still to be implemented: its title, icon and a line saying what it will show
//
class CProcPlaceholderPage : public CPropertyPageImpl<CProcPlaceholderPage> {
public:
	enum { IDD = IDD_PROC_PAGE };

	// title: kept (not copied) until the page is created
	CProcPlaceholderPage(PCWSTR title, UINT icon, std::wstring text);

	BEGIN_MSG_MAP(CProcPlaceholderPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcPlaceholderPage>)
	END_MSG_MAP()

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	std::wstring m_Text;
};
