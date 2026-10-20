#pragma once

#include <GraphControl/include/GraphControl.h>
#include <GraphControl/include/GraphGrid.h>
#include <GraphControl/include/CompositionBar.h>
#include <WinSys/Network.h>
#include <WinSys/Disk.h>
#include <WinSys/Gpu.h>

// samples kept by the graphs
const size_t PerfHistoryLength = 120;

namespace PerfColors {
	const COLORREF Kernel = RGB(214, 94, 94);
	const COLORREF User = RGB(17, 125, 187);
	const COLORREF Memory = RGB(139, 92, 196);
	const COLORREF Receive = RGB(214, 140, 40);
	const COLORREF Send = RGB(60, 160, 90);
	const COLORREF DiskActive = RGB(64, 168, 160);
	const COLORREF DiskRead = RGB(60, 140, 210);
	const COLORREF DiskWrite = RGB(220, 120, 60);
	const COLORREF IoOther = RGB(150, 150, 150);
	const COLORREF Gpu = RGB(196, 84, 168);
	const COLORREF GpuShared = RGB(96, 118, 170);
}

//
// One refresh tick's worth of system-wide performance data, taken by CPerfView and handed to every page.
//
struct PerfSample {
	bool HasCpu{ false };	// false on the first tick (no deltas yet) or when the processor count changed
	float KernelCpu{ 0 }, UserCpu{ 0 };	// percent of all processors
	std::vector<float> Cores;	// busy percent per logical processor, across all processor groups

	bool HasMemory{ false };
	bool HasMemoryLists{ false };	// Modified, Standby and Free are valid
	int64_t PhysicalTotal{ 0 }, InUse{ 0 }, Available{ 0 }, Modified{ 0 }, Standby{ 0 }, Free{ 0 };
	int64_t Committed{ 0 }, CommitLimit{ 0 }, Cached{ 0 }, PagedPool{ 0 }, NonPagedPool{ 0 };

	size_t Processes{ 0 };
	int64_t Threads{ 0 }, Handles{ 0 };
	uint64_t Uptime{ 0 };	// seconds

	// network adapters, rates in bytes per second; owned by CPerfView and valid for the duration of the update
	std::vector<WinSys::NetworkTracker::Interface> const* Adapters{ nullptr };
	bool HasNetwork{ false };
	double NetReceive{ 0 }, NetSend{ 0 };

	// drives with a letter; owned by CPerfView and valid for the duration of the update
	std::vector<WinSys::DiskTracker::Volume> const* Disks{ nullptr };
	bool HasDisks{ false };	// at least one drive has counters
	double DiskRead{ 0 }, DiskWrite{ 0 };	// all drives, bytes per second

	// the GPUs, in WinSys::GpuInfo::GetAdapters order; owned by CPerfView and valid for the duration of the update
	std::vector<WinSys::GpuTracker::AdapterSample> const* Gpus{ nullptr };
	bool HasGpu{ false };	// the utilizations are valid (not on the first tick)

	float CpuPercent() const {
		return KernelCpu + UserCpu;
	}
	float MemoryPercent() const {
		return PhysicalTotal ? 100.0f * InUse / PhysicalTotal : 0.0f;
	}
};

//
// A page of the Performance tab. All pages receive every sample, so their graphs have history when shown.
//
struct IPerfPage abstract {
	virtual ~IPerfPage() = default;
	virtual HWND CreatePage(HWND hParent) = 0;
	virtual HWND GetHwnd() const = 0;
	virtual void Update(PerfSample const& sample, bool visible) = 0;
	virtual void SetTheme(GraphCtrl::GraphTheme const& theme) = 0;
	virtual void SetTimeSpan(std::wstring const& text, UINT interval) = 0;
	// for the text below the graphs
	virtual void SetTextFont(HFONT font) = 0;
};

//
// Common page behavior: background, a block of text lines below the graphs, and layout.
// The derived class implements CreateGraphs(), LayoutGraphs(CRect const&, int gap), Sample(PerfSample const&),
// ApplyTheme() and ApplyTimeSpan(text, interval).
//
template<typename T>
class CPerfPage :
	public CWindowImpl<T>,
	public IPerfPage {
public:
	HWND CreatePage(HWND hParent) override {
		return this->Create(hParent, this->rcDefault, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	}

	HWND GetHwnd() const override {
		return this->m_hWnd;
	}

	void Update(PerfSample const& sample, bool visible) override {
		static_cast<T*>(this)->Sample(sample);
		if (visible)
			this->InvalidateRect(m_TextRect, FALSE);
	}

	void SetTheme(GraphCtrl::GraphTheme const& theme) override {
		m_Theme = theme;
		static_cast<T*>(this)->ApplyTheme();
		this->Invalidate();
	}

	void SetTimeSpan(std::wstring const& text, UINT interval) override {
		static_cast<T*>(this)->ApplyTimeSpan(text, interval);
	}

	void SetTextFont(HFONT font) override {
		LOGFONT lf;
		if (!::GetObject(font, sizeof(lf), &lf))
			return;
		MakeFont(lf);
		// the text block's height follows the font
		BOOL handled;
		OnSize(WM_SIZE, 0, 0, handled);
	}

	BEGIN_MSG_MAP(CPerfPage)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

protected:
	int Scale(int value) const {
		return ::MulDiv(value, ::GetDpiForWindow(this->m_hWnd), 96);
	}

	GraphCtrl::GraphTheme m_Theme;
	std::vector<std::wstring> m_Lines;	// text below the graphs; set its size in CreateGraphs

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		NONCLIENTMETRICS ncm = { sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(this->m_hWnd));
		MakeFont(ncm.lfMessageFont);

		static_cast<T*>(this)->CreateGraphs();
		return 0;
	}

	void MakeFont(LOGFONT const& lf) {
		if (!m_Font.IsNull())
			m_Font.DeleteObject();
		m_Font.CreateFontIndirect(&lf);
		CClientDC dc(this->m_hWnd);
		auto old = dc.SelectFont(m_Font);
		TEXTMETRIC tm;
		dc.GetTextMetrics(&tm);
		m_LineHeight = tm.tmHeight + tm.tmExternalLeading;
		dc.SelectFont(old);
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		int margin = Scale(8), gap = Scale(6);
		CRect rc;
		this->GetClientRect(&rc);
		rc.DeflateRect(margin, margin);
		int textHeight = (int)m_Lines.size() * m_LineHeight;
		m_TextRect.SetRect(rc.left, std::max(rc.top, rc.bottom - textHeight), rc.right, rc.bottom);
		rc.bottom = std::max(rc.top, m_TextRect.top - (textHeight ? gap : 0));
		static_cast<T*>(this)->LayoutGraphs(rc, gap);
		this->Invalidate();
		return 0;
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		return 1;	// OnPaint fills the background
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		CPaintDC dc(this->m_hWnd);
		CRect rc;
		this->GetClientRect(&rc);
		dc.FillSolidRect(&rc, m_Theme.Background);

		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(m_Theme.TextColor);
		auto old = dc.SelectFont(m_Font);
		CRect line(m_TextRect);
		for (auto& text : m_Lines) {
			line.bottom = line.top + m_LineHeight;
			dc.DrawText(text.c_str(), (int)text.size(), &line, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			line.top = line.bottom;
		}
		dc.SelectFont(old);
		return 0;
	}

	CFont m_Font;
	int m_LineHeight{ 0 };
	CRect m_TextRect;
};

//
// The graphs used on more than one page.
//
class CCpuGraph : public GraphCtrl::CGraphControl {
public:
	void Create(HWND hParent);
	void Push(PerfSample const& sample);
};

class CMemoryGraph : public GraphCtrl::CGraphControl {
public:
	void Create(HWND hParent, int64_t physicalTotal);
	void Push(PerfSample const& sample);

private:
	GraphCtrl::SeriesId m_InUse{ GraphCtrl::InvalidSeries };
};

// Receive and send throughput, in bits per second (adapters are rated in bits)
class CNetworkGraph : public GraphCtrl::CGraphControl {
public:
	void Create(HWND hParent, std::wstring title);
	void Push(double receive, double send, bool valid);	// bytes per second
};

// Read and write transfer rates, in bytes per second
class CDiskGraph : public GraphCtrl::CGraphControl {
public:
	void Create(HWND hParent, std::wstring title);
	void Push(double read, double write, bool valid);
};

//
// Summary: total CPU, memory, network and disk, with the system-wide figures below.
//
class CPerfSummaryPage : public CPerfPage<CPerfSummaryPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfSummary", CS_HREDRAW | CS_VREDRAW, -1)

	explicit CPerfSummaryPage(int64_t physicalTotal) : m_PhysicalTotal(physicalTotal) {}

	void CreateGraphs();
	void LayoutGraphs(CRect const& rc, int gap);
	void Sample(PerfSample const& sample);
	void ApplyTheme();
	void ApplyTimeSpan(std::wstring const& text, UINT interval);

private:
	CCpuGraph m_Cpu;
	CMemoryGraph m_Memory;
	CNetworkGraph m_Network;
	CDiskGraph m_Disk;
	int64_t m_PhysicalTotal;
};

//
// CPU: kernel/user time and a graph per logical processor.
//
class CPerfCpuPage : public CPerfPage<CPerfCpuPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfCpu", CS_HREDRAW | CS_VREDRAW, -1)

	explicit CPerfCpuPage(size_t processors) : m_Processors(processors) {}

	void CreateGraphs();
	void LayoutGraphs(CRect const& rc, int gap);
	void Sample(PerfSample const& sample);
	void ApplyTheme();
	void ApplyTimeSpan(std::wstring const& text, UINT interval);

private:
	CCpuGraph m_Cpu;
	GraphCtrl::CGraphGrid m_Cores;
	size_t m_Processors;
};

//
// Memory: physical memory in use and how physical memory is divided.
//
class CPerfMemoryPage : public CPerfPage<CPerfMemoryPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfMemory", CS_HREDRAW | CS_VREDRAW, -1)

	CPerfMemoryPage(int64_t physicalTotal, bool hasMemoryLists) : m_PhysicalTotal(physicalTotal), m_HasMemoryLists(hasMemoryLists) {}

	void CreateGraphs();
	void LayoutGraphs(CRect const& rc, int gap);
	void Sample(PerfSample const& sample);
	void ApplyTheme();
	void ApplyTimeSpan(std::wstring const& text, UINT interval);

private:
	CMemoryGraph m_Memory;
	GraphCtrl::CCompositionBar m_Composition;
	int64_t m_PhysicalTotal;
	bool m_HasMemoryLists;
};

//
// Network: total throughput and a graph per adapter that is up. Adapters come and go (a cable plugged in, Wi-Fi
// connecting), so their graphs are created and destroyed as the list changes.
//
class CPerfNetworkPage : public CPerfPage<CPerfNetworkPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfNetwork", CS_HREDRAW | CS_VREDRAW, -1)

	void CreateGraphs();
	void LayoutGraphs(CRect const& rc, int gap);
	void Sample(PerfSample const& sample);
	void ApplyTheme();
	void ApplyTimeSpan(std::wstring const& text, UINT interval);

private:
	struct Adapter {
		uint64_t Luid;
		std::wstring Title;
		std::unique_ptr<CNetworkGraph> Graph;
	};
	void SyncAdapters(std::vector<WinSys::NetworkTracker::Interface const*> const& adapters);
	void Relayout();

	CNetworkGraph m_Total;
	std::vector<Adapter> m_Adapters;	// in the order the system lists them
	std::wstring m_TimeSpanText;
	UINT m_Interval{ 0 };
	CRect m_GraphRect;	// from the last LayoutGraphs, for laying out again when adapters change
	int m_Gap{ 0 };
};
//
// A GPU: a graph per engine (3D, Copy, Video Decode...: the busiest node of each), and its dedicated and shared memory.
//
class CPerfGpuPage : public CPerfPage<CPerfGpuPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfGpu", CS_HREDRAW | CS_VREDRAW, -1)

	// index: in WinSys::GpuInfo::GetAdapters
	explicit CPerfGpuPage(size_t index) : m_Index(index) {}

	void CreateGraphs();
	void LayoutGraphs(CRect const& rc, int gap);
	void Sample(PerfSample const& sample);
	void ApplyTheme();
	void ApplyTimeSpan(std::wstring const& text, UINT interval);

private:
	GraphCtrl::CGraphGrid m_Engines;
	GraphCtrl::CGraphControl m_Dedicated, m_Shared;
	GraphCtrl::SeriesId m_DedicatedSeries{ GraphCtrl::InvalidSeries }, m_SharedSeries{ GraphCtrl::InvalidSeries };
	size_t m_Index;
};

//
// A drive (C:, D:...): active time, read/write transfer rates and how full it is.
//
class CPerfDiskPage : public CPerfPage<CPerfDiskPage> {
public:
	DECLARE_WND_CLASS_EX(L"WinSysLivePerfDisk", CS_HREDRAW | CS_VREDRAW, -1)

	explicit CPerfDiskPage(wchar_t letter) : m_Letter(letter) {}

	void CreateGraphs();
	void LayoutGraphs(CRect const& rc, int gap);
	void Sample(PerfSample const& sample);
	void ApplyTheme();
	void ApplyTimeSpan(std::wstring const& text, UINT interval);

private:
	GraphCtrl::CGraphControl m_Active;
	CDiskGraph m_Transfer;
	GraphCtrl::SeriesId m_ActiveSeries{ GraphCtrl::InvalidSeries };
	GraphCtrl::CCompositionBar m_Space;
	wchar_t m_Letter;
};