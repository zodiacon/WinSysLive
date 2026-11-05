#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Certificates.h>
#include <functional>

//
// System > Certificate Stores: a modal, resizable dialog, read-only (WinSys Certificates). A tree of the Local Machine's
// and the Current User's certificate stores (the localized names, with the certificates' count and how many stand out)
// beside the selected store's certificates, or a whole location's (each store's), each from its physical store (the
// user's view of the machine's certificates left out: they're under Local Machine). The Concern column (Certificates::
// Assess, against Microsoft's root program and disallowed lists as Windows caches them) colors the row: red for a trusted
// root whose private key is on this machine, or a trusted certificate Microsoft disallows; orange for a root not in
// Microsoft's program (a company's, a tool's, or an attacker's); yellow for one added in the last 30 days to Root,
// Trusted Publishers or Trusted People (when Microsoft doesn't vouch for it); gray text for an expired one. Concerns
// Only (on at first) keeps the colored rows; the filter matches the names, the thumbprint, the serial number, the
// purposes and the concern. Any column sorts (the dates as dates). View Certificate (the double-click; the context
// menu) opens Windows' certificate dialog; Manager opens certlm.msc or certmgr.msc for the selected location; Copy
// copies the selected rows, or all shown; Copy Thumbprint.
//
class CCertificatesDlg :
	public CDialogImpl<CCertificatesDlg>,
	public CDialogResize<CCertificatesDlg>,
	public CDpiDialogResize<CCertificatesDlg> {
public:
	enum { IDD = IDD_CERTIFICATES };

	// images: the frame's (shared); iconIndex: an icon's index in it
	CCertificatesDlg(HIMAGELIST images, std::function<int(UINT)> iconIndex) : m_Images(images), m_IconIndex(std::move(iconIndex)) {}

	BEGIN_DLGRESIZE_MAP(CCertificatesDlg)
		DLGRESIZE_CONTROL(IDC_CE_TREE, DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_CE_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_CE_CONCERNS, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_CE_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_CE_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CE_VIEW, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CE_MANAGER, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CE_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CE_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CCertificatesDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_HANDLER(IDC_CE_TREE, TVN_SELCHANGED, OnTreeSelChanged)
		NOTIFY_HANDLER(IDC_CE_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_CE_LIST, NM_DBLCLK, OnDoubleClick)
		NOTIFY_HANDLER(IDC_CE_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_CE_LIST, LVN_ITEMCHANGED, OnItemChanged)
		COMMAND_HANDLER(IDC_CE_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_CE_CONCERNS, OnConcernsOnly)
		COMMAND_ID_HANDLER(IDC_CE_VIEW, OnView)
		COMMAND_ID_HANDLER(IDC_CE_MANAGER, OnManager)
		COMMAND_ID_HANDLER(IDC_CE_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_CE_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDC_CE_COPYTHUMBPRINT, OnCopyThumbprint)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CCertificatesDlg>)
		CHAIN_MSG_MAP(CDialogResize<CCertificatesDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column {
		Subject, Issuer, Expires, Concern, Store, Source, Added, Purposes, FriendlyName, Key, Signature, PrivateKey,
		ValidFrom, Thumbprint, Serial, _Count
	};

	struct Row {
		WinSys::CertificateInfo Cert;
		WinSys::CertificateAssessment Assessment;
		std::wstring StoreName;	// the store's display name
	};
	struct Store {
		WinSys::CertificateStoreInfo Info;
		std::vector<size_t> Rows;	// into m_Rows
		size_t Concerns{ 0 };
		DWORD Error{ 0 };	// it couldn't be opened
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTreeSelChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnConcernsOnly(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnView(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnManager(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopyThumbprint(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// both locations' stores read again, the tree rebuilt with the same item selected
	void Load();
	// the selected tree item: a store's index, or -1 with the location for a location's item
	std::pair<int, WinSys::CertificateLocation> GetSelection() const;
	// the rows of the selection the filter and Concerns Only keep, sorted
	void ShowRows();
	std::wstring GetText(Row const& row, Column column) const;
	Row const* GetSelectedRow() const;
	void UpdateStatus();

	HIMAGELIST m_Images;
	std::function<int(UINT)> m_IconIndex;
	CTreeViewCtrl m_Tree;
	CListViewCtrl m_List;
	std::vector<Row> m_Rows;
	std::vector<Store> m_Stores;
	std::vector<size_t> m_Shown;	// into m_Rows, in display order; the list's item data is the position here
	std::optional<WinSys::CertificateTrustList> m_Program, m_Disallowed;
	std::wstring m_Filter;	// lowercase
	bool m_ConcernsOnly{ true };
	int m_SortColumn{ (int)Column::Concern };
	bool m_SortAscending{ false };	// the worst first
};
