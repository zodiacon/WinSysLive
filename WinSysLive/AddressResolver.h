#pragma once

#include <WinSys/Connections.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// posted to a window whose lookup finished (it repaints; the next GetText finds the name)
constexpr UINT WM_ADDRESS_RESOLVED = WM_APP + 0x150;

//
// Options > Resolve Addresses: the DNS names of the Network view's and the TCP/IP pages' addresses (WinSys
// ConnectionInfo::LookupName), looked up on worker threads of its own, as a lookup can take seconds, and cached for
// the process's life (a name not found is cached too). Called from the UI thread and the property sheets' threads.
//
class AddressResolver {
public:
	static AddressResolver& Get();

	// from the setting, at startup and when it changes (off: the addresses show, and nothing is looked up)
	void Enable(bool enable);
	bool IsEnabled() const;

	// the text to show for an address (text: its formatted address, empty for none): its name once found, else the
	// address. A lookup not done yet is queued, and notify gets WM_ADDRESS_RESOLVED when it's done
	std::wstring GetText(std::array<BYTE, 16> const& address, bool v6, uint32_t scope, std::wstring const& text, HWND notify);
	// the local and remote addresses of a connection
	std::wstring GetLocalText(WinSys::Connection const& c, HWND notify);
	std::wstring GetRemoteText(WinSys::Connection const& c, HWND notify);

	// stops the workers (before the app exits), without waiting for a lookup under way
	void Shutdown();

private:
	AddressResolver() = default;
	void Work(std::stop_token stop);

	struct Key {
		std::array<BYTE, 16> Address;
		uint32_t Scope;
		bool V6;
		auto operator<=>(Key const&) const = default;
	};
	struct Entry {
		bool Done{ false };
		std::wstring Name;	// empty: none
		std::vector<HWND> Waiting;	// to notify once done
	};

	mutable std::mutex m_Lock;
	std::condition_variable_any m_Wake;
	std::map<Key, Entry> m_Cache;
	std::deque<Key> m_Queue;
	std::vector<std::jthread> m_Workers;	// started on the first lookup; workers wait on m_Wake with their stop tokens
	std::atomic<bool> m_Enabled{ false };
	bool m_Shutdown{ false };
};
