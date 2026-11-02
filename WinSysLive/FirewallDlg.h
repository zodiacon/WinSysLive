#pragma once

#include "resource.h"
#include "DpiLayout.h"
#include <WinSys/Firewall.h>
#include <functional>
#include <mutex>

//
// System > Firewall Rules: a modal, resizable dialog. Windows Defender Firewall's rules (WinSys Firewall::EnumRules,
// about 0.2 s for 780 rules, read on a detached thread with COM initialized that shares only a LoadState with the
// dialog: closing doesn't wait) and, on top, each profile's state (on or off, the default inbound and outbound actions,
// and which profiles are active, Firewall::GetPolicy). A row per rule: name, group, direction, action, enabled,
// profiles, protocol, ports, addresses, program, service, package, interface types, edge traversal and description.
// Disabled rules are gray, block rules red. Inbound and Outbound (both on at first) and Enabled Only choose the rows,
// the filter matches the name, group, program, service, ports and description; any column sorts. The context menu:
// Enable or Disable (the selected rules; elevated only, as the firewall allows changes to administrators; each rule
// found again by its identity, Firewall::SetEnabled, its row updated in place), Open File Location (the program,
// environment variables expanded), Go to Process (a running process of the program, from the frame's snapshot,
// closing the dialog), Copy (the selected rows, else all shown, with the column names).
//
class CFirewallDlg :
	public CDialogImpl<CFirewallDlg>,
	public CDialogResize<CFirewallDlg>,
	public CDpiDialogResize<CFirewallDlg> {
public:
	enum { IDD = IDD_FIREWALL };
	static constexpr UINT WM_FIREWALL_READ = WM_APP + 0x1E2;

	// the running processes by image path (lowercase), and going to a process
	using RunningLookup = std::function<std::unordered_map<std::wstring, std::vector<uint32_t>>()>;
	CFirewallDlg(RunningLookup running, std::function<void(uint32_t)> goToProcess)
		: m_Running(std::move(running)), m_GoToProcess(std::move(goToProcess)) {}

	BEGIN_DLGRESIZE_MAP(CFirewallDlg)
		DLGRESIZE_CONTROL(IDC_FWR_PROFILES, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_FWR_FILTER, DLSZ_SIZE_X)
		DLGRESIZE_CONTROL(IDC_FWR_INBOUND, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_FWR_OUTBOUND, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_FWR_ENABLEDONLY, DLSZ_MOVE_X)
		DLGRESIZE_CONTROL(IDC_FWR_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_FWR_STATUS, DLSZ_SIZE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FWR_REFRESH, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_FWR_COPY, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

	BEGIN_MSG_MAP(CFirewallDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_FIREWALL_READ, OnRulesRead)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		COMMAND_HANDLER(IDC_FWR_FILTER, EN_CHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_FWR_INBOUND, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_FWR_OUTBOUND, OnFilterChanged)
		COMMAND_ID_HANDLER(IDC_FWR_ENABLEDONLY, OnFilterChanged)
		NOTIFY_HANDLER(IDC_FWR_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_FWR_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		COMMAND_ID_HANDLER(IDC_FWR_REFRESH, OnRefresh)
		COMMAND_ID_HANDLER(IDC_FWR_COPY, OnCopy)
		COMMAND_ID_HANDLER(IDCANCEL, OnClose)
		CHAIN_MSG_MAP(CDpiDialogResize<CFirewallDlg>)
		CHAIN_MSG_MAP(CDialogResize<CFirewallDlg>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

private:
	enum class Column { Name, Group, Direction, Action, Enabled, Profiles, Protocol, LocalPorts, RemotePorts,
		LocalAddresses, RemoteAddresses, Program, Service, Package, Interfaces, EdgeTraversal, Description, _Count };

	struct LoadState {
		std::mutex Lock;
		std::optional<std::vector<WinSys::FirewallRule>> Rules;
		std::optional<WinSys::FirewallPolicy> Policy;
		DWORD Error{ 0 };
	};

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRulesRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnCustomDraw(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnRefresh(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnClose(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// starts reading the rules and the policy (if not reading already)
	void Load();
	// the list's rows: the rules the check boxes and the filter allow, sorted
	void ShowRows();
	void ShowPolicy();
	std::wstring GetText(WinSys::FirewallRule const& r, Column column) const;
	// the rules of the selected rows (their indices in m_Rules)
	std::vector<size_t> GetSelected() const;
	// turns the selected rules on or off; the status line says how it went
	void SetEnabled(bool enabled);
	// the program's path with its environment variables expanded, lowercase for the lookup
	static std::wstring ExpandPath(std::wstring const& path);
	std::vector<uint32_t> const* GetProcesses(WinSys::FirewallRule const& r) const;
	static int CALLBACK Compare(LPARAM index1, LPARAM index2, LPARAM param);

	RunningLookup m_Running;
	std::function<void(uint32_t)> m_GoToProcess;
	std::unordered_map<std::wstring, std::vector<uint32_t>> m_Processes;	// by image path, lowercase
	CListViewCtrl m_List;
	std::vector<WinSys::FirewallRule> m_Rules;	// each row's item data is its index here
	std::optional<WinSys::FirewallPolicy> m_Policy;
	std::shared_ptr<LoadState> m_Load;	// while reading
	DWORD m_Error{ 0 };
	std::wstring m_Filter;	// lowercase
	int m_SortColumn{ (int)Column::Name };
	bool m_SortAscending{ true };
};
