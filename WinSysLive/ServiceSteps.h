#pragma once

#include <WinSys/Service.h>
#include <deque>

//
// Stop and Restart with running dependents, shared by the Services view and the process sheet's Services page: the
// dependents are stopped first (in the SCM's order), then the service; a restart then starts the service and the
// dependents that ran, in the reverse order. One control at a time, each waited for (the caller polls with a timer, so
// the UI doesn't block).
//
class ServiceControlSteps {
public:
	// the stop (and start, for a restart) of a service with its running dependents, after a confirmation if it has any;
	// false if canceled, or after showing why they can't be listed
	bool Plan(HWND owner, std::wstring const& name, std::wstring const& displayName, bool restart);
	// sends the controls until one has to be waited for: true while steps remain (call it again from a timer). A stop
	// that fails or times out ends the steps; the starts that failed are reported at the end
	bool Run(HWND owner);
	bool IsBusy() const {
		return !m_Steps.empty();
	}

	// "<operation> failed: <error>", with the elevation hint for a denied access
	static void ShowError(HWND owner, PCWSTR operation, DWORD error);

private:
	struct Step {
		std::wstring Name;
		bool Start;
	};
	std::deque<Step> m_Steps;
	bool m_Issued{ false };	// the first step's control was sent: waiting for its state
	ULONGLONG m_Deadline{ 0 };
	std::wstring m_Errors;	// starts that failed
};
