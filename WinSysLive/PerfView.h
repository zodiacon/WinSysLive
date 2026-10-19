#pragma once

#include "ViewBase.h"
#include <GraphControl/include/GraphControl.h>
#include <GraphControl/include/GraphGrid.h>
#include <GraphControl/include/CompositionBar.h>

//
// System-wide CPU and memory graphs. Samples are taken on every refresh tick, also while the tab is hidden,
// so the history is there when the tab is shown.
//
class CPerfView :
	public CViewBase<CPerfView> {
public:
	using CViewBase::CViewBase;

	CString GetTitle() const override;
	void OnRefresh(bool active) override;
	void OnThemeChanged(bool dark) override;

	BEGIN_MSG_MAP(CPerfView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		CHAIN_MSG_MAP(CViewBase<CPerfView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CPerfView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	struct CpuTimes {
		int64_t Idle, Kernel, User;	// kernel time includes idle time
	};
	// per logical processor, across all processor groups
	static std::vector<CpuTimes> GetCpuTimes();
	void SampleCpu();
	void SampleMemory();
	void UpdateTimeSpan();
	void Layout();

	GraphCtrl::CGraphControl m_Cpu;
	GraphCtrl::CGraphGrid m_Cores;
	GraphCtrl::CGraphControl m_Memory;
	GraphCtrl::CCompositionBar m_Composition;
	GraphCtrl::SeriesId m_KernelSeries{ GraphCtrl::InvalidSeries }, m_UserSeries{ GraphCtrl::InvalidSeries };
	GraphCtrl::SeriesId m_InUseSeries{ GraphCtrl::InvalidSeries };
	CFont m_Font;
	int m_LineHeight{ 0 };

	std::vector<CpuTimes> m_PrevCpu;
	int64_t m_PhysicalTotal{ 0 }, m_PageSize{ 0 };
	// the memory lists (modified, standby, free) need elevation; without them, the composition is in use/available
	bool m_HasMemoryLists{ false };
	int m_Interval{ 0 };	// the update interval the time span text was built for

	// the summary below the graphs
	std::wstring m_Summary[2];
	CRect m_SummaryRect;
	GraphCtrl::GraphTheme m_Theme;
};
