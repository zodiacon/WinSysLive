#include "pch.h"
#include "GpuPage.h"
#include "FormatHelper.h"
#include <WinSys/Helpers.h>
#include <WTLHelper.h>

using namespace GraphCtrl;

namespace {
	void SetIconId(PROPSHEETPAGE& psp, UINT icon) {
		psp.dwFlags |= PSP_USEICONID;
		psp.pszIcon = MAKEINTRESOURCE(icon);
	}

	// the graphs' time span, as the Performance page's
	// FormatBytes leaves zero empty
	std::wstring Bytes(uint64_t bytes) {
		return bytes ? FormatHelper::FormatBytes(bytes) : L"0 B";
	}

	std::wstring TimeSpanText(int interval) {
		auto seconds = PerfHistoryLength * interval / 1000;
		return seconds % 60 == 0 ? std::format(L"{} minutes", seconds / 60) : std::format(L"{} seconds", seconds);
	}
}

CProcGpuPage::CProcGpuPage(ProcessProperties const& props) : m_Props(props) {
	SetIconId(m_psp, IDI_DIRECTX);
	// created with the sheet, so the graphs have history when the page is first shown
	m_psp.dwFlags |= PSP_PREMATURE;
}

LRESULT CProcGpuPage::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hProcess = OpenSheetProcess(m_Props, PROCESS_QUERY_INFORMATION);
	if (!m_hProcess)
		m_OpenError = ::GetLastError();

	auto theme = WTLHelper::IsDarkMode() ? GraphTheme::Dark() : GraphTheme::Light();
	auto timeSpan = TimeSpanText(m_Props.UpdateInterval);
	for (auto& info : WinSys::GpuInfo::GetAdapters()) {
		auto adapter = std::make_unique<Adapter>();
		auto& engines = adapter->Engines;
		engines.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, GCS_GRID | GCS_FILL | GCS_TOOLTIP);
		engines.SetGraphCount(info.Engines.size());
		engines.SetRange(0, 100);
		engines.SetValueFormatter(Format::Percent);
		engines.SetHistoryLength(PerfHistoryLength);
		engines.SetSeriesStyle(SeriesStyle{ PerfColors::Gpu, PerfColors::Gpu });
		engines.SetSharedCrosshair(true);
		for (size_t i = 0; i < info.Engines.size(); i++)
			engines.GetGraph(i)->SetTitle(info.Engines[i]);
		engines.SetTheme(theme);
		engines.SetTimeSpanText(timeSpan);

		auto& memory = adapter->Memory;
		memory.Create(m_hWnd, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE, GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS | GCS_LEGEND | GCS_TOOLTIP);
		memory.AddSeries(L"Dedicated", SeriesStyle{ PerfColors::Memory, PerfColors::Memory });
		memory.AddSeries(L"Shared", SeriesStyle{ PerfColors::GpuShared, PerfColors::GpuShared });
		memory.SetTitle(L"GPU Memory");
		memory.SetRange(0, 1 << 20);
		memory.SetAutoScale(true);
		memory.SetValueFormatter(Format::Bytes);
		memory.SetHistoryLength(PerfHistoryLength);
		memory.SetTheme(theme);
		memory.SetTimeSpanText(timeSpan);
		memory.SetExpectedInterval(m_Props.UpdateInterval);

		adapter->Line.Create(m_hWnd, rcDefault, info.Name.c_str(), WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS);
		adapter->Line.SetFont(GetFont());
		m_Adapters.push_back(std::move(adapter));
	}

	CRect rc;
	GetClientRect(&rc);
	Layout(rc.Width(), rc.Height());
	return TRUE;
}

LRESULT CProcGpuPage::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	Layout(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	return 0;
}

void CProcGpuPage::Layout(int cx, int cy) {
	if (m_Adapters.empty() || !m_Adapters[0]->Memory.IsWindow())
		return;
	// the other pages' margins; each adapter a band: its engines' grid and its memory graph side by side, its line below
	CRect units(0, 0, 7, 11);
	MapDialogRect(&units);
	int margin = units.right, lineHeight = units.bottom, gap = units.right / 2;
	int count = (int)m_Adapters.size();
	int bandHeight = (std::max)(0, (cy - 2 * margin - (count - 1) * gap) / count);
	int width = (std::max)(0, cx - 2 * margin);
	for (int i = 0; i < count; i++) {
		auto& a = *m_Adapters[i];
		int top = margin + i * (bandHeight + gap);
		int graphsHeight = (std::max)(0, bandHeight - lineHeight - gap / 2);
		int enginesWidth = (std::max)(0, (width - gap) * 62 / 100);
		a.Engines.MoveWindow(margin, top, enginesWidth, graphsHeight);
		// square-ish tiles, as the Performance tab's GPU page
		if (auto engines = (int)a.Engines.GetGraphCount(); engines > 0 && graphsHeight > 0) {
			int columns = std::clamp((int)std::lround(std::sqrt((double)engines * enginesWidth / graphsHeight)), 1, engines);
			a.Engines.SetLayout(columns, (engines + columns - 1) / columns);
		}
		a.Memory.MoveWindow(margin + enginesWidth + gap, top, (std::max)(0, width - enginesWidth - gap), graphsHeight);
		a.Line.MoveWindow(margin, top + graphsHeight + gap / 2, width, lineHeight);
	}
}

void CProcGpuPage::OnSample(ProcessSnapshot const& snapshot) {
	if (m_Adapters.empty() || !m_Adapters[0]->Memory.IsWindow() || m_Exited)
		return;
	if (!snapshot.Process()) {
		if (snapshot.Exited()) {
			m_Exited = true;
			auto& adapters = WinSys::GpuInfo::GetAdapters();
			for (size_t i = 0; i < m_Adapters.size(); i++)
				m_Adapters[i]->Line.SetWindowText((adapters[i].Name + L": the process has exited").c_str());
		}
		return;
	}
	auto& adapters = WinSys::GpuInfo::GetAdapters();
	if (!m_hProcess) {
		auto why = m_OpenError == ERROR_ACCESS_DENIED ? std::wstring(L"the process can't be queried (administrator rights may help)")
			: L"the process can't be queried: " + WinSys::Helpers::GetErrorText(m_OpenError);
		for (size_t i = 0; i < m_Adapters.size(); i++)
			m_Adapters[i]->Line.SetWindowText((adapters[i].Name + L": " + why).c_str());
		return;
	}

	auto usage = WinSys::GpuInfo::QueryProcess(m_hProcess.get(), m_State, WinSys::GpuAdapterKind::Gpu, true);
	for (size_t i = 0; i < m_Adapters.size(); i++) {
		auto& a = *m_Adapters[i];
		auto& info = adapters[i];
		WinSys::GpuProcessAdapterUsage const* use = i < usage.Adapters.size() ? &usage.Adapters[i] : nullptr;
		// the engines: missing until there's a previous sample; zero for a process without GPU memory (not queried)
		std::vector<float> engines(info.Engines.size(), usage.Known && use && use->Engines.empty() && use->Dedicated == 0
			&& use->Shared == 0 ? 0.0f : MissingSample);
		if (use && use->Engines.size() == engines.size())
			engines = use->Engines;
		if (engines.size() == a.Engines.GetGraphCount())
			a.Engines.PushSamples(engines);
		float memory[] = { use ? (float)use->Dedicated : MissingSample, use ? (float)use->Shared : MissingSample };
		a.Memory.PushSamples(memory, _countof(memory));

		std::wstring text = info.Name + L": ";
		if (!usage.Known || !use)
			text += L"not known";
		else {
			float busiest = 0;
			for (auto e : engines)
				if (e != MissingSample)
					busiest = (std::max)(busiest, e);
			text += std::format(L"GPU {:.1f}%    Dedicated memory: {}    Shared memory: {}", busiest,
				Bytes(use->Dedicated), Bytes(use->Shared));
		}
		a.Line.SetWindowText(text.c_str());
	}
}
