#include "pch.h"
#include "DiskHealthMonitor.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>

using namespace WinSys;

namespace {
	using Clock = std::chrono::steady_clock;
	const auto ReadInterval = std::chrono::seconds(30);
	// a disk not asked for this long (its drive page hidden) isn't read: longer than the slowest update interval (60 s)
	const auto IdleAfter = std::chrono::minutes(2);

	// the thread starts with the first disk asked for and stops at exit (the static's destructor)
	class Worker {
	public:
		DiskHealthMonitor::Reading Get(uint32_t disk) {
			std::unique_lock lock(m_Lock);
			auto now = Clock::now();
			auto [it, added] = m_Disks.try_emplace(disk);
			auto& state = it->second;
			// new, or not read lately (its page hidden): read now
			bool idle = added || now - state.LastAsked > IdleAfter;
			state.LastAsked = now;
			if (!idle || state.Skip)
				return state.Reading;
			auto reading = state.Reading;
			m_New = true;
			if (!m_Thread.joinable())
				m_Thread = std::jthread([this](std::stop_token stop) { Run(stop); });
			lock.unlock();
			m_Wake.notify_one();
			return reading;
		}

	private:
		struct DiskState {
			DiskHealthMonitor::Reading Reading;
			Clock::time_point LastAsked;
			bool Skip{ false };	// on a bus whose health isn't asked for: never touched again
		};

		void Run(std::stop_token stop) {
			while (!stop.stop_requested()) {
				std::vector<uint32_t> disks;
				{
					std::unique_lock lock(m_Lock);
					m_New = false;
					auto now = Clock::now();
					for (auto& [disk, state] : m_Disks)
						if (!state.Skip && now - state.LastAsked <= IdleAfter)
							disks.push_back(disk);
				}
				for (auto disk : disks) {
					if (stop.stop_requested())
						return;
					// a spun-down hard disk keeps sleeping: its last reading stays
					bool asleep = !DiskHealthReader::IsPoweredOn(disk);
					auto health = asleep ? std::nullopt : DiskHealthReader::Read(disk);
					std::lock_guard lock(m_Lock);
					auto& state = m_Disks[disk];
					state.Reading.Done = true;
					state.Reading.Asleep = asleep;
					if (!asleep) {
						state.Skip = health && !health->NotRead.empty();
						state.Reading.Health = std::move(health);
					}
				}
				std::unique_lock lock(m_Lock);
				// the next round in 30 seconds, or at once for a disk asked for since
				m_Wake.wait_for(lock, stop, ReadInterval, [this] { return m_New; });
			}
		}

		std::mutex m_Lock;
		std::condition_variable_any m_Wake;
		std::map<uint32_t, DiskState> m_Disks;
		bool m_New{ false };
		std::jthread m_Thread;	// last: stopped and joined before the rest goes
	};

	Worker s_Worker;
}

DiskHealthMonitor::Reading DiskHealthMonitor::Get(uint32_t diskNumber) {
	return s_Worker.Get(diskNumber);
}
