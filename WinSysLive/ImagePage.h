#pragma once

#include "resource.h"
#include "ProcessPages.h"
#include "DpiLayout.h"
#include "PEView.h"

//
// The process sheet's Image page: the process's executable as a PE file (CPEView, read when the page is first shown:
// Headers, Sections, Imports, Exports, Debug, Open in TotalPE, Copy).
//
class CProcImagePage :
	public CPropertyPageImpl<CProcImagePage>,
	public CDpiDynamicDialogLayout<CProcImagePage> {
public:
	enum { IDD = IDD_PROC_IMAGE };

	explicit CProcImagePage(ProcessProperties const& props) : m_Props(props) {}

	BEGIN_MSG_MAP(CProcImagePage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_HANDLER(IDC_PE_VIEW, CBN_SELCHANGE, OnViewChanged)
		COMMAND_ID_HANDLER(IDC_PE_TOTALPE, OnTotalPE)
		COMMAND_ID_HANDLER(IDC_PE_COPY, OnCopy)
		NOTIFY_HANDLER(IDC_PE_LIST, LVN_COLUMNCLICK, OnColumnClick)
		CHAIN_MSG_MAP(CDpiDynamicDialogLayout<CProcImagePage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcImagePage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		m_View.Init(m_hWnd, m_Props.ImagePath);
		InitDynamicLayout(false, false);
		// the layout holds the list's window, which the view replaces leaving the Icons view
		m_View.ListReplaced = [this](HWND oldList, HWND newList) {
			for (int i = 0; i < m_arrLayoutData.GetSize(); i++)
				if (m_arrLayoutData[i].m_hWnd == oldList)
					m_arrLayoutData[i].m_hWnd = newList;
			};
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
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/) {
		m_View.OnColumnClick(reinterpret_cast<NMLISTVIEW*>(pnmh)->iSubItem);
		return 0;
	}

	ProcessProperties const& m_Props;
	CPEView m_View;
};
