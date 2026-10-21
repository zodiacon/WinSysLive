#pragma once

#include "resource.h"
#include "ViewBase.h"
#include "FilterBox.h"
#include "ColumnLayout.h"
#include "Signature.h"
#include <WinSys/KernelModules.h>
#include <VirtualListView.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_set>

//
// The Kernel Modules tab: the kernel's loaded modules (WinSys KernelModules: the kernel, the HAL, drivers), read again on every
// refresh tick (also while hidden, so the highlighting stays right): new ones green for two refreshes, unloaded ones red
// once. Their bases need elevation. The version strings come from the files (GetCachedVersionInfo, as they're shown);
// the signatures are verified on a worker thread once the tab is first shown (WinSys FileSignature: WinVerifyTrust, no
// revocation checks), which posts WM_KMODULE_SIGNED as each is done. The filter box matches the name, path,
// description and company. Double-click or Properties opens the file's properties; Copy copies the selected row.
//
class CKernelModulesView :
	public CViewBase<CKernelModulesView>,
	public CVirtualListView<CKernelModulesView> {
public:
	using CViewBase::CViewBase;
	~CKernelModulesView();

	CString GetTitle() const override;
	HWND GetExportList() override {
		return m_List;
	}
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnFontChanged(HFONT font) override;
	void SaveSettings() override {
		SaveColumns();
	}
	// the frame's image list has the new DPI's size: the rows take its height
	void OnDpiChanged(UINT /*dpi*/) override {
		m_List.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);
	}

	// CVirtualListView
	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetSaveColumnRange(HWND, int&) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt);
	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt);

	// posted by the signature worker when a file's signature is verified
	static constexpr UINT WM_KMODULE_SIGNED = WM_APP + 0x120;

	BEGIN_MSG_MAP(CKernelModulesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_KMODULE_SIGNED, OnSigned)
		COMMAND_HANDLER(IDC_VIEW_FILTER, EN_CHANGE, OnFilterChanged)
		NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnCustomDraw)
		CHAIN_MSG_MAP(CVirtualListView<CKernelModulesView>)
		CHAIN_MSG_MAP(CViewBase<CKernelModulesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_SELECTCOLUMNS, OnSelectColumns)
		COMMAND_ID_HANDLER(ID_COLUMN_HIDE, OnHideColumn)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_KMODULE_PROPERTIES, OnProperties)
		COMMAND_ID_HANDLER(ID_KMODULE_GOTOFILELOCATION, OnGoToFileLocation)
		CHAIN_MSG_MAP_ALT(CViewBase<CKernelModulesView>, 1)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Name, Base, Size, LoadOrder, Description, Company, Version, Signature, Path, _Count };

	struct DriverRow {
		WinSys::KernelModule Module;
		std::wstring Key;	// the path, lowercase, and the base: a driver unloaded and loaded again is another row
		int NewTicks{ 0 };	// > 0: still highlighted as new
		bool Unloaded{ false };	// shown once more, then removed
	};

	// the signatures, verified on a worker thread; shared with it, as it may still be verifying a file when the view goes
	struct Signatures {
		std::mutex Lock;
		std::condition_variable_any Wake;
		std::deque<std::wstring> Pending;	// paths
		std::unordered_map<std::wstring, WinSys::SignatureInfo> Results;	// by lowercase path
		HWND Notify{ nullptr };
	};

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSelectColumns(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnHideColumn(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	// the shown columns in display order (the header's, which the user can change by dragging), widths at 96 DPI
	std::vector<ColumnLayout::ShownColumn> GetShownColumns() const;
	// rebuilds the list's columns; that clears the sorting, whose column indices refer to the old ones
	void SetColumns(std::vector<ColumnLayout::ShownColumn> const& columns);
	// in the KernelModuleColumns setting
	void SaveColumns() const;
	LRESULT OnSigned(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFind(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnProperties(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnGoToFileLocation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// true if a module was loaded
	bool Merge(std::vector<WinSys::KernelModule> current);
	// the same modules as the rows, none of them highlighted: nothing to merge
	bool IsUnchanged(std::vector<WinSys::KernelModule> const& current) const;
	std::wstring GetText(DriverRow const& row, Column column) const;
	std::wstring GetSignatureText(std::wstring const& path) const;
	bool Matches(DriverRow const& row) const;
	// the list's rows: the drivers that match the filter, sorted as the list is; selected: the key of the row to select
	void ShowRows(std::wstring const& selected);
	std::wstring GetSelectedKey() const;
	DriverRow const* GetSelected() const;
	void SelectDriver(std::wstring const& key);
	void UpdateStatus();
	// starts the signature worker (the first time the tab is shown), and queues the paths it hasn't verified
	void VerifySignatures();
	static void VerifyThread(std::stop_token stop, std::shared_ptr<Signatures> signatures);
	static std::wstring Lowercase(std::wstring text);

	CListViewCtrl m_List;
	CFilterBox m_Filter;	// in the view's rebar
	int m_MenuColumn{ -1 };	// the Column the header's context menu was opened on, or -1
	std::vector<DriverRow> m_All;	// the last merge: every module, the unloaded ones once more
	std::vector<DriverRow const*> m_Rows;	// into m_All: the list's rows, in display order
	std::shared_ptr<Signatures> m_Signatures{ std::make_shared<Signatures>() };
	std::unordered_set<std::wstring> m_Queued;	// paths (lowercase) given to the worker
	std::jthread m_Verifier;
	bool m_Loaded{ false };	// read at least once: later modules are new
	bool m_Active{ false };
};
