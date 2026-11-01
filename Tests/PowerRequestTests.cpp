#include "pch.h"
#include "TestFramework.h"
#include <WinSys/PowerRequests.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// PowerRequests::ParseRequestList and ParseWakeTimers on buffers laid out as NtPowerInformation's (verified on Windows 11
// build 29698 against powercfg /requests and /waketimers): phnt's POWER_REQUEST V4 and WAKE_TIMER_INFO, the diagnostic
// buffer's strings at offsets from its start, a reason's from the reason's

namespace {
	// the same layouts as PowerRequests.cpp's
	struct Diagnostic {
		SIZE_T Size;
		ULONG CallerType;
		union {
			struct {
				SIZE_T ProcessImageNameOffset;
				ULONG ProcessId;
				ULONG ServiceTag;
			} Process;
			struct {
				SIZE_T DeviceDescriptionOffset;
				SIZE_T DevicePathOffset;
			} Device;
		};
		SIZE_T ReasonOffset;
	};
	struct RequestV4 {
		ULONG SupportedRequestMask;
		ULONG Counts[6];
		Diagnostic Diag;
	};
	struct Reason {
		ULONG Flags;
		union {
			struct {
				SIZE_T ResourceFileNameOffset;
				USHORT ResourceReasonId;
				ULONG StringCount;
				SIZE_T SubstitutionStringsOffset;
			} Detailed;
			SIZE_T SimpleStringOffset;
		};
	};
	struct WakeTimerInfo {
		SIZE_T OffsetToNext;
		LARGE_INTEGER DueTime;
		ULONG Period;
		Diagnostic Diag;
	};
	static_assert(sizeof(RequestV4) == 72 && sizeof(Diagnostic) == 40 && sizeof(Reason) == 32 && sizeof(WakeTimerInfo) == 64);

	// a buffer written in place: values at offsets, strings appended
	struct Buffer {
		std::vector<BYTE> Bytes;

		size_t Size() const {
			return Bytes.size();
		}
		void Align() {
			while (Bytes.size() % 8)
				Bytes.push_back(0);
		}
		template<typename T>
		size_t Append(T const& value) {
			Align();
			auto offset = Bytes.size();
			Bytes.resize(offset + sizeof(T));
			memcpy(Bytes.data() + offset, &value, sizeof(T));
			return offset;
		}
		size_t AppendString(std::wstring const& text) {
			Align();
			auto offset = Bytes.size();
			auto bytes = (text.size() + 1) * sizeof(WCHAR);
			Bytes.resize(offset + bytes);
			memcpy(Bytes.data() + offset, text.c_str(), bytes);
			return offset;
		}
		template<typename T>
		T& At(size_t offset) {
			return *reinterpret_cast<T*>(Bytes.data() + offset);
		}
	};

	// a request entry: its diagnostic strings and a simple reason; returns the entry's offset
	size_t AppendRequest(Buffer& b, ULONG const (&counts)[6], ULONG callerType, std::wstring const& name, std::wstring const& devicePath,
		uint32_t pid, std::wstring const& reason) {
		RequestV4 raw{};
		memcpy(raw.Counts, counts, sizeof(counts));
		raw.Diag.CallerType = callerType;
		raw.Diag.Size = sizeof(Diagnostic);
		auto entry = b.Append(raw);
		auto diag = entry + offsetof(RequestV4, Diag);
		auto nameAt = b.AppendString(name);
		size_t pathAt = devicePath.empty() ? 0 : b.AppendString(devicePath);
		Reason r{};
		r.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
		auto reasonAt = b.Append(r);
		auto textAt = b.AppendString(reason);
		b.At<Reason>(reasonAt).SimpleStringOffset = textAt - reasonAt;
		auto& d = b.At<RequestV4>(entry).Diag;
		if (callerType == 0) {
			d.Device.DeviceDescriptionOffset = nameAt - diag;
			d.Device.DevicePathOffset = pathAt ? pathAt - diag : 0;
		}
		else {
			d.Process.ProcessImageNameOffset = nameAt - diag;
			d.Process.ProcessId = pid;
		}
		d.ReasonOffset = reasonAt - diag;
		return entry;
	}

	// a request list of entries made by the callers (count, offsets, then the entries)
	Buffer RequestList(std::vector<std::function<size_t(Buffer&)>> const& entries) {
		Buffer b;
		b.Append((ULONG_PTR)entries.size());
		std::vector<size_t> offsetSlots;
		for (size_t i = 0; i < entries.size(); i++)
			offsetSlots.push_back(b.Append((ULONG_PTR)0));
		for (size_t i = 0; i < entries.size(); i++) {
			auto entry = entries[i](b);
			b.At<ULONG_PTR>(offsetSlots[i]) = entry;
		}
		return b;
	}
}

TEST(PowerRequests_ProcessAndDriverRequests) {
	auto b = RequestList({
		[](Buffer& b) { ULONG c[6]{ 0, 1, 0, 0, 0, 0 }; return AppendRequest(b, c, 1, L"C:\\Test\\app.exe", L"", 1234, L"Playing music"); },
		[](Buffer& b) { ULONG c[6]{ 2, 0, 0, 0, 0, 0 }; return AppendRequest(b, c, 0, L"USB Audio", L"USB\\VID_1234\\0", 0, L"An audio stream"); },
		});
	auto requests = PowerRequests::ParseRequestList(b.Bytes.data(), b.Size(), true);
	CHECK_EQ(requests.size(), 2u);
	if (requests.size() == 2) {
		auto& p = requests[0];
		CHECK_EQ(p.Kind, PowerRequester::Process);
		CHECK_EQ(p.Name, std::wstring(L"C:\\Test\\app.exe"));
		CHECK_EQ(p.ProcessId, 1234u);
		CHECK_EQ(p.Reason, std::wstring(L"Playing music"));
		CHECK_EQ(p.Counts[(int)PowerRequestType::System], 1u);
		CHECK_EQ(p.Counts[(int)PowerRequestType::Display], 0u);
		auto& d = requests[1];
		CHECK_EQ(d.Kind, PowerRequester::Driver);
		CHECK_EQ(d.Name, std::wstring(L"USB Audio"));
		CHECK_EQ(d.DevicePath, std::wstring(L"USB\\VID_1234\\0"));
		CHECK_EQ(d.Counts[(int)PowerRequestType::Display], 2u);
		CHECK_EQ(d.Reason, std::wstring(L"An audio stream"));
	}
}

TEST(PowerRequests_InactiveOnlyWhenAsked) {
	auto b = RequestList({
		[](Buffer& b) { ULONG c[6]{}; return AppendRequest(b, c, 0, L"Sleep Idle State Disabled", L"", 0, L"Power Manager"); },
		[](Buffer& b) { ULONG c[6]{ 0, 0, 0, 1, 0, 0 }; return AppendRequest(b, c, 1, L"C:\\x.exe", L"", 7, L"Busy"); },
		});
	auto active = PowerRequests::ParseRequestList(b.Bytes.data(), b.Size(), true);
	CHECK_EQ(active.size(), 1u);
	if (!active.empty())
		CHECK(active[0].IsActive());
	auto all = PowerRequests::ParseRequestList(b.Bytes.data(), b.Size(), false);
	CHECK_EQ(all.size(), 2u);
	if (all.size() == 2)
		CHECK(!all[0].IsActive());
}

TEST(PowerRequests_BadBuffersAreNotReadPast) {
	// a count the buffer can't hold, an offset past it, a buffer shorter than a count
	Buffer b;
	b.Append((ULONG_PTR)1000000);
	b.Append((ULONG_PTR)8);
	CHECK(PowerRequests::ParseRequestList(b.Bytes.data(), b.Size(), false).empty());
	Buffer c;
	c.Append((ULONG_PTR)1);
	c.Append((ULONG_PTR)4096);
	CHECK(PowerRequests::ParseRequestList(c.Bytes.data(), c.Size(), false).empty());
	BYTE tiny[4]{};
	CHECK(PowerRequests::ParseRequestList(tiny, sizeof(tiny), false).empty());
	// an entry cut short: no entry
	auto full = RequestList({ [](Buffer& b) { ULONG c[6]{ 1 }; return AppendRequest(b, c, 1, L"C:\\a.exe", L"", 1, L"r"); } });
	CHECK(PowerRequests::ParseRequestList(full.Bytes.data(), 16 + sizeof(RequestV4) - 8, false).empty());
}

TEST(PowerRequests_DetailedReasonWithoutItsModule) {
	// a reason from a resource DLL that isn't there: its name and ID
	Buffer b;
	b.Append((ULONG_PTR)1);
	auto slot = b.Append((ULONG_PTR)0);
	RequestV4 raw{};
	raw.Counts[1] = 1;
	raw.Diag.Size = sizeof(Diagnostic);
	raw.Diag.CallerType = 1;
	auto entry = b.Append(raw);
	b.At<ULONG_PTR>(slot) = entry;
	auto diag = entry + offsetof(RequestV4, Diag);
	auto nameAt = b.AppendString(L"C:\\x.exe");
	Reason r{};
	r.Flags = POWER_REQUEST_CONTEXT_DETAILED_STRING;
	r.Detailed.ResourceReasonId = 7;
	auto reasonAt = b.Append(r);
	auto moduleAt = b.AppendString(L"no_such_module_for_tests.dll");
	b.At<Reason>(reasonAt).Detailed.ResourceFileNameOffset = moduleAt - reasonAt;
	b.At<RequestV4>(entry).Diag.Process.ProcessImageNameOffset = nameAt - diag;
	b.At<RequestV4>(entry).Diag.ReasonOffset = reasonAt - diag;
	auto requests = PowerRequests::ParseRequestList(b.Bytes.data(), b.Size(), true);
	CHECK_EQ(requests.size(), 1u);
	if (!requests.empty())
		CHECK_EQ(requests[0].Reason, std::wstring(L"no_such_module_for_tests.dll #7"));
}

TEST(PowerRequests_WakeTimers) {
	Buffer b;
	WakeTimerInfo first{};
	first.DueTime.QuadPart = 6000000000LL;	// ten minutes left
	first.Diag.Size = sizeof(Diagnostic);
	first.Diag.CallerType = 2;	// a service (tag 0: no name looked up)
	first.Diag.Process.ProcessId = 2348;
	auto firstAt = b.Append(first);
	auto nameAt = b.AppendString(L"C:\\Windows\\System32\\svchost.exe");
	Reason r{};
	r.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
	auto reasonAt = b.Append(r);
	auto textAt = b.AppendString(L"Maintenance");
	b.At<Reason>(reasonAt).SimpleStringOffset = textAt - reasonAt;
	auto diag = firstAt + offsetof(WakeTimerInfo, Diag);
	b.At<WakeTimerInfo>(firstAt).Diag.Process.ProcessImageNameOffset = nameAt - diag;
	b.At<WakeTimerInfo>(firstAt).Diag.ReasonOffset = reasonAt - diag;
	// the second: periodic, the last (no next)
	b.Align();
	auto secondAt = b.Size();
	b.At<WakeTimerInfo>(firstAt).OffsetToNext = secondAt - firstAt;
	WakeTimerInfo second{};
	second.DueTime.QuadPart = 10000000;
	second.Period = 3600000;
	second.Diag.Size = sizeof(Diagnostic);
	second.Diag.CallerType = 1;
	second.Diag.Process.ProcessId = 99;
	b.Append(second);

	int64_t now = 133000000000000000LL;
	auto timers = PowerRequests::ParseWakeTimers(b.Bytes.data(), b.Size(), now);
	CHECK_EQ(timers.size(), 2u);
	if (timers.size() == 2) {
		CHECK_EQ(timers[0].DueTime, now + 6000000000LL);
		CHECK_EQ(timers[0].Kind, PowerRequester::Service);
		CHECK_EQ(timers[0].ProcessId, 2348u);
		CHECK_EQ(timers[0].Name, std::wstring(L"C:\\Windows\\System32\\svchost.exe"));
		CHECK_EQ(timers[0].Reason, std::wstring(L"Maintenance"));
		CHECK_EQ(timers[1].PeriodMs, 3600000u);
		CHECK_EQ(timers[1].Kind, PowerRequester::Process);
	}
	// an empty list is one entry with nothing in it
	WakeTimerInfo empty{};
	CHECK(PowerRequests::ParseWakeTimers(reinterpret_cast<uint8_t const*>(&empty), sizeof(empty), now).empty());
}

TEST(PowerRequests_LiveSystem) {
	// the real queries, which need elevation (as powercfg's): they succeed, and the timer resolution is sane
	if (auto r = PowerRequests::GetTimerResolution()) {
		CHECK(r->Finest <= r->Current);
		CHECK(r->Current <= r->Coarsest);
	}
	else
		CHECK(false);
	if (SecurityHelper::IsRunningElevated()) {
		CHECK(PowerRequests::Enum(false).has_value());
		CHECK(PowerRequests::EnumWakeTimers().has_value());
	}
}
