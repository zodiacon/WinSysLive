#include "pch.h"
#include "TestFramework.h"
#include <WinSys/Heaps.h>

using namespace WinSys;

// ProcessHeaps::Parse on RTL_PROCESS_HEAPS buffers in both layouts (RTL_HEAP_INFORMATION_V2, 96 bytes, verified on
// Windows 11 build 29698: an NT heap with a 16-byte entry overhead, a CSR port heap, a segment heap with no flags,
// overhead or allocated bytes; with RTL_QUERY_PROCESS_HEAP_SEGMENTS the entries are the segments), the flags' words, and
// a live query of this process

namespace {
	template<typename T>
	struct HeapsBuffer {
		ULONG NumberOfHeaps;
		T Heaps[3];
	};

	RTL_HEAP_ENTRY Segment(SIZE_T size, SIZE_T committed) {
		RTL_HEAP_ENTRY entry{};
		entry.Size = size;
		entry.Flags = RTL_HEAP_SEGMENT;
		entry.u.s2.CommittedSize = committed;
		return entry;
	}

	// the three heaps seen in a svchost and explorer, at made-up addresses
	template<typename T>
	void Fill(HeapsBuffer<T>& buffer, RTL_HEAP_ENTRY* ntSegments, RTL_HEAP_ENTRY* segmentHeapSegments) {
		buffer = {};
		buffer.NumberOfHeaps = 3;
		auto& nt = buffer.Heaps[0];
		nt.BaseAddress = (PVOID)0x1370000;
		nt.Flags = HEAP_GROWABLE;
		nt.EntryOverhead = 16;
		nt.BytesAllocated = 361518400;
		nt.BytesCommitted = 409939968;
		nt.NumberOfEntries = 2;
		nt.Entries = ntSegments;
		auto& port = buffer.Heaps[1];
		port.BaseAddress = (PVOID)0x1360000;
		port.Flags = HEAP_CLASS_8;
		port.EntryOverhead = 16;
		port.BytesAllocated = 1920;
		port.BytesCommitted = 4096;
		auto& segment = buffer.Heaps[2];
		segment.BaseAddress = (PVOID)0x8001000000;
		segment.BytesCommitted = 3031040;
		segment.NumberOfEntries = 2;
		segment.Entries = segmentHeapSegments;
	}

	void CheckParsed(std::vector<HeapInfo> const& heaps) {
		CHECK_EQ(heaps.size(), size_t(3));
		if (heaps.size() != 3)
			return;
		CHECK_EQ(heaps[0].Address, uint64_t(0x1370000));
		CHECK_EQ(heaps[0].Kind, HeapKind::Nt);
		CHECK_EQ(heaps[0].Allocated, uint64_t(361518400));
		CHECK_EQ(heaps[0].Committed, uint64_t(409939968));
		CHECK_EQ(heaps[0].Segments, 2u);
		CHECK_EQ(heaps[0].Reserved, uint64_t(1048576 + 2097152));
		CHECK(!heaps[0].Default);
		CHECK_EQ(heaps[1].Flags, uint32_t(HEAP_CLASS_8));
		CHECK_EQ(heaps[1].Segments, 0u);
		CHECK_EQ(heaps[2].Address, uint64_t(0x8001000000));
		CHECK_EQ(heaps[2].Kind, HeapKind::Segment);
		CHECK_EQ(heaps[2].Committed, uint64_t(3031040));
		CHECK_EQ(heaps[2].Allocated, uint64_t(0));
		// a non-segment entry (a block) isn't counted
		CHECK_EQ(heaps[2].Segments, 1u);
		CHECK_EQ(heaps[2].Reserved, uint64_t(1048576));
	}
}

TEST(Heaps_ParseV2) {
	RTL_HEAP_ENTRY ntSegments[] = { Segment(1048576, 1044480), Segment(2097152, 2093056) };
	RTL_HEAP_ENTRY segmentHeapSegments[] = { Segment(1048576, 987136), { 4096, RTL_HEAP_BUSY } };
	HeapsBuffer<RTL_HEAP_INFORMATION_V2> buffer;
	Fill(buffer, ntSegments, segmentHeapSegments);
	CheckParsed(ProcessHeaps::Parse(&buffer, true));
}

TEST(Heaps_ParseV1) {
	RTL_HEAP_ENTRY ntSegments[] = { Segment(1048576, 1044480), Segment(2097152, 2093056) };
	RTL_HEAP_ENTRY segmentHeapSegments[] = { Segment(1048576, 987136), { 4096, RTL_HEAP_BUSY } };
	HeapsBuffer<RTL_HEAP_INFORMATION_V1> buffer;
	Fill(buffer, ntSegments, segmentHeapSegments);
	CheckParsed(ProcessHeaps::Parse(&buffer, false));
}

TEST(Heaps_ParseEmpty) {
	CHECK(ProcessHeaps::Parse(nullptr, true).empty());
	HeapsBuffer<RTL_HEAP_INFORMATION_V2> buffer{};
	CHECK(ProcessHeaps::Parse(&buffer, true).empty());
}

TEST(Heaps_FlagsAndClassText) {
	CHECK_EQ(ProcessHeaps::FlagsText(HEAP_GROWABLE), std::wstring(L"Growable"));
	CHECK_EQ(ProcessHeaps::FlagsText(HEAP_CLASS_1 | HEAP_GROWABLE | HEAP_NO_SERIALIZE), std::wstring(L"Growable, No serialize"));
	CHECK_EQ(ProcessHeaps::FlagsText(HEAP_CLASS_8), std::wstring());
	CHECK_EQ(ProcessHeaps::FlagsText(HEAP_SETTABLE_USER_VALUE | HEAP_CREATE_ENABLE_EXECUTE), std::wstring(L"Executable"));
	CHECK_EQ(ProcessHeaps::ClassText(HEAP_GROWABLE), std::wstring());
	CHECK_EQ(ProcessHeaps::ClassText(HEAP_CLASS_1 | HEAP_GROWABLE), std::wstring(L"Private"));
	CHECK_EQ(ProcessHeaps::ClassText(HEAP_CLASS_8), std::wstring(L"CSR port"));
	CHECK_EQ(ProcessHeaps::ClassText(0xA000), std::wstring(L"Class 10"));
}

TEST(Heaps_LiveOwnProcess) {
	// a heap of the test's own, with flags to find it by
	auto heap = ::HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
	CHECK(heap != nullptr);
	auto block = ::HeapAlloc(heap, 0, 100000);
	auto heaps = ProcessHeaps::Query(::GetCurrentProcessId());
	CHECK(heaps.has_value());
	if (heaps) {
		HANDLE handles[256];
		auto count = ::GetProcessHeaps(_countof(handles), handles);
		CHECK_EQ(heaps->size(), size_t(count));

		auto def = std::ranges::find_if(*heaps, [](auto& h) { return h.Default; });
		CHECK(def != heaps->end());
		if (def != heaps->end()) {
			CHECK_EQ(def->Address, (uint64_t)::GetProcessHeap());
			CHECK(def->Committed > 0);
			CHECK(def->Reserved >= def->Committed);
			CHECK(def->Segments > 0);
		}
		auto mine = std::ranges::find_if(*heaps, [&](auto& h) { return h.Address == (uint64_t)heap; });
		CHECK(mine != heaps->end());
		if (mine != heaps->end()) {
			CHECK_EQ(mine->Kind, HeapKind::Nt);
			CHECK(!mine->Default);
			CHECK_EQ(ProcessHeaps::ClassText(mine->Flags), std::wstring(L"Private"));
			CHECK(ProcessHeaps::FlagsText(mine->Flags).find(L"Executable") != std::wstring::npos);
			CHECK(mine->Allocated >= 100000);
			CHECK(mine->Committed >= mine->Allocated);
		}
	}
	::HeapFree(heap, 0, block);
	::HeapDestroy(heap);
}

TEST(Heaps_LiveMissingProcess) {
	// an ID no process has (IDs are multiples of 4)
	CHECK(!ProcessHeaps::Query(3).has_value());
}
