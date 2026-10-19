#pragma once

#include "ViewBase.h"
#include "PerfPages.h"
#include "PerfNav.h"

//
// System-wide performance: a list of pages on the left (Summary, CPU, Memory, Network) and the selected page on the right.
// Samples are taken on every refresh tick and handed to every page, also while the tab or the page is hidden,
// so the history is there when it's shown.
//
class CPerfView :
	public CViewBase<CPerfView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	void OnActivate(bool active) override;
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;

	BEGIN_MSG_MAP(CPerfView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		CHAIN_MSG_MAP(CViewBase<CPerfView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CPerfView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	struct CpuTimes {
		int64_t Idle, Kernel, User;	// kernel time includes idle time
	};
	// per logical processor, across all processor groups
	static std::vector<CpuTimes> GetCpuTimes();
	void SampleCpu(PerfSample& sample);
	void SampleMemory(PerfSample& sample);
	void SampleNetwork(PerfSample& sample);
	void UpdateNav(PerfSample const& sample);
	void UpdateTimeSpan();
	void ShowPage(int index);
	void Layout();

	enum NavItem { SummaryItem, CpuItem, MemoryItem, NetworkItem };

	CPerfNav m_Nav;
	std::vector<std::unique_ptr<IPerfPage>> m_Pages;	// in NavItem order
	int m_Page{ -1 };

	std::vector<CpuTimes> m_PrevCpu;
	int64_t m_PhysicalTotal{ 0 }, m_PageSize{ 0 };
	// the memory lists (modified, standby, free) may need elevation; without them, the composition is in use/available
	bool m_HasMemoryLists{ false };
	int m_Interval{ 0 };	// the update interval the time span text was built for
	WinSys::NetworkTracker m_Network;
};
