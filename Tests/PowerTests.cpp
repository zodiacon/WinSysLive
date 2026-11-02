#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Battery.h>
#include <WinSys/ThermalZones.h>

using namespace WinSys;

// the batteries' and thermal zones' wording and units, and live checks that hold on a desktop (no battery, often no
// thermal zone) as on a laptop

TEST(Power_ChemistryText) {
	CHECK_EQ(BatteryMonitor::ChemistryText(L"LION"), std::wstring(L"Lithium-ion"));
	CHECK_EQ(BatteryMonitor::ChemistryText(L"lion"), std::wstring(L"Lithium-ion"));
	CHECK_EQ(BatteryMonitor::ChemistryText(L"LiP"), std::wstring(L"Lithium polymer"));
	CHECK_EQ(BatteryMonitor::ChemistryText(L"NiMH"), std::wstring(L"Nickel metal hydride"));
	CHECK_EQ(BatteryMonitor::ChemistryText(L"XYZ"), std::wstring(L"XYZ"));
}

TEST(Power_StateText) {
	// BATTERY_POWER_ON_LINE 1, BATTERY_DISCHARGING 2, BATTERY_CHARGING 4, BATTERY_CRITICAL 8
	CHECK_EQ(BatteryMonitor::StateText(1 | 4), std::wstring(L"Charging"));
	CHECK_EQ(BatteryMonitor::StateText(2), std::wstring(L"Discharging"));
	CHECK_EQ(BatteryMonitor::StateText(2 | 8), std::wstring(L"Discharging, critical"));
	CHECK_EQ(BatteryMonitor::StateText(1), std::wstring(L"On AC power"));
	CHECK_EQ(BatteryMonitor::StateText(0), std::wstring(L"Idle"));
}

TEST(Power_Temperatures) {
	auto equal = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
	CHECK(equal(BatteryMonitor::TenthsKelvinToCelsius(2982), 25.05f));
	CHECK(equal(ThermalZoneMonitor::ToCelsius(3132, true), 40.05f));
	CHECK(equal(ThermalZoneMonitor::ToCelsius(313, false), 39.85f));
}

TEST(Power_ChargeAndHealth) {
	BatteryInfo b;
	CHECK(!b.ChargePercent().has_value());
	b.DesignedCapacity = 50000;
	b.FullChargedCapacity = 45000;
	b.Reading.Capacity = 22500;
	CHECK(b.ChargePercent().has_value() && std::fabs(*b.ChargePercent() - 50.0f) < 0.01f);
	CHECK(b.HealthPercent().has_value() && std::fabs(*b.HealthPercent() - 90.0f) < 0.01f);
	// a capacity above the full charge (a fresh calibration) reads as full
	b.Reading.Capacity = 46000;
	CHECK(std::fabs(*b.ChargePercent() - 100.0f) < 0.01f);
	// relative units have no health
	b.Relative = true;
	CHECK(!b.HealthPercent().has_value());
}

TEST(Power_LiveBatteries) {
	BatteryMonitor monitor;
	auto count = monitor.Open();
	auto state = BatteryMonitor::GetSystemState();
	printf("  %zu batteries; the system says %s\n", count, state.HasBattery ? "it has one" : "none");
	// the class driver's batteries and the system's view agree
	CHECK_EQ(count > 0, state.HasBattery);
	for (auto& b : monitor.Read()) {
		if (!b.HasReading)
			continue;
		if (auto charge = b.ChargePercent())
			CHECK(*charge >= 0 && *charge <= 100);
		if (b.Reading.Voltage)
			CHECK(*b.Reading.Voltage > 1000 && *b.Reading.Voltage < 100000);	// mV
	}
}

TEST(Power_LiveThermalZones) {
	ThermalZoneMonitor monitor;
	if (!monitor.Open()) {
		printf("  no thermal zones\n");
		return;
	}
	auto zones = monitor.Read();
	printf("  %zu thermal zones\n", zones.size());
	CHECK(!zones.empty());
	for (auto& z : zones) {
		CHECK(!z.Name.empty());
		CHECK(z.Celsius > -50 && z.Celsius < 150);
	}
}
