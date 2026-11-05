#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Certificates.h>

using namespace WinSys;

// Certificates: the concerns (Assess) on made-up certificates, the trust list parser on bad input, and live reads of the
// machine's stores and Microsoft's cached lists

namespace {
	const int64_t Day = 864000000000LL;
	const int64_t Now = 0x01DD6D5A00000000;	// November 2026

	CertificateInfo Cert(PCWSTR store, PCWSTR thumbprint) {
		CertificateInfo c;
		c.Store = store;
		c.PhysicalStore = L".Default";
		c.Thumbprint = thumbprint;
		c.NotBefore = Now - 365 * Day;
		c.NotAfter = Now + 365 * Day;
		return c;
	}
}

TEST(Certificates_Assess) {
	CertificateTrustList program, disallowed;
	program.Thumbprints = { L"AAAA", L"BBBB" };
	disallowed.Thumbprints = { L"BAD0" };

	// a root in the program, long there: nothing
	auto a = Certificates::Assess(Cert(L"Root", L"AAAA"), &program, &disallowed, Now);
	CHECK(a.Concern == CertificateConcern::None);
	CHECK(a.Text.empty());
	// one that isn't
	a = Certificates::Assess(Cert(L"Root", L"CCCC"), &program, &disallowed, Now);
	CHECK(a.Concern == CertificateConcern::NotInProgram);
	CHECK_EQ(a.Text, std::wstring(L"Not in Microsoft's root program"));
	// the program unknown: not judged
	a = Certificates::Assess(Cert(L"Root", L"CCCC"), nullptr, nullptr, Now);
	CHECK(a.Concern == CertificateConcern::None);
	// AuthRoot is a root store too; CA isn't
	CHECK(Certificates::Assess(Cert(L"AuthRoot", L"CCCC"), &program, nullptr, Now).Concern == CertificateConcern::NotInProgram);
	CHECK(Certificates::Assess(Cert(L"CA", L"CCCC"), &program, nullptr, Now).Concern == CertificateConcern::None);

	// a root with its private key here: the worst, with every reason said
	auto c = Cert(L"Root", L"CCCC");
	c.HasPrivateKey = true;
	c.Added = Now - 3 * Day;
	a = Certificates::Assess(c, &program, &disallowed, Now);
	CHECK(a.Concern == CertificateConcern::Danger);
	CHECK_EQ(a.Text, std::wstring(L"A trusted root whose private key is on this machine; Not in Microsoft's root program; Added 3 days ago"));
	// a private key on a personal certificate is what it's for
	c.Store = L"My";
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);

	// disallowed, where it's trusted (in the Disallowed store it's distrusted, as it should be)
	CHECK(Certificates::Assess(Cert(L"CA", L"BAD0"), &program, &disallowed, Now).Concern == CertificateConcern::Danger);
	CHECK(Certificates::Assess(Cert(L"Disallowed", L"BAD0"), &program, &disallowed, Now).Concern == CertificateConcern::None);

	// recent: in Root, Trusted Publishers and Trusted People, unless the program vouches for it
	c = Cert(L"TrustedPublisher", L"DDDD");
	c.Added = Now - Day / 2;
	a = Certificates::Assess(c, &program, &disallowed, Now);
	CHECK(a.Concern == CertificateConcern::Recent);
	CHECK_EQ(a.Text, std::wstring(L"Added today"));
	c.Added = Now - Day - 1;
	CHECK_EQ(Certificates::Assess(c, &program, &disallowed, Now).Text, std::wstring(L"Added yesterday"));
	c.Added = Now - 31 * Day;
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);
	c = Cert(L"Root", L"AAAA");
	c.Added = Now - Day;
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);
	// Windows caches intermediates in CA by itself
	c = Cert(L"CA", L"EEEE");
	c.Added = Now - Day;
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);
	// an unknown time (0) isn't recent
	c = Cert(L"TrustedPeople", L"EEEE");
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);

	// expired, anywhere
	c = Cert(L"My", L"FFFF");
	c.NotAfter = Now - Day;
	a = Certificates::Assess(c, &program, &disallowed, Now);
	CHECK(a.Concern == CertificateConcern::Expired);
	CHECK_EQ(a.Text, std::wstring(L"Expired"));

	// Windows' own roots that the program doesn't list
	c = Cert(L"Root", L"7F88CD7223F3C813818C994614A89C99FA3B5247");
	CHECK(Certificates::IsWindowsRoot(c));
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);
	c = Cert(L"Root", L"F8DB7E1C16F1FFD4AAAD4AAD8DFF0F2445184AEB");
	c.PhysicalStore = L".Flight";
	CHECK(Certificates::IsWindowsRoot(c));
	CHECK(Certificates::Assess(c, &program, &disallowed, Now).Concern == CertificateConcern::None);
	CHECK(!Certificates::IsWindowsRoot(Cert(L"Root", L"CCCC")));
}

TEST(Certificates_StoreKinds) {
	CHECK(Certificates::IsRootStore(L"Root"));
	CHECK(Certificates::IsRootStore(L"ROOT"));
	CHECK(Certificates::IsRootStore(L"AuthRoot"));
	CHECK(!Certificates::IsRootStore(L"CA"));
	CHECK(Certificates::IsTrustStore(L"CA"));
	CHECK(Certificates::IsTrustStore(L"TrustedPublisher"));
	CHECK(!Certificates::IsTrustStore(L"My"));
	CHECK(!Certificates::IsTrustStore(L"Disallowed"));
	BYTE junk[] = { 1, 2, 3, 4 };
	CHECK(!Certificates::ParseTrustList(junk, sizeof(junk)).has_value());
	CHECK(!Certificates::ParseTrustList(nullptr, 0).has_value());
}

TEST(Certificates_LiveStores) {
	auto stores = Certificates::EnumStores(CertificateLocation::LocalMachine);
	auto root = std::ranges::find_if(stores, [](auto& s) { return s.Name == L"Root"; });
	CHECK(root != stores.end());
	if (root == stores.end())
		return;
	CHECK(!root->DisplayName.empty());
	CHECK(std::ranges::any_of(root->PhysicalStores, [](auto& p) { return p == L".Default"; }));
	auto certs = Certificates::Read(CertificateLocation::LocalMachine, L"Root");
	CHECK(certs.has_value());
	if (!certs)
		return;
	CHECK(!certs->empty());
	for (auto& c : *certs) {
		CHECK_EQ(c.Thumbprint.size(), (size_t)40);
		CHECK_EQ(c.Store, std::wstring(L"Root"));
		CHECK(!c.PhysicalStore.empty());
		CHECK(!c.Encoded.empty());
		CHECK(c.NotBefore < c.NotAfter);
	}
	// Microsoft Root Certificate Authority 2011, in every Windows since 8, self-signed and RSA 4096
	auto ms = std::ranges::find_if(*certs, [](auto& c) { return c.Thumbprint == L"8F43288AD272F3103B6FB1428485EA3014C0BCFE"; });
	CHECK(ms != certs->end());
	if (ms != certs->end()) {
		CHECK_EQ(ms->Subject, std::wstring(L"Microsoft Root Certificate Authority 2011"));
		CHECK(ms->SelfSigned);
		CHECK_EQ(ms->KeyBits, 4096u);
		CHECK_EQ(ms->KeyAlgorithm, std::wstring(L"RSA"));
		CHECK(!ms->HasPrivateKey);
	}
	// the user's view leaves the machine's certificates out
	auto user = Certificates::Read(CertificateLocation::CurrentUser, L"Root");
	CHECK(user.has_value());
	if (user)
		CHECK(std::ranges::none_of(*user, [](auto& c) { return c.PhysicalStore == L".LocalMachine"; }));
}

TEST(Certificates_LiveRootProgram) {
	// cached by Windows' root update; a machine that never synced (or has it off by policy) has none
	auto program = Certificates::ReadRootProgram();
	if (!program)
		return;
	CHECK(program->Thumbprints.size() > 100);
	CHECK(program->ThisUpdate > 0);
	CHECK(program->Thumbprints.contains(L"8F43288AD272F3103B6FB1428485EA3014C0BCFE"));
	for (auto& t : program->Thumbprints)
		CHECK_EQ(t.size(), (size_t)40);
	auto disallowed = Certificates::ReadDisallowed();
	if (disallowed)
		CHECK(!disallowed->Thumbprints.contains(L"8F43288AD272F3103B6FB1428485EA3014C0BCFE"));
}
