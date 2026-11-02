#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Firewall.h>
#include <netfw.h>

using namespace WinSys;

// Firewall's wording and rule identity, and a live read of the rules and profiles (read-only: the tests never change
// the firewall)

TEST(Firewall_ProtocolName) {
	CHECK_EQ(Firewall::ProtocolName(6), std::wstring(L"TCP"));
	CHECK_EQ(Firewall::ProtocolName(17), std::wstring(L"UDP"));
	CHECK_EQ(Firewall::ProtocolName(58), std::wstring(L"ICMPv6"));
	CHECK_EQ(Firewall::ProtocolName(256), std::wstring(L"Any"));
	CHECK_EQ(Firewall::ProtocolName(132), std::wstring(L"132"));
}

TEST(Firewall_ProfilesText) {
	CHECK_EQ(Firewall::ProfilesText(NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PUBLIC), std::wstring(L"Domain, Public"));
	CHECK_EQ(Firewall::ProfilesText(NET_FW_PROFILE2_ALL), std::wstring(L"All"));
	CHECK_EQ(Firewall::ProfilesText(7), std::wstring(L"All"));
	CHECK_EQ(Firewall::ProfilesText(NET_FW_PROFILE2_PRIVATE), std::wstring(L"Private"));
	CHECK_EQ(Firewall::ProfilesText(0), std::wstring());
}

TEST(Firewall_SameRule) {
	FirewallRule a;
	a.RawName = L"@FirewallAPI.dll,-28502";
	a.Application = L"C:\\Windows\\System32\\svchost.exe";
	a.Service = L"Dnscache";
	a.Protocol = 17;
	a.RemotePorts = L"53";
	a.Profiles = 7;
	auto b = a;
	b.Enabled = !a.Enabled;		// not part of the identity
	b.Name = L"localized";		// nor the resolved name
	b.Application = L"c:\\windows\\system32\\SVCHOST.exe";	// paths and services compare without case
	CHECK(Firewall::SameRule(a, b));
	b.Inbound = !a.Inbound;
	CHECK(!Firewall::SameRule(a, b));
	b = a;
	b.RemotePorts = L"5353";
	CHECK(!Firewall::SameRule(a, b));
	b = a;
	b.Profiles = 4;
	CHECK(!Firewall::SameRule(a, b));
}

TEST(Firewall_LiveRulesAndPolicy) {
	auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	auto start = ::GetTickCount64();
	auto rules = Firewall::EnumRules();
	auto elapsed = ::GetTickCount64() - start;
	CHECK(rules.has_value());
	if (rules) {
		printf("  %zu rules in %llu ms\n", rules->size(), elapsed);
		CHECK(!rules->empty());
		size_t inbound = 0, outbound = 0, unresolved = 0, coreNetworking = 0;
		for (auto& r : *rules) {
			CHECK(!r.RawName.empty());
			(r.Inbound ? inbound : outbound)++;
			// a built-in group's indirect name resolves to its text
			if (!r.Grouping.empty() && r.Grouping[0] == L'@')
				unresolved++;
			if (r.RawGrouping == L"@FirewallAPI.dll,-25000")
				coreNetworking++;
			// every rule is the same as itself, and the first of its kind
			CHECK(Firewall::SameRule(r, r));
		}
		CHECK(inbound > 0 && outbound > 0);
		CHECK_EQ(unresolved, size_t(0));
		// Core Networking, on every Windows
		CHECK(coreNetworking > 0);
	}
	auto policy = Firewall::GetPolicy();
	CHECK(policy.has_value());
	if (policy) {
		CHECK_EQ(policy->Profiles.size(), size_t(3));
		CHECK(policy->CurrentProfiles != 0);
	}
	if (SUCCEEDED(com))
		::CoUninitialize();
}
