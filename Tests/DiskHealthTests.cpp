#include "pch.h"
#include "TestFramework.h"
#include <WinSys/DiskHealth.h>

using namespace WinSys;

// WinSys DiskHealthReader: an NVMe health log page and Identify Controller data laid out by the NVMe specification's
// byte offsets, an ATA SMART attribute table as this machine's Seagate hard disk gives it, the verdicts, and a live read

namespace {
	void Put(std::vector<uint8_t>& buffer, size_t offset, uint64_t value, int size) {
		for (int i = 0; i < size; i++)
			buffer[offset + i] = (uint8_t)(value >> (8 * i));
	}

	// the Samsung 9100 PRO's log here: 37 degrees, nothing used, 1,320 hours
	std::vector<uint8_t> NvmeLog() {
		std::vector<uint8_t> log(512);
		Put(log, 1, 310, 2);		// Kelvin
		log[3] = 100;				// available spare
		log[4] = 10;				// its threshold
		log[5] = 0;					// percentage used
		Put(log, 32, 29076453, 8);	// data units read
		Put(log, 48, 52608559, 8);	// written
		Put(log, 112, 131, 8);		// power cycles
		Put(log, 128, 1320, 8);		// power-on hours
		Put(log, 144, 74, 8);		// unsafe shutdowns
		Put(log, 160, 0, 8);		// media errors
		Put(log, 176, 754, 8);		// error log entries
		return log;
	}

	// a SMART attribute table entry: id, flags (2), current, worst, raw (6), reserved
	void PutAttribute(std::vector<uint8_t>& table, int index, uint8_t id, uint8_t current, uint8_t worst, uint64_t raw) {
		auto offset = 2 + index * 12;
		table[offset] = id;
		table[offset + 3] = current;
		table[offset + 4] = worst;
		Put(table, offset + 5, raw, 6);
	}

	// the ST8000DM004's table here (Seagate packs more in the raw values' upper bytes)
	std::vector<uint8_t> AtaTable() {
		std::vector<uint8_t> table(512);
		table[0] = 10;	// version
		int i = 0;
		PutAttribute(table, i++, 1, 83, 64, 194432123);
		PutAttribute(table, i++, 4, 98, 98, 2672);
		PutAttribute(table, i++, 5, 100, 100, 0);
		PutAttribute(table, i++, 9, 97, 97, 202610787224841);	// 3,337 hours in the low 32 bits
		PutAttribute(table, i++, 12, 100, 100, 131);
		PutAttribute(table, i++, 187, 100, 100, 0);
		PutAttribute(table, i++, 190, 68, 61, 588775455);	// 31 degrees: an airflow temperature differing from 194's
		PutAttribute(table, i++, 194, 32, 40, 81604378656);		// 32 degrees in the lowest byte
		PutAttribute(table, i++, 197, 100, 100, 0);
		PutAttribute(table, i++, 198, 100, 100, 0);
		return table;
	}
}

TEST(DiskHealth_Nvme) {
	auto log = NvmeLog();
	DiskHealth h;
	CHECK(DiskHealthReader::ParseNvmeHealthLog(log.data(), log.size(), h));
	CHECK(h.Source == DiskHealthSource::Nvme);
	CHECK(h.Temperature == 37);
	CHECK(h.AvailableSpare == 100u && h.AvailableSpareThreshold == 10u);
	CHECK(h.PercentageUsed == 0u);
	CHECK(h.BytesRead == 29076453ull * 512000);
	CHECK(h.BytesWritten == 52608559ull * 512000);
	CHECK(h.PowerCycles == 131ull && h.PowerOnHours == 1320ull);
	CHECK(h.UnsafeShutdowns == 74ull && h.MediaErrors == 0ull && h.ErrorLogEntries == 754ull);

	std::vector<uint8_t> identify(4096);
	Put(identify, 266, 353, 2);	// WCTEMP: 80 degrees
	Put(identify, 268, 358, 2);	// CCTEMP: 85 degrees
	CHECK(DiskHealthReader::ParseNvmeIdentifyController(identify.data(), identify.size(), h));
	CHECK(h.TemperatureWarning == 80 && h.TemperatureCritical == 85);

	DiskHealthReader::Evaluate(h);
	CHECK(h.State == DiskHealthState::Good);
	CHECK(h.Problems.empty());	// error log entries are no problem by themselves

	// too short
	DiskHealth none;
	CHECK(!DiskHealthReader::ParseNvmeHealthLog(log.data(), 100, none));
	CHECK(!DiskHealthReader::ParseNvmeIdentifyController(identify.data(), 300, none));
	CHECK(none.Source == DiskHealthSource::None);
	DiskHealthReader::Evaluate(none);
	CHECK(none.State == DiskHealthState::Unknown);
	CHECK(std::wstring(DiskHealthReader::StateText(none.State)) == L"Unknown");
}

TEST(DiskHealth_NvmeVerdicts) {
	auto log = NvmeLog();
	log[0] = 0x05;	// spare below threshold, reliability degraded
	log[5] = 95;
	Put(log, 160, 3, 8);
	DiskHealth h;
	DiskHealthReader::ParseNvmeHealthLog(log.data(), log.size(), h);
	DiskHealthReader::Evaluate(h);
	CHECK(h.State == DiskHealthState::Critical);
	CHECK_EQ(h.Problems.size(), 4u);
	if (h.Problems.size() == 4) {
		CHECK(h.Problems[0] == L"spare capacity below its threshold");
		CHECK(h.Problems[1] == L"reliability degraded by media errors");
		CHECK(h.Problems[2] == L"95% of its rated endurance used");
		CHECK(h.Problems[3] == L"3 media errors");
	}

	// worn and hot, but no critical warning
	log = NvmeLog();
	log[5] = 120;	// past its rated endurance
	Put(log, 1, 273 + 81, 2);
	DiskHealth w;
	DiskHealthReader::ParseNvmeHealthLog(log.data(), log.size(), w);
	w.TemperatureWarning = 80;
	DiskHealthReader::Evaluate(w);
	CHECK(w.State == DiskHealthState::Warning);
	CHECK_EQ(w.Problems.size(), 2u);
	if (w.Problems.size() == 2)
		CHECK(w.Problems[1] == L"running hot (81 \u00B0C, warning at 80 \u00B0C)");

	// the drive's own temperature warning: not said twice
	log[0] = 0x02;
	DiskHealth t;
	DiskHealthReader::ParseNvmeHealthLog(log.data(), log.size(), t);
	t.TemperatureWarning = 80;
	DiskHealthReader::Evaluate(t);
	CHECK(t.State == DiskHealthState::Critical);
	CHECK(std::ranges::count_if(t.Problems, [](auto& p) { return p.find(L"hot") != std::wstring::npos || p.find(L"temperature") != std::wstring::npos; }) == 1);
}

TEST(DiskHealth_Ata) {
	auto table = AtaTable();
	DiskHealth h;
	CHECK(DiskHealthReader::ParseAtaSmart(table.data(), table.size(), false, h));
	CHECK(h.Source == DiskHealthSource::Ata);
	CHECK_EQ(h.Attributes.size(), 10u);
	CHECK(h.PowerOnHours == 3337ull);
	CHECK(h.StartStopCount == 2672ull);
	CHECK(h.PowerCycles == 131ull);
	CHECK(h.ReallocatedSectors == 0ull && h.PendingSectors == 0ull && h.UncorrectableSectors == 0ull && h.ReportedUncorrectable == 0ull);
	CHECK(h.Temperature == 32);	// 194's, not 190's
	if (!h.Attributes.empty()) {
		CHECK_EQ(h.Attributes[0].Id, (uint8_t)1);
		CHECK_EQ(h.Attributes[0].Current, (uint8_t)83);
		CHECK_EQ(h.Attributes[0].Worst, (uint8_t)64);
		CHECK_EQ(h.Attributes[0].Raw, 194432123ull);
	}
	DiskHealthReader::Evaluate(h);
	CHECK(h.State == DiskHealthState::Good);

	// a failing disk
	PutAttribute(table, 2, 5, 90, 90, 8);
	PutAttribute(table, 8, 197, 100, 100, 1);
	DiskHealth bad;
	DiskHealthReader::ParseAtaSmart(table.data(), table.size(), true, bad);
	DiskHealthReader::Evaluate(bad);
	CHECK(bad.State == DiskHealthState::Critical);
	CHECK_EQ(bad.Problems.size(), 3u);
	if (bad.Problems.size() == 3) {
		CHECK(bad.Problems[0] == L"SMART predicts a failure");
		CHECK(bad.Problems[1] == L"8 reallocated sectors");
		CHECK(bad.Problems[2] == L"1 sector pending reallocation");
	}

	// an empty table and no prediction: nothing reported
	std::vector<uint8_t> empty(512);
	DiskHealth none;
	CHECK(!DiskHealthReader::ParseAtaSmart(empty.data(), empty.size(), false, none));
	CHECK(!DiskHealthReader::ParseAtaSmart(table.data(), 100, false, none));
	CHECK(none.Source == DiskHealthSource::None);
}

// the system drive's disks, read as the drive page reads them (no elevation needed)
TEST(DiskHealth_Live) {
	WCHAR windows[MAX_PATH];
	::GetWindowsDirectoryW(windows, MAX_PATH);
	auto disks = DiskHealthReader::GetVolumeDisks(windows[0]);
	CHECK(!disks.empty());
	for (auto disk : disks) {
		auto h = DiskHealthReader::Read(disk);
		CHECK(h.has_value());
		if (!h)
			continue;
		CHECK_EQ(h->DiskNumber, disk);
		CHECK(!h->Model.empty());
		CHECK(!h->BusType.empty());
		if (h->Source == DiskHealthSource::None)
			continue;	// a virtual disk (CI's runner) reports nothing
		CHECK(h->State != DiskHealthState::Unknown);
		CHECK(h->PowerOnHours.value_or(0) > 0);
		if (h->Temperature)
			CHECK(*h->Temperature > 0 && *h->Temperature < 100);
	}
	CHECK(DiskHealthReader::GetVolumeDisks(L'#').empty());
	CHECK(!DiskHealthReader::Read(999).has_value());
}
