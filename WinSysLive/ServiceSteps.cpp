#include "pch.h"
#include "resource.h"
#include "ServiceSteps.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

namespace {
	// how long each service is waited for to stop or start
	const ULONGLONG StepTimeout = 30000;
}

void ServiceControlSteps::ShowError(HWND owner, PCWSTR operation, DWORD error) {
	auto text = std::format(L"{} failed", operation);
	if (error)
		text += L": " + Helpers::GetErrorText(error);
	if (error == ERROR_ACCESS_DENIED && !SecurityHelper::IsRunningElevated())
		text += L"\nControlling most services requires running as administrator (File > Run as Administrator).";
	AtlMessageBox(owner, text.c_str(), IDR_MAINFRAME, MB_ICONERROR);
}

bool ServiceControlSteps::Plan(HWND owner, std::wstring const& name, std::wstring const& displayName, bool restart) {
	if (IsBusy())
		return false;
	auto service = WinSys::Service::Open(name, ServiceAccessMask::EnumerateDependents);
	if (!service) {
		ShowError(owner, L"Opening the service", ::GetLastError());
		return false;
	}
	auto dependents = service->GetDependents(ServiceEnumState::Active);
	if (dependents.empty() && ::GetLastError() != ERROR_SUCCESS) {
		ShowError(owner, L"Listing the dependent services", ::GetLastError());
		return false;
	}
	if (!dependents.empty()) {
		auto text = std::format(L"{} {} also stops the services that depend on it:\n", restart ? L"Restarting" : L"Stopping", displayName);
		for (size_t i = 0; i < dependents.size() && i < 20; i++)
			text += std::format(L"\n    {}", dependents[i].DisplayName);
		if (dependents.size() > 20)
			text += std::format(L"\n    ...and {} more", dependents.size() - 20);
		text += restart ? L"\n\nThey're started again after it." : L"\n\nStop them all?";
		if (AtlMessageBox(owner, text.c_str(), IDR_MAINFRAME, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
			return false;
	}
	// the SCM's order is the order to stop them in
	for (auto& d : dependents)
		m_Steps.push_back({ d.Name, false });
	m_Steps.push_back({ name, false });
	if (restart) {
		m_Steps.push_back({ name, true });
		// those that ran: started after the services they depend on
		for (auto it = dependents.rbegin(); it != dependents.rend(); ++it)
			if (it->State == ServiceState::Running)
				m_Steps.push_back({ it->Name, true });
	}
	m_Issued = false;
	m_Errors.clear();
	return true;
}

bool ServiceControlSteps::Run(HWND owner) {
	// a message box lets the caller's timer call again: the steps are gone before one shows
	while (!m_Steps.empty()) {
		auto step = m_Steps.front();
		auto state = ServiceManager::GetServiceState(step.Name);
		if (!m_Issued) {
			if (step.Start ? state == ServiceState::Running : state == ServiceState::Stopped) {
				m_Steps.pop_front();
				continue;
			}
			// stopping already: waited for as if stopped here
			bool stopping = !step.Start && state == ServiceState::StopPending;
			auto service = stopping ? nullptr : WinSys::Service::Open(step.Name, step.Start ? ServiceAccessMask::Start : ServiceAccessMask::Stop);
			bool ok = stopping || (service && (step.Start ? service->Start() : service->Stop()));
			if (!ok) {
				auto error = ::GetLastError();
				if (step.Start) {
					// the others may still start
					if (error != ERROR_SERVICE_ALREADY_RUNNING)
						m_Errors += std::format(L"\n{}: {}", step.Name, Helpers::GetErrorText(error));
					m_Steps.pop_front();
					continue;
				}
				if (error != ERROR_SERVICE_NOT_ACTIVE) {
					m_Steps.clear();
					ShowError(owner, std::format(L"Stopping {}", step.Name).c_str(), error);
					return false;
				}
				m_Steps.pop_front();
				continue;
			}
			m_Issued = true;
			m_Deadline = ::GetTickCount64() + StepTimeout;
			return true;
		}
		// waiting for the control sent
		bool done = step.Start ? state != ServiceState::StartPending : state == ServiceState::Stopped;
		if (!done) {
			if (::GetTickCount64() < m_Deadline)
				return true;
			if (!step.Start) {
				m_Steps.clear();
				m_Issued = false;
				AtlMessageBox(owner, std::format(L"Service {} did not stop in time; the rest is left as it is.", step.Name).c_str(),
					IDR_MAINFRAME, MB_ICONWARNING);
				return false;
			}
			m_Errors += std::format(L"\n{}: still starting", step.Name);
		}
		else if (step.Start && state != ServiceState::Running)
			m_Errors += std::format(L"\n{}: stopped while starting", step.Name);
		m_Issued = false;
		m_Steps.pop_front();
	}
	if (!m_Errors.empty()) {
		auto text = L"Some services didn't start again:" + m_Errors;
		m_Errors.clear();
		AtlMessageBox(owner, text.c_str(), IDR_MAINFRAME, MB_ICONWARNING);
	}
	return false;
}
