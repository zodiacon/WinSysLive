#pragma once

#include "resource.h"
#include "DpiLayout.h"

//
// System > System Information: a modal, resizable dialog, read-only, no elevation needed. A tab per area, each a list of
// properties (some tabs grouped under bold, shaded titles), from WinSys SystemDetails: Operating System (version, build,
// installation and boot times, uptime, computer name, domain or workgroup, locales), Computer (SMBIOS: system,
// baseboard, BIOS), Processor (CPUID, topology, caches, features, hypervisor), Memory (installed, usable, every populated
// slot's module) and Security (Secure Boot, code integrity and memory integrity, VBS and Credential Guard, the kernel
// debugger, the speculation mitigations, kernel shadow stacks, Kernel DMA Protection, LSA protection, UAC, Smart App
// Control). One list shows the selected tab's rows. Refresh reads all again; Copy copies the selected row, or the tab's
// rows with no selection.
//
class CSystemInfoDlg :
	public CDialogImpl<CSystemInfoDlg>,
	public CDialogResize<CSystemInfoDlg>,
	public CDpiDialogResize<CSystemInfoDlg> {
public:
	enum { IDD = IDD_SYSINFO };

	BEGIN_DLGRESIZE_MAP(CSystemInfoDlg)
		DLGRESIZE_CONTROL(IDC_SI_TABS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_SI_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_SI_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SI_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SI_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CSystemInfoDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_SI_LIST, NM_CUSTOMDRAW, OnListCustomDraw)
		NOTIFY_HANDLER(IDC_SI_TABS, TCN_SELCHANGE, OnTabChanged)
		COMMAND_ID_HANDLER(IDC_SI_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_SI_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CSystemInfoDlg>)
		CHAIN_MSG_MAP(CDialogResize<CSystemInfoDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Tab {
		Os, Computer, Processor, Memory, Security,
		_Count
	};

	struct Row {
		std::wstring Name, Value;
		bool Header{ false };	// a group's title
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnListCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// reads every tab again, keeping the selected row by its name
	void Load();
	// fills the list with the selected tab's rows; selects the row with this name, if any
	void ShowTab(std::wstring const& select = {});
	void AddOs();
	void AddComputer();
	void AddProcessor();
	void AddMemory();
	void AddSecurity();

	// the rows go to m_Building's tab
	void Group(PCWSTR title);
	void Add(std::wstring name, std::wstring value);
	std::vector<Row>& Rows() {
		return m_Rows[m_Tabs.GetCurSel() < 0 ? 0 : m_Tabs.GetCurSel()];
	}

	CTabCtrl m_Tabs;
	CListViewCtrl m_List;
	CFont m_BoldFont;
	std::vector<Row> m_Rows[(int)Tab::_Count];
	Tab m_Building{ Tab::Os };
};
