#include "pch.h"
#include "DiskHealthMonitor.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>

using namespace WinSys;

namespace {
	const auto ReadInterval = std::chrono::seconds(30);

	// the thread starts with the first disk asked for and stops at exit (the static's destructor)
	class Worker {
	public:
		DiskHealthMonitor::Reading Get(uint32_t disk) {
			std::unique_lock lock(m_Lock);
			auto it = m_Disks.find(disk);
			if (it != m_Disks.end())
				return it->second;
			m_Disks.emplace(disk, DiskHealthMonitor::Reading{});
			m_New = true;
			if (!m_Thread.joinable())
				m_Thread = std::jthread([this](std::stop_token stop) { Run(stop); });
			lock.unlock();
			m_Wake.notify_one();
			return {};
		}

	private:
		void Run(std::stop_token stop) {
			while (!stop.stop_requested()) {
				std::vector<uint32_t> disks;
				{
					std::unique_lock lock(m_Lock);
					m_New = false;
					for (auto& [disk, reading] : m_Disks)
						disks.push_back(disk);
				}
				for (auto disk : disks) {
					if (stop.stop_requested())
						return;
					// a spun-down hard disk keeps sleeping: its last reading stays
					bool asleep = !DiskHealthReader::IsPoweredOn(disk);
					auto health = asleep ? std::nullopt : DiskHealthReader::Read(disk);
					std::lock_guard lock(m_Lock);
					auto& reading = m_Disks[disk];
					reading.Done = true;
					reading.Asleep = asleep;
					if (!asleep)
						reading.Health = std::move(health);
				}
				std::unique_lock lock(m_Lock);
				// the next round in 30 seconds, or at once for a disk asked for since
				m_Wake.wait_for(lock, stop, ReadInterval, [this] { return m_New; });
			}
		}

		std::mutex m_Lock;
		std::condition_variable_any m_Wake;
		std::map<uint32_t, DiskHealthMonitor::Reading> m_Disks;
		bool m_New{ false };
		std::jthread m_Thread;	// last: stopped and joined before the rest goes
	};

	Worker s_Worker;
}

DiskHealthMonitor::Reading DiskHealthMonitor::Get(uint32_t diskNumber) {
	return s_Worker.Get(diskNumber);
}
