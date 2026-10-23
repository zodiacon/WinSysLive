#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Firmware.h>

//
// Tools > Firmware and TPM: a modal, resizable dialog with two tabs, read-only. Summary: the firmware type, Secure
// Boot, the boot timeout and the boot entries (BootOrder's, the current and next marked), and the TPM's version,
// interface and identity (WinSys Firmware and Tpm), in sections drawn bold and shaded. UEFI Variables: every variable
// with its vendor, attributes and size, the selected one's value as a hex dump below. The variables need
// SeSystemEnvironmentPrivilege, so elevation; the status line says why they're missing otherwise. Refresh reads all
// again; Copy copies the summary, or the selected variable with its dump.
//
class CFirmwareDlg :
	public CDialogImpl<CFirmwareDlg>,
	public CDialogResize<CFirmwareDlg>,
	public CDpiDialogResize<CFirmwareDlg> {
public:
	enum { IDD = IDD_FIRMWARE };

	BEGIN_DLGRESIZE_MAP(CFirmwareDlg)
		DLGRESIZE_CONTROL(IDC_FW_TABS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_FW_SUMMARY, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_FW_VARS, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_FW_VALUE, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FW_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FW_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FW_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CFirmwareDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		NOTIFY_HANDLER(IDC_FW_TABS, TCN_SELCHANGE, OnTabChanged)
		NOTIFY_HANDLER(IDC_FW_SUMMARY, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_FW_VARS, LVN_ITEMCHANGED, OnVariableChanged)
		COMMAND_ID_HANDLER(IDC_FW_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_FW_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CFirmwareDlg>)
		CHAIN_MSG_MAP(CDialogResize<CFirmwareDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	struct Row {
		std::wstring Name, Value;
		bool Header{ false };	// a section's title
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnVariableChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// reads the firmware's variables and the TPM again, into both tabs
	void Load();
	void ShowTab(int tab);
	// the selected variable's value, as hex and ASCII, 16 bytes a line
	void ShowValue();
	WinSys::UefiVariable const* GetSelectedVariable() const;
	static std::wstring VendorName(GUID const& vendor);
	static std::wstring AttributesText(uint32_t attributes);
	static std::wstring HexDump(std::vector<uint8_t> const& data);

	CTabCtrl m_Tabs;
	CListViewCtrl m_Summary, m_Variables;
	CEdit m_Value;
	CFont m_BoldFont, m_HexFont;
	std::vector<Row> m_Rows;	// the summary's
	std::vector<WinSys::UefiVariable> m_Vars;	// each variable row's item data is its index here
};
