#include "pch.h"
#include "SignatureVerifier.h"

SignatureVerifier::~SignatureVerifier() {
	m_Worker.request_stop();
	m_State->Wake.notify_all();
	if (m_Worker.joinable())
		m_Worker.detach();
}

void SignatureVerifier::Init(HWND notify, UINT message) {
	m_State->Notify = notify;
	m_State->Message = message;
}

std::wstring SignatureVerifier::Lowercase(std::wstring text) {
	::CharLowerBuff(text.data(), (DWORD)text.size());
	return text;
}

void SignatureVerifier::Verify(std::wstring const& path) {
	if (path.empty() || !m_Queued.insert(Lowercase(path)).second)
		return;
	{
		std::lock_guard lock(m_State->Lock);
		m_State->Pending.push_back(path);
	}
	if (!m_Worker.joinable())
		m_Worker = std::jthread(Run, m_State);
	m_State->Wake.notify_one();
}

std::optional<WinSys::SignatureInfo> SignatureVerifier::GetResult(std::wstring const& path) const {
	std::lock_guard lock(m_State->Lock);
	auto it = m_State->Results.find(Lowercase(path));
	if (it == m_State->Results.end())
		return std::nullopt;
	return it->second;
}

void SignatureVerifier::Run(std::stop_token stop, std::shared_ptr<State> state) {
	// WinVerifyTrust's catalog lookups may use COM
	::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	for (;;) {
		std::wstring path;
		{
			std::unique_lock lock(state->Lock);
			if (!state->Wake.wait(lock, stop, [&] { return !state->Pending.empty(); }))
				break;
			path = std::move(state->Pending.front());
			state->Pending.pop_front();
		}
		auto info = WinSys::FileSignature::Verify(path.c_str());
		if (stop.stop_requested())
			break;
		{
			std::lock_guard lock(state->Lock);
			state->Results[Lowercase(path)] = std::move(info);
		}
		::PostMessage(state->Notify, state->Message, 0, 0);
	}
	::CoUninitialize();
}
