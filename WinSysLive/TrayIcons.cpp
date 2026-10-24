#include "pch.h"
#include "TrayIcons.h"
#include "AppSettings.h"
#include "FormatHelper.h"
#include <WinSys/SystemInfo.h>

namespace {
	CTrayIcons::KindInfo const s_Kinds[] = {
		{ L"CPU", L"&CPU", RGB(0, 224, 0) },
		{ L"IO", L"&I/O", RGB(0, 160, 255) },
		{ L"Commit", L"C&ommit charge", RGB(255, 192, 0) },
		{ L"Memory", L"&Physical memory", RGB(200, 112, 255) },
		{ L"Network", L"&Network", RGB(255, 96, 64) },
	};
	PCWSTR const s_ModeNames[] = { L"Off", L"Graph" };

	std::wstring Rate(double bytesPerSecond) {
		return bytesPerSecond < 1 ? L"0 B/s" : FormatHelper::FormatBytes((uint64_t)bytesPerSecond) + L"/s";
	}

}

CTrayIcons::KindInfo const& CTrayIcons::Info(TrayIconKind kind) {
	return s_Kinds[(int)kind];
}

CTrayIcons::Modes CTrayIcons::Load() {
	auto& settings = AppSettings::Get();
	Modes modes{};
	std::wstring text = settings.TrayIcons();
	if (text.empty()) {
		modes[(int)TrayIconKind::Cpu] = settings.TrayIcon() ? TrayIconMode::Graph : TrayIconMode::Off;
		return modes;
	}
	for (size_t pos = 0; pos < text.size(); ) {
		auto comma = text.find(L',', pos);
		auto item = text.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
		if (auto colon = item.find(L':'); colon != std::wstring::npos) {
			auto key = item.substr(0, colon), mode = item.substr(colon + 1);
			for (int i = 0; i < Count; i++)
				if (_wcsicmp(key.c_str(), s_Kinds[i].Key) == 0)
					modes[i] = _wcsicmp(mode.c_str(), L"Off") == 0 ? TrayIconMode::Off : TrayIconMode::Graph;
		}
		if (comma == std::wstring::npos)
			break;
		pos = comma + 1;
	}
	return modes;
}

void CTrayIcons::Save(Modes const& modes) {
	std::wstring text;
	bool any = false;
	for (int i = 0; i < Count; i++) {
		if (!text.empty())
			text += L',';
		text += std::wstring(s_Kinds[i].Key) + L":" + s_ModeNames[(int)modes[i]];
		any |= modes[i] != TrayIconMode::Off;
	}
	auto& settings = AppSettings::Get();
	settings.TrayIcons(text.c_str());
	// for versions that know only the CPU icon
	settings.TrayIcon(any);
	settings.Save();
}

void CTrayIcons::Init(HWND hWnd, UINT callbackMessage) {
	m_hWnd = hWnd;
	m_Message = callbackMessage;
	if (auto basic = WinSys::SystemInfo::GetBasicInformation(); basic) {
		m_PageSize = basic->PageSize;
		m_PhysicalTotal = (int64_t)basic->NumberOfPhysicalPages * basic->PageSize;
	}
	for (int i = 0; i < Count; i++) {
		m_Icons[i].SetColor(s_Kinds[i].Color);
		m_Icons[i].SetTip(L"WinSysLive");
	}
	// percentages to 100; the rates to the largest shown, at least 100 KB/s, so an idle system's trickle stays low
	m_Icons[(int)TrayIconKind::Cpu].SetScale(100);
	m_Icons[(int)TrayIconKind::Commit].SetScale(100);
	m_Icons[(int)TrayIconKind::Memory].SetScale(100);
	m_Icons[(int)TrayIconKind::Io].SetScale(0, 100 * 1024);
	m_Icons[(int)TrayIconKind::Network].SetScale(0, 100 * 1024);
}

void CTrayIcons::Apply(Modes const& modes) {
	m_Modes = modes;
	for (int i = 0; i < Count; i++) {
		auto& icon = m_Icons[i];
		if (modes[i] == TrayIconMode::Off) {
			icon.Remove();
			continue;
		}
		if (icon.IsShown())
			icon.Update();
		else
			icon.Add(m_hWnd, m_Message, i + 1);
	}
	bool network = modes[(int)TrayIconKind::Network] != TrayIconMode::Off;
	if (network && !m_Network) {
		m_Network = std::make_unique<WinSys::NetworkTracker>();
		m_Network->Update();	// the baseline for the rates
	}
	else if (!network)
		m_Network.reset();
}

void CTrayIcons::AddAgain() {
	for (int i = 0; i < Count; i++) {
		m_Icons[i].Remove();
		if (m_Modes[i] != TrayIconMode::Off)
			m_Icons[i].Add(m_hWnd, m_Message, i + 1);
	}
}

void CTrayIcons::RemoveAll() {
	for (auto& icon : m_Icons)
		icon.Remove();
}

bool CTrayIcons::AnyShown() const {
	return std::ranges::any_of(m_Icons, [](auto& icon) { return icon.IsShown(); });
}

void CTrayIcons::UpdateIcon(TrayIconKind kind, double value, std::wstring const& tip) {
	auto& icon = m_Icons[(int)kind];
	icon.AddSample(value);
	icon.SetTip(L"WinSysLive\n" + tip);
	icon.Update();
}

void CTrayIcons::Refresh(std::optional<double> cpu) {
	auto shown = [&](TrayIconKind kind) { return m_Icons[(int)kind].IsShown(); };
	if (!AnyShown())
		return;

	if (cpu && shown(TrayIconKind::Cpu))
		UpdateIcon(TrayIconKind::Cpu, *cpu, std::format(L"CPU: {:.1f}%", *cpu));

	if (shown(TrayIconKind::Io) || shown(TrayIconKind::Commit) || shown(TrayIconKind::Memory)) {
		if (auto perf = WinSys::SystemInfo::GetPerformanceInformation(); perf && m_PageSize) {
			if (shown(TrayIconKind::Io)) {
				LARGE_INTEGER now, frequency;
				::QueryPerformanceCounter(&now);
				::QueryPerformanceFrequency(&frequency);
				int64_t io[3] = { perf->IoReadTransferCount.QuadPart, perf->IoWriteTransferCount.QuadPart, perf->IoOtherTransferCount.QuadPart };
				if (m_PrevIoTime) {
					auto seconds = double(now.QuadPart - m_PrevIoTime) / frequency.QuadPart;
					double rate[3]{};
					for (int i = 0; i < 3; i++)
						rate[i] = seconds > 0 ? (std::max)(0.0, (io[i] - m_PrevIo[i]) / seconds) : 0;
					auto total = rate[0] + rate[1] + rate[2];
					UpdateIcon(TrayIconKind::Io, total,
						std::format(L"I/O: {}\nRead {}, write {}, other {}", Rate(total), Rate(rate[0]), Rate(rate[1]), Rate(rate[2])));
				}
				std::copy(std::begin(io), std::end(io), m_PrevIo);
				m_PrevIoTime = now.QuadPart;
			}
			if (shown(TrayIconKind::Commit) && perf->CommitLimit) {
				auto committed = (int64_t)perf->CommittedPages * m_PageSize, limit = (int64_t)perf->CommitLimit * m_PageSize;
				auto percent = 100.0 * committed / limit;
				UpdateIcon(TrayIconKind::Commit, percent, std::format(L"Commit charge: {} of {} ({:.0f}%)",
					FormatHelper::FormatBytes(committed), FormatHelper::FormatBytes(limit), percent));
			}
			if (shown(TrayIconKind::Memory) && m_PhysicalTotal) {
				auto used = m_PhysicalTotal - (int64_t)perf->AvailablePages * m_PageSize;
				auto percent = 100.0 * used / m_PhysicalTotal;
				UpdateIcon(TrayIconKind::Memory, percent, std::format(L"Physical memory: {} of {} ({:.0f}%)",
					FormatHelper::FormatBytes(used), FormatHelper::FormatBytes(m_PhysicalTotal), percent));
			}
		}
	}
	if (!shown(TrayIconKind::Io))
		m_PrevIoTime = 0;	// shown again: a new baseline

	if (m_Network && shown(TrayIconKind::Network) && m_Network->Update()) {
		auto receive = m_Network->GetTotalReceiveRate(), send = m_Network->GetTotalSendRate();
		UpdateIcon(TrayIconKind::Network, receive + send,
			std::format(L"Network: {}\nReceive {}, send {}", Rate(receive + send), Rate(receive), Rate(send)));
	}
}
