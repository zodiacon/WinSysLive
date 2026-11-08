#include "pch.h"
#include "VirtualMachineLookup.h"
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace WinSys;

namespace {
	// a read missing an ID is asked for again after this; after a failed read (not elevated), after the longer one
	const ULONGLONG RetryMs = 5000, RetryFailedMs = 60000;

	// the thread starts with the first request and stops at exit (the static's destructor)
	class Worker {
	public:
		VirtualMachineLookup::Result Get() {
			std::lock_guard lock(m_Lock);
			return m_Result;
		}

		std::optional<ComputeSystem> Find(std::wstring const& id) {
			std::unique_lock lock(m_Lock);
			auto it = std::ranges::find(m_Result.Systems, id, &ComputeSystem::Id);
			if (it != m_Result.Systems.end())
				return *it;
			// unknown: a VM that started since, or no read yet
			auto now = ::GetTickCount64();
			if (!m_Pending && (!m_Asked || now - m_Asked >= (m_Result.Error ? RetryFailedMs : RetryMs)))
				Ask(lock, nullptr, 0);
			return std::nullopt;
		}

		void Refresh(HWND window, UINT message) {
			std::unique_lock lock(m_Lock);
			Ask(lock, window, message);
		}

	private:
		void Ask(std::unique_lock<std::mutex>& lock, HWND window, UINT message) {
			m_Asked = ::GetTickCount64();
			m_Pending = true;
			if (window)
				m_Notify.emplace_back(window, message);
			if (!m_Thread.joinable())
				m_Thread = std::jthread([this](std::stop_token stop) { Run(stop); });
			lock.unlock();
			m_Wake.notify_one();
		}

		void Run(std::stop_token stop) {
			::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			while (!stop.stop_requested()) {
				{
					std::unique_lock lock(m_Lock);
					if (!m_Wake.wait(lock, stop, [this] { return m_Pending; }))
						break;
					m_Pending = false;
				}
				auto systems = VirtualMachines::Enumerate();
				auto error = systems ? 0 : ::GetLastError();
				std::vector<std::pair<HWND, UINT>> notify;
				{
					std::lock_guard lock(m_Lock);
					m_Result.Systems = systems ? std::move(*systems) : std::vector<ComputeSystem>();
					m_Result.Error = systems ? 0 : (error ? error : ERROR_ACCESS_DENIED);
					m_Result.Read = true;
					m_Result.Generation++;
					notify.swap(m_Notify);
				}
				for (auto& [window, message] : notify)
					if (::IsWindow(window))
						::PostMessage(window, message, 0, 0);
			}
			::CoUninitialize();
		}

		std::mutex m_Lock;
		std::condition_variable_any m_Wake;
		VirtualMachineLookup::Result m_Result;
		std::vector<std::pair<HWND, UINT>> m_Notify;
		ULONGLONG m_Asked{ 0 };
		bool m_Pending{ false };
		std::jthread m_Thread;	// last: stopped and joined before the rest goes
	};

	Worker s_Worker;

	bool StartsWith(std::wstring const& text, PCWSTR prefix) {
		return _wcsnicmp(text.c_str(), prefix, wcslen(prefix)) == 0;
	}
}

VirtualMachineLookup::Result VirtualMachineLookup::Get() {
	return s_Worker.Get();
}

void VirtualMachineLookup::Refresh(HWND window, UINT message) {
	s_Worker.Refresh(window, message);
}

std::optional<ComputeSystem> VirtualMachineLookup::Find(std::wstring const& id) {
	return id.empty() ? std::nullopt : s_Worker.Find(id);
}

PCWSTR VirtualMachineLookup::RoleOf(ProcessInfoEx& p) {
	auto& name = p.GetImageName();
	if (_wcsicmp(name.c_str(), L"vmwp.exe") == 0)
		return L"worker process";
	if (_wcsicmp(name.c_str(), L"vmsp.exe") == 0)
		return L"security process";
	if (_wcsicmp(name.c_str(), L"wslhost.exe") == 0)
		return L"WSL host";
	if (StartsWith(name, L"vmmem"))
		return L"memory process";
	return L"";
}

std::wstring VirtualMachineLookup::IdOf(::ProcessManager const& pm, ProcessInfoEx& p) {
	auto& name = p.GetImageName();
	if (_wcsicmp(name.c_str(), L"vmwp.exe") == 0 || _wcsicmp(name.c_str(), L"vmsp.exe") == 0) {
		// the account is in the snapshot when elevated (no process to open); vmsp.exe has no command line
		auto id = VirtualMachines::IdFromAccount(p.GetUser());
		return id.empty() && _wcsicmp(name.c_str(), L"vmwp.exe") == 0 ? VirtualMachines::IdFromCommandLine(p.GetCommandLine()) : id;
	}
	if (_wcsicmp(name.c_str(), L"wslhost.exe") == 0)
		return VirtualMachines::IdFromCommandLine(p.GetCommandLine());
	if (!StartsWith(name, L"vmmem"))
		return {};
	// a minimal process: no account, no command line; its worker created it
	if (auto parent = pm.GetProcessById(p.ParentId); parent && parent->Key.Created <= p.Key.Created
		&& _wcsicmp(parent->GetImageName().c_str(), L"vmwp.exe") == 0)
		return IdOf(pm, *parent);
	// Windows Sandbox's template, made at startup by a worker gone since: the one template HCS has
	if (_wcsicmp(name.c_str(), L"vmmemCmZygote") == 0) {
		auto result = s_Worker.Get();
		if (!result.Read)
			Find(L"?");	// asks for the first read
		std::wstring id;
		for (auto& s : result.Systems)
			if (_wcsicmp(s.Owner.c_str(), L"CmService") == 0 && s.State == L"SavedAsTemplate") {
				if (!id.empty())
					return {};	// more than one: which isn't known
				id = s.Id;
			}
		return id;
	}
	return {};
}

std::wstring VirtualMachineLookup::TextOf(::ProcessManager const& pm, ProcessInfoEx& p) {
	auto id = IdOf(pm, p);
	if (id.empty())
		return {};
	if (auto system = Find(id)) {
		// "WSL 2", not "WSL 2 (WSL 2)"
		auto name = VirtualMachines::DisplayName(*system), kind = VirtualMachines::KindText(*system);
		return name == kind ? name : name + L" (" + kind + L")";
	}
	// unknown (not elevated: HCS refuses): WSL's own processes say whose they are
	if (_wcsicmp(p.GetImageName().c_str(), L"wslhost.exe") == 0 || _wcsicmp(p.GetImageName().c_str(), L"vmmemWSL") == 0)
		return L"WSL 2";
	return id;
}
