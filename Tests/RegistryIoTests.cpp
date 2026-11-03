#include "pch.h"
#include "TestFramework.h"
#include <WinSys/RegistryIo.h>

using namespace WinSys;

// RegistryIoParser on payloads in Microsoft-Windows-Kernel-Registry's layouts (version 0, as TdhGetManifestEventInformation
// gives them and a trace of reg.exe showed them: KeyName empty but in opens, RelativeName absolute or relative to the
// base object, STATUS_REPARSE opens retried), RegistryIoTable's naming and counting, and a live tracker (elevated only)

namespace {
	using Op = RegistryOperation;

	struct Payload {
		std::vector<uint8_t> Bytes;
		size_t PointerSize{ 8 };

		Payload& Pointer(uint64_t value) {
			auto p = reinterpret_cast<uint8_t const*>(&value);
			Bytes.insert(Bytes.end(), p, p + PointerSize);
			return *this;
		}
		Payload& UInt32(uint32_t value) {
			auto p = reinterpret_cast<uint8_t const*>(&value);
			Bytes.insert(Bytes.end(), p, p + sizeof(value));
			return *this;
		}
		Payload& String(std::wstring const& text) {
			auto p = reinterpret_cast<uint8_t const*>(text.c_str());
			Bytes.insert(Bytes.end(), p, p + (text.size() + 1) * sizeof(wchar_t));
			return *this;
		}
		std::optional<RegistryEvent> Parse(Op op) const {
			return RegistryIoParser::Parse((uint16_t)op, Bytes.data(), Bytes.size(), PointerSize);
		}
	};

	// Open and Create: BaseObject, KeyObject, Status, Disposition, BaseName, RelativeName
	RegistryEvent Open(uint64_t base, uint64_t key, std::wstring const& name, uint32_t status = 0, Op op = Op::OpenKey, uint32_t disposition = 0) {
		return *Payload{}.Pointer(base).Pointer(key).UInt32(status).UInt32(disposition).String(L"").String(name).Parse(op);
	}

	// QueryValue: KeyObject, Status, InfoClass, DataSize, KeyName, ValueName, CapturedDataSize, CapturedData
	RegistryEvent QueryValue(uint64_t key, std::wstring const& value, uint32_t status = 0) {
		return *Payload{}.Pointer(key).UInt32(status).UInt32(2).UInt32(22).String(L"").String(value).Parse(Op::QueryValue);
	}

	// the event header's time stamp, which orders the events
	RegistryEvent At(RegistryEvent e, int64_t stamp) {
		e.Stamp = stamp;
		return e;
	}

	RegistryIoCounters const* Find(std::vector<RegistryIoCounters> const& keys, std::wstring const& key) {
		auto it = std::ranges::find_if(keys, [&](auto& k) { return ::_wcsicmp(k.Key.c_str(), key.c_str()) == 0; });
		return it == keys.end() ? nullptr : &*it;
	}
}

TEST(Registry_ParseOpen) {
	auto e = Payload{}.Pointer(0xFFFFF68A5DAC2C70).Pointer(0xFFFFF68AD247DC70).UInt32(0).UInt32(0)
		.String(L"").String(L"Software\\ScorpioSoftware\\WinSysLive").Parse(Op::OpenKey);
	CHECK(e.has_value());
	CHECK(e->Operation == Op::OpenKey);
	CHECK_EQ(e->BaseObject, 0xFFFFF68A5DAC2C70ull);
	CHECK_EQ(e->KeyObject, 0xFFFFF68AD247DC70ull);
	CHECK_EQ(e->Status, 0u);
	CHECK(e->Name == L"Software\\ScorpioSoftware\\WinSysLive");

	// a failed open has no key object; a created key says so
	auto failed = Open(0x1000, 0, L"\\REGISTRY\\MACHINE\\SOFTWARE\\Missing", 0xC0000034);
	CHECK_EQ(failed.KeyObject, 0ull);
	CHECK_EQ(failed.Status, 0xC0000034u);
	auto created = Open(0x1000, 0x2000, L"\\REGISTRY\\MACHINE\\SOFTWARE\\New", 0, Op::CreateKey, 1);
	CHECK(created.Operation == Op::CreateKey);
	CHECK_EQ(created.Disposition, 1u);

	// a 32-bit header's pointers are 4 bytes
	Payload narrow{ {}, 4 };
	auto e32 = narrow.Pointer(0x1234).Pointer(0x5678).UInt32(0x104).UInt32(0).String(L"").String(L"\\REGISTRY\\MACHINE").Parse(Op::OpenKey);
	CHECK_EQ(e32->BaseObject, 0x1234ull);
	CHECK_EQ(e32->KeyObject, 0x5678ull);
	CHECK(RegistryIoParser::IsRetried(e32->Status));
	CHECK(e32->Name == L"\\REGISTRY\\MACHINE");
}

TEST(Registry_ParseOperations) {
	auto query = QueryValue(0xAB00, L"OEMCP");
	CHECK(query.Operation == Op::QueryValue);
	CHECK_EQ(query.KeyObject, 0xAB00ull);
	CHECK(query.ValueName == L"OEMCP");

	// SetValue: KeyObject, Status, Type, DataSize, KeyName, ValueName, then the captured data
	auto set = Payload{}.Pointer(0xAB00).UInt32(0).UInt32(REG_SZ).UInt32(8).String(L"").String(L"Theme").UInt32(8).Parse(Op::SetValue);
	CHECK(set->ValueName == L"Theme");
	// DeleteValue: KeyObject, Status, KeyName, ValueName
	auto del = Payload{}.Pointer(0xAB00).UInt32(0xC0000034).String(L"").String(L"Gone").Parse(Op::DeleteValue);
	CHECK(del->ValueName == L"Gone");
	CHECK_EQ(del->Status, 0xC0000034u);
	// EnumerateKey: KeyObject, Status, Index, InfoClass, DataSize, KeyName
	auto enumerate = Payload{}.Pointer(0xAB00).UInt32(0x8000001A).UInt32(5).UInt32(0).UInt32(0).String(L"").Parse(Op::EnumerateKey);
	CHECK_EQ(enumerate->KeyObject, 0xAB00ull);
	CHECK(!RegistryIoParser::IsFailure(enumerate->Status));
	CHECK(RegistryIoParser::ResultName(enumerate->Status) == L"NO MORE ENTRIES");

	// not an operation's event; a payload cut short reads as zeros
	CHECK(!RegistryIoParser::Parse(16, nullptr, 0, 8));
	CHECK(!RegistryIoParser::Parse(0, nullptr, 0, 8));
	uint8_t shortPayload[3]{ 1, 2, 3 };
	auto cut = RegistryIoParser::Parse((uint16_t)Op::QueryValue, shortPayload, sizeof(shortPayload), 8);
	CHECK(cut && cut->KeyObject == 0 && cut->ValueName.empty());

	// a size probe isn't a failure, a missing name is
	CHECK(!RegistryIoParser::IsFailure(0xC0000023));
	CHECK(!RegistryIoParser::IsFailure(0x80000005));
	CHECK(RegistryIoParser::IsFailure(0xC0000034));
	CHECK(RegistryIoParser::ResultName(0xC0000034) == L"NAME NOT FOUND");
	CHECK(RegistryIoParser::ResultName(0xC0001234) == L"0xC0001234");
	CHECK(std::wstring(RegistryIoParser::OperationName(Op::QueryValue)) == L"Query Value");
}

TEST(Registry_TableNamesFromOpens) {
	RegistryIoTable table;
	// retried after a reparse: counted once, by its second open
	table.OnEvent(Open(0x10, 0, L"\\REGISTRY\\MACHINE\\SYSTEM\\ControlSet001\\Control\\Nls\\CodePage", 0x104), 100, 1, 1);
	table.OnEvent(Open(0x10, 0xA0, L"\\REGISTRY\\MACHINE\\SYSTEM\\ControlSet001\\Control\\Nls\\CodePage"), 100, 2, 2);
	table.OnEvent(QueryValue(0xA0, L"ACP"), 100, 3, 3);
	table.OnEvent(QueryValue(0xA0, L"OEMCP"), 100, 4, 4);
	// the same key in another case, by another open: the same row
	table.OnEvent(Open(0x10, 0xA8, L"\\Registry\\Machine\\System\\ControlSet001\\Control\\NLS\\CodePage"), 100, 5, 5);
	// a missing key: an open and a failure, and no key object named
	table.OnEvent(Open(0x10, 0, L"\\REGISTRY\\MACHINE\\SOFTWARE\\Missing", 0xC0000034), 100, 6, 6);
	// another process's open of the same key: its own row
	table.OnEvent(Open(0x10, 0xB0, L"\\REGISTRY\\MACHINE\\SYSTEM\\ControlSet001\\Control\\Nls\\CodePage"), 200, 7, 7);
	// a close and a flush don't count
	table.OnEvent(*Payload{}.Pointer(0xA0).UInt32(0).String(L"").Parse(Op::CloseKey), 100, 8, 8);

	CHECK_EQ(table.GetDeferredCount(), (size_t)0);
	auto keys = table.GetKeys();
	CHECK_EQ(keys.size(), (size_t)3);
	auto codePage = std::ranges::find_if(keys, [](auto& k) { return k.Pid == 100 && k.Key.ends_with(L"CodePage"); });
	CHECK(codePage != keys.end());
	if (codePage != keys.end()) {
		CHECK(codePage->Key == L"HKLM\\SYSTEM\\ControlSet001\\Control\\Nls\\CodePage");
		CHECK_EQ(codePage->Opens, 2ull);
		CHECK_EQ(codePage->Reads, 2ull);
		CHECK_EQ(codePage->Writes, 0ull);
		CHECK_EQ(codePage->Failures, 0ull);
		CHECK(codePage->LastValue == L"OEMCP");
		CHECK(codePage->LastOperation == Op::OpenKey);
		CHECK_EQ(codePage->FirstSeen, 2ll);
		CHECK_EQ(codePage->LastSeen, 5ll);
	}
	auto missing = Find(keys, L"HKLM\\SOFTWARE\\Missing");
	CHECK(missing && missing->Opens == 1 && missing->Failures == 1 && missing->LastStatus == 0xC0000034);

	// a new key; a deleted one
	table.OnEvent(Open(0x10, 0xC0, L"\\REGISTRY\\MACHINE\\SOFTWARE\\New", 0, Op::CreateKey, 1), 100, 9, 9);
	table.OnEvent(*Payload{}.Pointer(0xC0).UInt32(0).String(L"").Parse(Op::DeleteKey), 100, 10, 10);
	keys = table.GetKeys();
	auto created = Find(keys, L"HKLM\\SOFTWARE\\New");
	CHECK(created && created->Opens == 1 && created->Writes == 2);

	table.RemoveProcess(200);
	CHECK_EQ(table.GetKeys().size(), (size_t)3);
	// idle for a minute: forgotten
	table.Prune(10 + RegistryIoTable::KeyIdle + 1);
	CHECK(table.GetKeys().empty());
}

TEST(Registry_TableDeferredUntilNamed) {
	RegistryIoTable table;
	// HKCU's predefined handle, opened before the session: its key object isn't known
	const uint64_t hkcu = 0x5DAC2C70, key = 0xD247DC70, old = 0x423D4970;
	table.OnEvent(Open(hkcu, key, L"Software\\ScorpioSoftware\\WinSysLive"), 300, 1, 1);
	table.OnEvent(QueryValue(key, L"ActiveTab"), 300, 2, 2);
	table.OnEvent(QueryValue(old, L"41911A28A3BC10B5"), 300, 3, 3);
	CHECK(table.GetKeys().empty());
	CHECK_EQ(table.GetDeferredCount(), (size_t)3);
	// the resolver gets the base and the other key object; the query on the opened key waits for its open
	auto unnamed = table.TakeUnnamed();
	std::ranges::sort(unnamed);
	std::vector<uint64_t> expected{ hkcu, old };
	std::ranges::sort(expected);
	CHECK(unnamed == expected);
	CHECK(table.TakeUnnamed().empty());

	// the resolver names the base: the open, then the query on the key it opened, count under the full name
	table.SetName(hkcu, L"HKCU", 4);
	table.NameFailed(old);
	table.ApplyDeferred();
	auto keys = table.GetKeys();
	auto settings = Find(keys, L"HKCU\\Software\\ScorpioSoftware\\WinSysLive");
	CHECK(settings && settings->Opens == 1 && settings->Reads == 1 && settings->LastValue == L"ActiveTab");
	CHECK_EQ(table.GetDeferredCount(), (size_t)1);
	// what still waits goes to the resolver again
	CHECK(table.TakeUnnamed() == std::vector<uint64_t>({ old }));

	// later operations on the opened key count at once
	table.OnEvent(QueryValue(key, L"Tabs"), 300, 5, 5);
	keys = table.GetKeys();
	settings = Find(keys, L"HKCU\\Software\\ScorpioSoftware\\WinSysLive");
	CHECK(settings && settings->Reads == 2 && settings->LastValue == L"Tabs");

	// given up on after MaxAttempts passes: counted under the key object, with the last pass's reason
	for (int i = 1; i < RegistryIoTable::MaxAttempts; i++)
		table.NameFailed(old, RegistryUnnamed::KernelHandle);
	table.ApplyDeferred();
	CHECK_EQ(table.GetDeferredCount(), (size_t)0);
	keys = table.GetKeys();
	auto unnamedKey = std::ranges::find_if(keys, [&](auto& k) { return k.Object == old; });
	CHECK(unnamedKey != keys.end() && unnamedKey->Key.empty() && unnamedKey->Reads == 1);
	CHECK(unnamedKey != keys.end() && unnamedKey->Unnamed == RegistryUnnamed::KernelHandle);
	CHECK(RegistryIoParser::UnnamedName(old, RegistryUnnamed::KernelHandle) == L"(kernel key 0x423D4970)");
	CHECK(RegistryIoParser::UnnamedName(old, RegistryUnnamed::Protected) == L"(key 0x423D4970 of a protected process)");
	// and its later operations at once
	table.OnEvent(QueryValue(old, L"x"), 300, 6, 6);
	CHECK_EQ(table.GetDeferredCount(), (size_t)0);
}

TEST(Registry_TableReusedAddress) {
	RegistryIoTable table;
	// an operation waits on a key object, which is closed; its address is reused by an open of another key: the
	// operation isn't that key's
	table.OnEvent(At(QueryValue(0xE0, L"Old"), 1), 400, 1, 1);
	table.OnEvent(At(Open(0x10, 0xE0, L"\\REGISTRY\\MACHINE\\SOFTWARE\\Reused"), 2), 400, 2, 2);
	table.OnEvent(At(QueryValue(0xE0, L"New"), 3), 400, 3, 3);
	CHECK_EQ(table.GetDeferredCount(), (size_t)0);
	auto keys = table.GetKeys();
	auto reused = Find(keys, L"HKLM\\SOFTWARE\\Reused");
	CHECK(reused && reused->Reads == 1 && reused->LastValue == L"New");
	CHECK(std::ranges::any_of(keys, [](auto& k) { return k.Object == 0xE0 && k.LastValue == L"Old" && k.Unnamed == RegistryUnnamed::Closed; }));

	// an open relative to a base given up on is named after the base's object
	table.OnEvent(Open(0xF0, 0xF8, L"Sub"), 400, 4, 4);
	for (int i = 0; i < RegistryIoTable::MaxAttempts; i++)
		table.NameFailed(0xF0);
	table.ApplyDeferred();
	keys = table.GetKeys();
	CHECK(Find(keys, L"(key 0xF0, closed)\\Sub") != nullptr);
}

TEST(Registry_TableOutOfOrder) {
	RegistryIoTable table;
	// a query (on one processor) arrives before the open (on another) that named its key object, as each processor's
	// buffer is delivered when it fills: it's that key's, counted under its name, not the address's previous key's
	table.OnEvent(At(QueryValue(0xD0, L"Early"), 20), 500, 1, 1);
	table.OnEvent(At(Open(0x10, 0xD0, L"\\REGISTRY\\MACHINE\\SOFTWARE\\Ordered"), 10), 500, 2, 2);
	CHECK_EQ(table.GetDeferredCount(), (size_t)1);
	table.ApplyDeferred();
	CHECK_EQ(table.GetDeferredCount(), (size_t)0);
	auto keys = table.GetKeys();
	CHECK_EQ(keys.size(), (size_t)1);
	auto ordered = Find(keys, L"HKLM\\SOFTWARE\\Ordered");
	CHECK(ordered && ordered->Opens == 1 && ordered->Reads == 1 && ordered->LastValue == L"Early");
}

TEST(Registry_TableStaleNameOnReuse) {
	RegistryIoTable table;
	// a key object named by an open, closed (closes aren't traced), and its address reused by an open relative to a
	// base not named yet: the open waits, and the operations after it mustn't take the old key's name meanwhile
	const uint64_t object = 0xA70, hkcu = 0xC70;
	// before those, a key object at the same address the resolver gave up on: its failures were that key's
	for (int i = 0; i < RegistryIoTable::MaxAttempts; i++)
		table.NameFailed(object, RegistryUnnamed::Closed);
	table.OnEvent(At(Open(0x10, object, L"\\REGISTRY\\MACHINE\\SOFTWARE\\Policies\\DnsClient"), 1), 600, 1, 1);
	table.OnEvent(At(Open(hkcu, object, L"Software\\Test\\During", 0, Op::CreateKey, 1), 2), 600, 2, 2);
	table.OnEvent(At(QueryValue(object, L"Missing", 0xC0000034), 3), 600, 3, 3);
	auto keys = table.GetKeys();
	auto old = Find(keys, L"HKLM\\SOFTWARE\\Policies\\DnsClient");
	CHECK(old && old->Opens == 1 && old->Reads == 0 && old->Failures == 0);
	CHECK_EQ(table.GetDeferredCount(), (size_t)2);
	table.SetName(hkcu, L"HKCU", 4);
	table.ApplyDeferred();
	keys = table.GetKeys();
	auto during = Find(keys, L"HKCU\\Software\\Test\\During");
	CHECK(during && during->Opens == 1 && during->Writes == 1 && during->Reads == 1 && during->Failures == 1);
	old = Find(keys, L"HKLM\\SOFTWARE\\Policies\\DnsClient");
	CHECK(old && old->Reads == 0);
}

TEST(Registry_TableWaitingOpenAddressReused) {
	RegistryIoTable table;
	// as a live run traced it: a create relative to HKCU's handle (not named yet) waits, and so do the set and query
	// after it; the key is closed and, before HKCU is named, another process's open takes its address. That open
	// mustn't take the waiting operations for the closed key's (they wait on the create, not on the address)
	const uint64_t object = 0xB70, hkcu = 0xC70;
	table.OnEvent(At(Open(hkcu, object, L"Software\\Test\\During", 0, Op::CreateKey, 1), 10), 700, 1, 1);
	auto set = *Payload{}.Pointer(object).UInt32(0).UInt32(REG_SZ).UInt32(8).String(L"").String(L"During").Parse(Op::SetValue);
	table.OnEvent(At(set, 11), 700, 2, 2);
	table.OnEvent(At(QueryValue(object, L"Missing", 0xC0000034), 12), 700, 3, 3);
	table.OnEvent(At(Open(0x10, object, L"\\REGISTRY\\MACHINE\\SOFTWARE\\Other"), 20), 800, 4, 4);
	table.OnEvent(At(QueryValue(object, L"OtherValue"), 21), 800, 5, 5);
	CHECK_EQ(table.GetDeferredCount(), (size_t)3);
	// the resolver gets HKCU's handle, not the tokens the operations wait on
	CHECK(table.TakeUnnamed() == std::vector<uint64_t>({ hkcu }));

	table.SetName(hkcu, L"HKCU", 6);
	table.ApplyDeferred();
	CHECK_EQ(table.GetDeferredCount(), (size_t)0);
	auto keys = table.GetKeys();
	CHECK_EQ(keys.size(), (size_t)2);
	auto during = Find(keys, L"HKCU\\Software\\Test\\During");
	CHECK(during && during->Pid == 700 && during->Opens == 1 && during->Writes == 2 && during->Reads == 1 && during->Failures == 1);
	auto other = Find(keys, L"HKLM\\SOFTWARE\\Other");
	CHECK(other && other->Pid == 800 && other->Opens == 1 && other->Reads == 1 && other->LastValue == L"OtherValue");
	// operations on the address now are the other key's
	table.OnEvent(At(QueryValue(object, L"Later"), 30), 800, 7, 7);
	keys = table.GetKeys();
	other = Find(keys, L"HKLM\\SOFTWARE\\Other");
	CHECK(other && other->Reads == 2);
}

TEST(Registry_LiveTracker) {
	// a key opened before the session (named by the resolver) and one opened during it (named by its open)
	std::wstring path = L"Software\\WinSysLive Registry Test " + std::to_wstring(::GetCurrentProcessId());
	CRegKey before;
	if (before.Create(HKEY_CURRENT_USER, path.c_str(), nullptr, REG_OPTION_VOLATILE) != ERROR_SUCCESS) {
		CHECK(!"creating the test key");
		return;
	}
	RegistryIoTracker tracker;
	if (!tracker.Start(L"WinSysLive Tests Registry")) {
		auto error = ::GetLastError();
		printf("  the tracker didn't start (%lu%s)\n", error, error == ERROR_ACCESS_DENIED ? ": not elevated" : "");
		CHECK(error == ERROR_ACCESS_DENIED);
		before.Close();
		::RegDeleteKey(HKEY_CURRENT_USER, path.c_str());
		return;
	}
	::Sleep(500);
	before.SetDWORDValue(L"Before", 1);
	DWORD value;
	before.QueryDWORDValue(L"Before", value);
	CRegKey during;
	during.Create(HKEY_CURRENT_USER, (path + L"\\During").c_str(), nullptr, REG_OPTION_VOLATILE);
	during.SetStringValue(L"During", L"text");
	during.QueryDWORDValue(L"Missing", value);
	during.Close();

	auto pid = ::GetCurrentProcessId();
	auto suffix = path.substr(path.find(L'\\'));
	RegistryIoCounters const* first = nullptr, * second = nullptr;
	std::vector<RegistryIoCounters> keys;
	for (int i = 0; i < 80 && !(first && second && first->Reads > 0 && second->Failures > 0); i++) {
		::Sleep(250);
		keys = tracker.GetKeys();
		auto mine = [&](std::wstring const& end) -> RegistryIoCounters const* {
			auto it = std::ranges::find_if(keys, [&](auto& k) {
				return k.Pid == pid && k.Key.size() >= end.size() && ::_wcsicmp(k.Key.c_str() + k.Key.size() - end.size(), end.c_str()) == 0;
			});
			return it == keys.end() ? nullptr : &*it;
		};
		first = mine(suffix);
		second = mine(suffix + L"\\During");
	}
	printf("  %zu keys; waiting %zu; lost %llu\n", keys.size(), tracker.GetDeferredCount(), tracker.GetLostEvents());
	// where the test's own operations went, if not to the rows it looks for
	for (auto& k : keys)
		if (k.Pid == pid && (k.LastValue == L"During" || k.LastValue == L"Missing" || k.LastValue == L"Before"))
			printf("  %ls (object 0x%llX): last %ls %ls, %llu opens, %llu reads, %llu writes, %llu failures\n",
				k.Key.empty() ? RegistryIoParser::UnnamedName(k.Object, k.Unnamed).c_str() : k.Key.c_str(), k.Object,
				RegistryIoParser::OperationName(k.LastOperation), k.LastValue.c_str(), k.Opens, k.Reads, k.Writes, k.Failures);
	CHECK(first != nullptr);
	if (first) {
		printf("  %ls: %llu writes, %llu reads\n", first->Key.c_str(), first->Writes, first->Reads);
		CHECK(first->Key.starts_with(L"HKCU\\"));
		CHECK(first->Writes >= 1);
		CHECK(first->Reads >= 1);
	}
	CHECK(second != nullptr);
	if (second) {
		printf("  %ls: %llu opens, %llu writes, %llu failures\n", second->Key.c_str(), second->Opens, second->Writes, second->Failures);
		CHECK(second->Opens >= 1);
		CHECK(second->Writes >= 1);
		CHECK(second->Failures >= 1);
	}
	tracker.Stop();
	before.Close();
	::RegDeleteKey(HKEY_CURRENT_USER, (path + L"\\During").c_str());
	::RegDeleteKey(HKEY_CURRENT_USER, path.c_str());
}
