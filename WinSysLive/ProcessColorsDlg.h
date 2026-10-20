#pragma once

#include "resource.h"
#include "ProcessColors.h"

//
// Options > Process Colors: a row per process kind, in precedence order, with a check box to use its color and a swatch
// for each mode that opens the color dialog, and a button that restores the row's default colors. The swatches show
// sample text as the Processes view draws it.
//
class CProcessColorsDlg : public CDialogImpl<CProcessColorsDlg> {
public:
	enum { IDD = IDD_PROCESS_COLORS };

	// entries: as ProcessColors::Load returns them (in ProcessColors::Defaults order)
	explicit CProcessColorsDlg(std::vector<ProcessColors::Entry> entries);

	// after IDOK
	std::vector<ProcessColors::Entry> const& GetEntries() const;

	BEGIN_MSG_MAP(CProcessColorsDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
		COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
		COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
		COMMAND_ID_HANDLER(IDC_DEFAULTS, OnDefaults)
		COMMAND_RANGE_HANDLER(IDC_PCOLOR_LIGHT1, IDC_PCOLOR_LIGHT8, OnChooseColor)
		COMMAND_RANGE_HANDLER(IDC_PCOLOR_DARK1, IDC_PCOLOR_DARK8, OnChooseColor)
		COMMAND_RANGE_HANDLER(IDC_PCOLOR_DEFAULT1, IDC_PCOLOR_DEFAULT8, OnDefaultColors)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDrawItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDefaults(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChooseColor(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDefaultColors(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the entry's color a swatch shows, or nullptr if the ID isn't a swatch
	COLORREF* ColorOf(UINT id);
	static bool IsDarkSwatch(UINT id);
	void ShowEntries();
	// a row's Default button is enabled while its colors aren't the defaults
	void UpdateDefaultButton(int row);

	std::vector<ProcessColors::Entry> m_Entries, m_Defaults;
};
