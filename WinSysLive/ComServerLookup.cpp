#include "pch.h"
#include "ComServerLookup.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace {
	struct Request {
		std::wstring CommandLine, ImagePath;
		bool Wow64;
		std::shared_ptr<ComServerLookup::Result> Result;
	};

	// the thread starts with the first request and stops at exit (the static's destructor)
	class Worker {
	public:
		void Add(Request request) {
			{
				std::lock_guard lock(m_Lock);
				m_Queue.push_back(std::move(request));
				if (!m_Thread.joinable())
					m_Thread = std::jthread([this](std::stop_token stop) { Run(stop); });
			}
			m_Wake.notify_one();
		}

	private:
		void Run(std::stop_token stop) {
			::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			while (!stop.stop_requested()) {
				Request request;
				{
					std::unique_lock lock(m_Lock);
					if (!m_Wake.wait(lock, stop, [this] { return !m_Queue.empty(); }))
						break;
					request = std::move(m_Queue.front());
					m_Queue.pop_front();
				}
				request.Result->Info = WinSys::ComServers::Identify(request.CommandLine, request.ImagePath, request.Wow64);
				request.Result->Done.store(true, std::memory_order_release);
			}
			::CoUninitialize();
		}

		std::mutex m_Lock;
		std::condition_variable_any m_Wake;
		std::deque<Request> m_Queue;
		std::jthread m_Thread;	// last: stopped and joined before the queue goes
	};

	Worker s_Worker;
}

std::shared_ptr<ComServerLookup::Result> ComServerLookup::Request(std::wstring commandLine, std::wstring imagePath, bool wow64) {
	auto result = std::make_shared<Result>();
	s_Worker.Add({ std::move(commandLine), std::move(imagePath), wow64, result });
	return result;
}
