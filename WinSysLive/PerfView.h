#pragma once

#include "ViewBase.h"
#include "PerfPages.h"
#include "PerfNav.h"

//
// System-wide performance: a list of pages on the left (Summary, CPU, Memory, Network, I/O, a page per drive and per GPU)
// and the selected page on the right.
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
	void OnFontChanged(HFONT font) override;

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
	void SampleMemory(PerfSample& sample, SYSTEM_PERFORMANCE_INFORMATION const* perf);
	// rates from the previous call's counters (none on the first)
	void SampleIo(PerfSample& sample, SYSTEM_PERFORMANCE_INFORMATION const* perf);
	void SampleNetwork(PerfSample& sample);
	void SampleDisks(PerfSample& sample);
	void SampleGpus(PerfSample& sample);
	// adds and removes drive pages to match the drives that exist
	void SyncDiskPages();
	void UpdateNav(PerfSample const& sample);
	void UpdateTimeSpan();
	void ShowPage(int index);
	// a page's name for the PerfPageKey setting, which stays when drives come and go; the page with a name (-1: none)
	std::wstring PageKey(int index) const;
	int FindPage(std::wstring const& key) const;
	void Layout();

	// the drive pages follow, from FirstDiskItem, in drive letter order (m_DiskLetters), then the GPU pages (in
	// WinSys::GpuInfo::GetAdapters order), which move as drives come and go
	enum NavItem { SummaryItem, CpuItem, MemoryItem, NetworkItem, IoItem, FirstDiskItem };
	int FirstGpuItem() const;

	CPerfNav m_Nav;
	std::vector<std::unique_ptr<IPerfPage>> m_Pages;	// in NavItem order
	int m_Page{ -1 };

	std::vector<CpuTimes> m_PrevCpu;
	int64_t m_PhysicalTotal{ 0 }, m_PageSize{ 0 };
	// the memory lists (modified, standby, free) may need elevation; without them, the composition is in use/available
	bool m_HasMemoryLists{ false };
	// the system's I/O counters at the previous sample
	struct IoCounters {
		int64_t ReadBytes, WriteBytes, OtherBytes;
		uint32_t ReadOps, WriteOps, OtherOps;	// 32-bit counters, which wrap around
	};
	IoCounters m_PrevIo{};
	int64_t m_PrevIoTime{ 0 };	// QPC; 0: no previous sample
	int m_Interval{ 0 };	// the update interval the time span text was built for
	std::wstring m_TimeSpanText;
	GraphCtrl::GraphTheme m_Theme;	// for pages created later
	HFONT m_Font{ nullptr };	// the frame's, for pages created later; null: the pages' default
	WinSys::NetworkTracker m_Network;
	WinSys::DiskTracker m_Disks;
	WinSys::GpuTracker m_Gpu;
	std::vector<wchar_t> m_DiskLetters;
};
