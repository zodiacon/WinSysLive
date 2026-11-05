#include "pch.h"
#include "IntegrityChecker.h"
#include <WinSys/Processes.h>

using namespace WinSys;

IntegrityChecker::~IntegrityChecker() {
	m_Worker.request_stop();
	m_State->Wake.notify_all();
	if (m_Worker.joinable())
		m_Worker.detach();
}

void IntegrityChecker::Init(HWND notify, UINT message) {
	m_State->Notify = notify;
	m_State->Message = message;
}

void IntegrityChecker::Queue(Request request) {
	{
		std::lock_guard lock(m_State->Lock);
		m_State->Pending.push_back(request);
	}
	if (!m_Worker.joinable())
		m_Worker = std::jthread(Run, m_State);
	m_State->Wake.notify_one();
}

void IntegrityChecker::CheckImage(ProcessOrThreadKey const& key) {
	if (m_ImageQueued.insert(key).second)
		Queue({ key, true });
}

void IntegrityChecker::CountMemory(ProcessOrThreadKey const& key) {
	auto now = ::GetTickCount64();
	auto [it, added] = m_MemoryQueued.try_emplace(key, now);
	if (!added) {
		if (now - it->second < MemoryMaxAge)
			return;
		it->second = now;
	}
	Queue({ key, false });
}

std::optional<ImageIntegrity> IntegrityChecker::GetImage(ProcessOrThreadKey const& key) const {
	std::lock_guard lock(m_State->Lock);
	auto it = m_State->Results.find(key);
	return it == m_State->Results.end() ? std::nullopt : it->second.Image;
}

std::optional<ExecutableMemory> IntegrityChecker::GetMemory(ProcessOrThreadKey const& key) const {
	std::lock_guard lock(m_State->Lock);
	auto it = m_State->Results.find(key);
	return it == m_State->Results.end() ? std::nullopt : it->second.Memory;
}

void IntegrityChecker::Forget(ProcessOrThreadKey const& key) {
	m_ImageQueued.erase(key);
	m_MemoryQueued.erase(key);
	std::lock_guard lock(m_State->Lock);
	m_State->Results.erase(key);
	if (std::ranges::any_of(m_State->Pending, [&](auto& r) { return r.Key == key; }))
		m_State->Forgotten.insert(key);
}

void IntegrityChecker::Run(std::stop_token stop, std::shared_ptr<State> state) {
	for (;;) {
		Request request;
		{
			std::unique_lock lock(state->Lock);
			if (!state->Wake.wait(lock, stop, [&] { return !state->Pending.empty(); }))
				break;
			request = state->Pending.front();
			state->Pending.pop_front();
			if (state->Forgotten.contains(request.Key)) {
				if (std::ranges::none_of(state->Pending, [&](auto& r) { return r.Key == request.Key; }))
					state->Forgotten.erase(request.Key);
				continue;
			}
		}
		// by its creation time too: not another process that took the ID since
		auto& key = request.Key;
		std::optional<ImageIntegrity> image;
		std::optional<ExecutableMemory> memory;
		if (request.Image) {
			if (auto process = Process::OpenById(key.Id, key.Created, ProcessAccessMask::QueryLimitedInformation | ProcessAccessMask::VmRead))
				image = ProcessIntegrity::CheckImage(process->Handle());
			else
				image = ImageIntegrity{ ImageIntegrityStatus::Unknown, L"The process can't be opened" };
		}
		else if (auto process = Process::OpenById(key.Id, key.Created, ProcessAccessMask::QueryInformation))
			memory = ProcessIntegrity::GetExecutableMemory(process->Handle());
		if (stop.stop_requested())
			break;
		{
			std::lock_guard lock(state->Lock);
			if (state->Forgotten.contains(key))
				continue;
			auto& result = state->Results[key];
			if (request.Image)
				result.Image = std::move(image);
			else {
				result.Memory = memory;
				result.MemoryTime = ::GetTickCount64();
			}
		}
		::PostMessage(state->Notify, state->Message, 0, 0);
	}
}
