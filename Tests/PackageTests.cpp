#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Packages.h>
#include <WinSys/SecurityHelper.h>

using namespace WinSys;

// Packages' wording, and live enumerations of the installed packages (PackageManager through WRL)

TEST(Packages_VersionText) {
	CHECK_EQ(Packages::VersionText((uint64_t(10) << 48) | (uint64_t(0) << 32) | (uint64_t(26100) << 16) | 4202), std::wstring(L"10.0.26100.4202"));
	CHECK_EQ(Packages::VersionText(0), std::wstring(L"0.0.0.0"));
	CHECK_EQ(Packages::VersionText(0xFFFFFFFFFFFFFFFF), std::wstring(L"65535.65535.65535.65535"));
}

TEST(Packages_ArchitectureText) {
	CHECK_EQ(Packages::ArchitectureText(9), std::wstring(L"x64"));
	CHECK_EQ(Packages::ArchitectureText(11), std::wstring(L"Neutral"));
	CHECK_EQ(Packages::ArchitectureText(12), std::wstring(L"ARM64"));
	CHECK_EQ(Packages::ArchitectureText(42), std::wstring(L"42"));
}

TEST(Packages_ProblemsAndKindText) {
	CHECK_EQ(Packages::ProblemsText(PackageProblem::None), std::wstring(L"OK"));
	CHECK_EQ(Packages::ProblemsText(PackageProblem::Tampered | PackageProblem::LicenseIssue), std::wstring(L"License issue, Tampered"));
	PackageInfo p;
	CHECK_EQ(std::wstring(Packages::KindText(p)), std::wstring(L"Main"));
	p.Framework = true;
	CHECK_EQ(std::wstring(Packages::KindText(p)), std::wstring(L"Framework"));
	p.Bundle = true;
	CHECK_EQ(std::wstring(Packages::KindText(p)), std::wstring(L"Bundle"));
}

namespace {
	void CheckPackages(std::vector<PackageInfo> const& packages) {
		CHECK(!packages.empty());
		size_t frameworks = 0, withPath = 0, withDisplayName = 0;
		for (auto& p : packages) {
			CHECK(!p.FullName.empty());
			// the family name is the name and the publisher ID
			CHECK_EQ(p.FamilyName, p.Name + L"_" + p.PublisherId);
			CHECK(p.FullName.starts_with(p.Name + L"_"));
			CHECK(p.Version != 0);
			if (p.Framework)
				frameworks++;
			if (!p.InstalledPath.empty())
				withPath++;
			if (!p.DisplayName.empty())
				withDisplayName++;
		}
		// every Windows 11 has the VCLibs and UI.Xaml frameworks, and Store apps
		CHECK(frameworks > 0);
		CHECK(frameworks < packages.size());
		CHECK(withPath > packages.size() / 2);
		CHECK(withDisplayName > packages.size() / 2);
	}
}

TEST(Packages_LiveCurrentUser) {
	auto com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	auto start = ::GetTickCount64();
	auto packages = Packages::Enum();
	auto elapsed = ::GetTickCount64() - start;
	CHECK(packages.has_value());
	if (packages) {
		printf("  %zu packages in %llu ms\n", packages->size(), elapsed);
		CheckPackages(*packages);
	}
	if (SUCCEEDED(com))
		::CoUninitialize();
}

TEST(Packages_LiveAllUsers) {
	auto com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	auto start = ::GetTickCount64();
	auto packages = Packages::Enum(true);
	auto elapsed = ::GetTickCount64() - start;
	if (SecurityHelper::IsRunningElevated()) {
		CHECK(packages.has_value());
		if (packages) {
			printf("  %zu packages (all users) in %llu ms\n", packages->size(), elapsed);
			CheckPackages(*packages);
		}
	}
	else {
		CHECK(!packages.has_value());
		CHECK_EQ(::GetLastError(), DWORD(E_ACCESSDENIED));
	}
	if (SUCCEEDED(com))
		::CoUninitialize();
}

TEST(Packages_AppIdOf) {
	CHECK_EQ(Packages::AppIdOf(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"), std::wstring(L"App"));
	CHECK_EQ(Packages::AppIdOf(L"NoBang"), std::wstring(L"NoBang"));
}

TEST(Packages_LiveApplicationIds) {
	auto com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	auto packages = Packages::Enum();
	CHECK(packages.has_value());
	if (packages) {
		size_t withApps = 0;
		for (auto& p : *packages) {
			auto ids = Packages::GetApplicationIds(p.FullName);
			// frameworks and resource packages have no applications
			if (p.Framework || p.Resource)
				CHECK(ids.empty());
			for (auto& id : ids)
				CHECK(id.starts_with(p.FamilyName + L"!"));
			if (!ids.empty())
				withApps++;
		}
		// most main packages are apps (some are only extensions or services)
		CHECK(withApps > packages->size() / 3);
	}
	CHECK(Packages::GetApplicationIds(L"No.Such.Package_1.0.0.0_x64__0000000000000").empty());
	if (SUCCEEDED(com))
		::CoUninitialize();
}
