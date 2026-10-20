#pragma once

#include "resource.h"
#include "Symbols.h"

//
// Options > Symbol Settings: where symbols are looked for (SymbolPathOptions, kept in the Symbol* settings), with the
// resulting search path shown as it's edited. OK saves the settings; the caller hands the new path to the engine.
//
class CSymbolSettingsDlg : public CDialogImpl<CSymbolSettingsDlg> {
public:
	enum { IDD = IDD_SYMBOLS };

	// the options in the settings
	static SymbolPathOptions LoadOptions();

	BEGIN_MSG_MAP(CSymbolSettingsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_SYM_BROWSE, OnBrowse)
		COMMAND_ID_HANDLER(IDC_DEFAULTS, OnDefaults)
		COMMAND_ID_HANDLER(IDC_SYM_USE_ENV, OnChanged)
		COMMAND_ID_HANDLER(IDC_SYM_USE_SERVER, OnChanged)
		COMMAND_HANDLER(IDC_SYM_SERVER_URL, EN_CHANGE, OnChanged)
		COMMAND_HANDLER(IDC_SYM_CACHE, EN_CHANGE, OnChanged)
		COMMAND_HANDLER(IDC_SYM_EXTRA, EN_CHANGE, OnChanged)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Show(SymbolPathOptions const& options);
	SymbolPathOptions GetOptions() const;
	std::wstring GetText(UINT id) const;
	// enables the server's fields, and shows the resulting path
	void UpdateUI();
};
