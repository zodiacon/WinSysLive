#include "pch.h"
#include "AddressResolver.h"

namespace {
	// lookups of addresses without a name take seconds, so a few run at once
	int const WorkerCount = 4;
	// how long an answer is kept before it's looked up again: a name, none (a failure may be a passing DNS problem)
	ULONGLONG const NameLifetime = 60 * 60 * 1000, NoNameLifetime = 5 * 60 * 1000;
	// entries not asked for this long go; pruning runs at most this often
	ULONGLONG const UnusedLifetime = 60 * 60 * 1000, PruneInterval = 60 * 1000;

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

AddressResolver::Key AddressResolver::MakeKey(std::array<BYTE, 16> const& address, bool v6, uint32_t scope) {
	Key key{ address, v6 ? scope : 0, v6 };
	if (!v6)
		std::fill(key.Address.begin() + 4, key.Address.end(), BYTE(0));
	return key;
}

std::wstring AddressResolver::GetText(std::array<BYTE, 16> const& address, bool v6, uint32_t scope, std::wstring const& text, HWND notify) {
	if (!m_Enabled || text.empty() || WinSys::ConnectionInfo::IsUnspecified(address, v6))
		return text;
	auto key = MakeKey(address, v6, scope);
	auto now = ::GetTickCount64();
	std::lock_guard lock(m_Lock);
	if (m_Shutdown)
		return text;
	Prune(now);
	auto& entry = m_Cache[key];
	entry.Used = now;
	bool due = !entry.Done || entry.Answered == 0 || now - entry.Answered > (entry.Name.empty() ? NoNameLifetime : NameLifetime);
	if (due && notify && std::ranges::find(entry.Waiting, notify) == entry.Waiting.end())
		entry.Waiting.push_back(notify);
	if (due && !entry.Pending) {
		entry.Pending = true;
		m_Queue.push_back(key);
		if (m_Workers.empty())
			for (int i = 0; i < WorkerCount; i++)
				m_Workers.emplace_back([this](std::stop_token stop) { Work(stop); });
		m_Wake.notify_one();
	}
	return entry.Done && !entry.Name.empty() ? entry.Name : text;
}

std::wstring AddressResolver::GetLocalText(WinSys::Connection const& c, HWND notify) {
	return GetText(c.Id.LocalAddress, IsV6(c.Id.Proto), c.Id.LocalScope, c.LocalText, notify);
}

std::wstring AddressResolver::GetRemoteText(WinSys::Connection const& c, HWND notify) {
	return GetText(c.Id.RemoteAddress, IsV6(c.Id.Proto), c.Id.RemoteScope, c.RemoteText, notify);
}

void AddressResolver::Refresh(WinSys::Connection const& c) {
	bool v6 = IsV6(c.Id.Proto);
	std::lock_guard lock(m_Lock);
	for (auto& key : { MakeKey(c.Id.LocalAddress, v6, c.Id.LocalScope), MakeKey(c.Id.RemoteAddress, v6, c.Id.RemoteScope) })
		if (auto it = m_Cache.find(key); it != m_Cache.end())
			it->second.Answered = 0;
}

void AddressResolver::Prune(ULONGLONG now) {
	if (now - m_LastPrune < PruneInterval)
		return;
	m_LastPrune = now;
	std::erase_if(m_Cache, [&](auto const& item) {
		auto& entry = item.second;
		return !entry.Pending && now - entry.Used > UnusedLifetime;
		});
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
			// pending entries aren't pruned
			auto& entry = m_Cache[key];
			entry.Done = true;
			entry.Pending = false;
			entry.Name = std::move(name);
			entry.Answered = ::GetTickCount64();
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
