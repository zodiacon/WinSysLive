#pragma once

#include "ProcessPages.h"
#include <WinSys/DotNet.h>
#include <wil/resource.h>
#include <thread>
#include <mutex>

//
// The process sheet's .NET page (added for a process the CLR runs in: DotNetInfo::HasFrameworkRuntime or its .NET
// diagnostics pipe): the runtimes and every loaded assembly, from the runtime's own rundown (WinSys
// DotNetInfo::QueryProcess, an ETW session of its own, "WinSysLive DotNet <pid>.<n>"), queried on a thread when the page
// is first shown and on Refresh (about half a second; elevation needed, the page says so). The list: name, version,
// AppDomain, flags (dynamic, native image, ReadyToRun, domain neutral, collectible), the manifest module's file, the
// native image (NGen), culture and public key token; any column sorts, the filter matches name, version, AppDomain and
// paths. Right-click: Copy (the selected rows, or all shown) and Open File Location.
//
class CProcDotNetPage : public CPropertyPageImpl<CProcDotNetPage> {
public:
	enum { IDD = IDD_PROC_DOTNET };
	enum { WM_DOTNET_DONE = WM_APP + 1 };

	explicit CProcDotNetPage(ProcessProperties const& props);
	~CProcDotNetPage();

	// CPropertyPageImpl (_WTL_NEW_PAGE_NOTIFY_HANDLERS, WTL's default): 0 allows the activation
	int OnSetActive();

	BEGIN_MSG_MAP(CProcDotNetPage)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_DOTNET_DONE, OnDone)
		NOTIFY_HANDLER(IDC_DN_LIST, LVN_COLUMNCLICK, OnColumnClick)
		COMMAND_HANDLER(IDC_DN_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_DN_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(ID_DOTNET_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_DOTNET_OPENLOCATION, OnOpenLocation)
		CHAIN_MSG_MAP(CPropertyPageImpl<CProcDotNetPage>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Name, Version, AppDomain, Flags, Path, NativeImage, Culture, PublicKeyToken, _Count };

	// an assembly as the list shows it
	struct Row {
		std::wstring Name, Version, Culture, PublicKeyToken, AppDomain, Flags, Path, NativeImage;
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDone(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnOpenLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void Layout(int cx, int cy);
	void StartQuery();
	void BuildRows(WinSys::DotNetProcessDetails const& details);
	void ShowRows();
	void UpdateStatus();
	std::wstring const& GetText(Row const& row, Column column) const;
	Row const* GetSelectedRow() const;

	ProcessProperties const& m_Props;
	CListViewCtrl m_List;
	std::vector<Row> m_Rows;	// the list's item data index it
	std::wstring m_Runtimes;	// the runtime line
	size_t m_AppDomains{ 0 };
	std::wstring m_Filter;	// lowercase
	int m_SortColumn{ -1 };
	bool m_SortAscending{ true };
	bool m_Loaded{ false };
	bool m_Querying{ false };
	bool m_Complete{ true };
	DWORD m_Error{ 0 };

	// the query's thread and result
	wil::unique_event_nothrow m_Cancel;
	std::jthread m_Worker;
	std::mutex m_ResultLock;
	std::optional<WinSys::DotNetProcessDetails> m_Result;
	DWORD m_ResultError{ 0 };
};
