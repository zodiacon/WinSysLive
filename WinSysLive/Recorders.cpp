#include "pch.h"
#include "Recorders.h"
#include "NetworkTracking.h"
#include "AppSettings.h"

namespace {
	std::weak_ptr<WinSys::IpcTracker> s_Ipc;
	std::weak_ptr<WinSys::FileIoTracker> s_FileIo;
	std::weak_ptr<HistoryRecorder> s_History;

	// the current tracker, or a new one started with the session prefix
	template<typename T>
	std::shared_ptr<T> Acquire(std::weak_ptr<T>& current, PCWSTR sessionPrefix) {
		if (auto tracker = current.lock())
			return tracker;
		auto tracker = std::make_shared<T>();
		if (!tracker->Start(sessionPrefix)) {
			// the tracker's destructor stops what did start
			auto error = ::GetLastError();
			tracker.reset();
			::SetLastError(error);
			return nullptr;
		}
		current = tracker;
		return tracker;
	}
}

std::shared_ptr<WinSys::IpcTracker> Recorders::AcquireIpc() {
	return Acquire(s_Ipc, L"WinSysLive IPC");
}

std::shared_ptr<WinSys::FileIoTracker> Recorders::AcquireFileIo() {
	return Acquire(s_FileIo, L"WinSysLive Disk");
}

std::shared_ptr<HistoryRecorder> Recorders::AcquireHistory(ProcessManager& pm) {
	if (auto history = s_History.lock())
		return history;
	auto history = std::make_shared<HistoryRecorder>(pm, (uint32_t)AppSettings::Get().HistoryEvents());
	s_History = history;
	return history;
}

void Recorders::OnRefresh(ProcessManager& pm) {
	if (auto history = s_History.lock())
		history->Update(pm);
	auto ipc = s_Ipc.lock();
	auto fileIo = s_FileIo.lock();
	if (ipc || fileIo)
		for (auto& p : pm.GetTerminatedProcesses()) {
			if (ipc)
				ipc->RemoveProcess(p->Id);
			if (fileIo)
				fileIo->RemoveProcess(p->Id);
		}
}

void Recorders::Holder::Start(ProcessManager& pm) {
	if (!m_History)
		m_History = AcquireHistory(pm);
	m_TrackerError = 0;
	if (!m_Ipc && !(m_Ipc = AcquireIpc()))
		m_TrackerError = ::GetLastError();
	if (!m_FileIo && !(m_FileIo = AcquireFileIo()))
		m_TrackerError = ::GetLastError();
	if (!m_Network && !(m_Network = NetworkTracking::Acquire()))
		m_TrackerError = ::GetLastError();
}

void Recorders::Holder::Stop() {
	m_History.reset();
	m_Ipc.reset();
	m_FileIo.reset();
	m_Network.reset();
	m_TrackerError = 0;
}
