#pragma once

#include "resource.h"
#include <ResizablePropertySheet.h>
#include <IconPropertySheet.h>
#include <WinSys/Handles.h>
#include <WinSys/ObjectDetails.h>
#include "ObjectSecurity.h"
#include "DpiLayout.h"
#include <functional>

class CHandleSheet;

//
// The General and Details tabs of a handle's properties: a property list (bold, shaded rows title its sections) with
// Refresh, which queries the object again for both tabs, and Copy.
//
class CHandleListPage :
	public CPropertyPageImpl<CHandleListPage>,
	public CDialogResize<CHandleListPage>,
	public CDpiDialogResizePage<CHandleListPage> {
public:
	enum { IDD = IDD_HANDLE_PAGE };

	struct Row {
		std::wstring Name, Value;
		bool Header{ false };	// a section's title
	};

	CHandleListPage(CHandleSheet& sheet, PCWSTR title);
	// the rows to show, now or once the page is created
	void SetRows(std::vector<Row> rows);
	// before the page is added to the sheet
	void SetPageTitle(std::wstring title);
	// the tab's icon (IDI_*), before the page is added to the sheet
	void SetPageIcon(UINT id);

	BEGIN_DLGRESIZE_MAP(CHandleListPage)
		DLGRESIZE_CONTROL(IDC_HANDLEPROPS_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_HANDLEPROPS_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_HANDLEPROPS_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CHandleListPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		NOTIFY_HANDLER(IDC_HANDLEPROPS_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(IDC_HANDLEPROPS_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_HANDLEPROPS_COPY, OnCopy)
		CHAIN_MSG_MAP(CDpiDialogResizePage<CHandleListPage>)
		CHAIN_MSG_MAP(CDialogResize<CHandleListPage>)
		CHAIN_MSG_MAP(CPropertyPageImpl<CHandleListPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Show();
	// the last column takes the rest of the list's width
	void FitValueColumn();

	CHandleSheet& m_Sheet;
	std::wstring m_Title;
	std::vector<Row> m_Rows;
	CListViewCtrl m_List;
	CFont m_BoldFont;
};

//
// A handle's properties: a modal, resizable property sheet with tab icons and three tabs: General (the handle, the object's counts,
// its type's statistics: what every object has), Security (the system's ACL editor, CreateSecurityPage) and the type's
// own details (an event's state, a mutex's owner, a section's size, a file's mode and position, a pipe's ends...), titled
// by the type. The sheet and the details tab have the type's icon (GetHandleIcon), General the handles icon, Security
// the system's shield (an HPROPSHEETPAGE has no icon the sheet knows, so it's set once the sheet is initialized).
// The ACL editor's own dialogs get dark title bars from CAclDialogTheming.
// The object is queried once (WinSys ObjectInspector::QueryHandle) and again on Refresh.
//
class CHandleSheet :
	public CIconPropertySheetImpl<CHandleSheet, CResizablePropertySheetImpl<CHandleSheet>> {
	using BaseSheet = CIconPropertySheetImpl<CHandleSheet, CResizablePropertySheetImpl<CHandleSheet>>;
public:
	// hProcess: the handle's process, with PROCESS_DUP_HANDLE (it must stay valid while the sheet runs); name: the
	// handle's name as the list shows it; processName: the image name of a process ID (from the snapshot), for the
	// PIDs the details mention
	CHandleSheet(HANDLE hProcess, uint32_t pid, std::wstring processImage, WinSys::HandleEntry const& entry,
		std::wstring name, std::function<std::wstring(DWORD)> processName);
	~CHandleSheet();

	// queries the object again and refills the General and Details tabs
	void Refresh();

	void OnSheetInitialized();

private:
	using Row = CHandleListPage::Row;

	void Query();
	std::vector<Row> GeneralRows() const;
	// the type's own, and the tab's title
	std::vector<Row> DetailRows(std::wstring& title) const;
	std::wstring ProcessText(DWORD pid) const;
	std::wstring Decode(uint32_t access) const;

	HANDLE m_hProcess;
	uint32_t m_Pid;
	std::wstring m_ProcessImage;
	WinSys::HandleEntry m_Entry;
	std::wstring m_Name, m_Type, m_Title, m_DetailsTitle;
	std::function<std::wstring(DWORD)> m_ProcessName;
	std::optional<WinSys::ObjectDetails> m_Details;
	DWORD m_Error{ 0 };	// when there are no details
	CHandleListPage m_General, m_TypeDetails;
	CObjectSecurityInfo* m_Security{ nullptr };
	int m_SecurityTab{ -1 };	// its tab's index, if it was added
	CIcon m_Icon, m_SmallIcon;	// the type's
	CAclDialogTheming m_AclTheming;
};
