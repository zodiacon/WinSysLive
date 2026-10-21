#include "pch.h"
#include "AddressResolver.h"

namespace {
	// lookups of addresses without a name take seconds, so a few run at once
	int const WorkerCount = 4;

	bool IsV6(WinSys::ConnectionProtocol proto) {
		return proto == WinSys::ConnectionProtocol::Tcp6 || proto == WinSys::ConnectionProtocol::Udp6;
	}
}

AddressResolver& AddressResolver::Get() {
	// never destroyed: Shutdown leaves the workers to the process's end, and they use it until then
	static auto resolver = new AddressResolver;
	return *resolver;
}

void AddressResolver::Enable(bool enable) {
	m_Enabled = enable;
}

bool AddressResolver::IsEnabled() const {
	return m_Enabled;
}

std::wstring AddressResolver::GetText(std::array<BYTE, 16> const& address, bool v6, uint32_t scope, std::wstring const& text, HWND notify) {
	if (!m_Enabled || text.empty() || WinSys::ConnectionInfo::IsUnspecified(address, v6))
		return text;
	Key key{ address, v6 ? scope : 0, v6 };
	if (!v6)
		std::fill(key.Address.begin() + 4, key.Address.end(), BYTE(0));
	std::lock_guard lock(m_Lock);
	if (m_Shutdown)
		return text;
	auto [it, added] = m_Cache.try_emplace(key);
	auto& entry = it->second;
	if (entry.Done)
		return entry.Name.empty() ? text : entry.Name;
	if (notify && std::ranges::find(entry.Waiting, notify) == entry.Waiting.end())
		entry.Waiting.push_back(notify);
	if (added) {
		m_Queue.push_back(key);
		if (m_Workers.empty())
			for (int i = 0; i < WorkerCount; i++)
				m_Workers.emplace_back([this](std::stop_token stop) { Work(stop); });
		m_Wake.notify_one();
	}
	return text;
}

std::wstring AddressResolver::GetLocalText(WinSys::Connection const& c, HWND notify) {
	return GetText(c.Id.LocalAddress, IsV6(c.Id.Proto), c.Id.LocalScope, c.LocalText, notify);
}

std::wstring AddressResolver::GetRemoteText(WinSys::Connection const& c, HWND notify) {
	return GetText(c.Id.RemoteAddress, IsV6(c.Id.Proto), c.Id.RemoteScope, c.RemoteText, notify);
}

void AddressResolver::Work(std::stop_token stop) {
	for (;;) {
		Key key;
		{
			std::unique_lock lock(m_Lock);
			if (!m_Wake.wait(lock, stop, [&] { return !m_Queue.empty(); }))
				return;
			key = m_Queue.front();
			m_Queue.pop_front();
		}
		auto name = WinSys::ConnectionInfo::LookupName(key.Address, key.V6, key.Scope);
		std::vector<HWND> waiting;
		{
			std::lock_guard lock(m_Lock);
			auto& entry = m_Cache[key];
			entry.Done = true;
			entry.Name = std::move(name);
			waiting.swap(entry.Waiting);
		}
		// a window may be gone by now: the post fails
		for (auto hWnd : waiting)
			::PostMessage(hWnd, WM_ADDRESS_RESOLVED, 0, 0);
	}
}

void AddressResolver::Shutdown() {
	std::vector<std::jthread> workers;
	{
		std::lock_guard lock(m_Lock);
		m_Shutdown = true;
		workers.swap(m_Workers);
	}
	// a lookup under way can take seconds and can't be canceled (GetNameInfoW), so the workers aren't joined: the
	// process's end stops them
	for (auto& w : workers) {
		w.request_stop();
		w.detach();
	}
}
