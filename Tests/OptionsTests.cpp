#include "pch.h"
#include "TestFramework.h"
#include "../WinSysLive/IntervalText.h"
#include "../WinSysLive/GlobalHotKey.h"
#include "../WinSysLive/CpuHistory.h"

// View > Update Interval > Other... (IntervalText.h), Options > Global Hotkey (GlobalHotKey.h) and the threads' CPU
// History scales (CpuHistory.h)

TEST(Options_IntervalParse) {
	using IntervalText::ParseSeconds;
	CHECK_EQ(ParseSeconds(L"2"), std::optional<int>(2000));
	CHECK_EQ(ParseSeconds(L" 0.25 "), std::optional<int>(250));
	CHECK_EQ(ParseSeconds(L"1,5"), std::optional<int>(1500));
	CHECK_EQ(ParseSeconds(L".5"), std::optional<int>(500));
	CHECK_EQ(ParseSeconds(L"3."), std::optional<int>(3000));
	CHECK_EQ(ParseSeconds(L"0.1"), std::optional<int>(100));
	CHECK_EQ(ParseSeconds(L"60"), std::optional<int>(60000));
	CHECK_EQ(ParseSeconds(L"0.125"), std::optional<int>(125));
	// out of range, a fourth decimal, not a number
	CHECK(!ParseSeconds(L"0.099"));
	CHECK(!ParseSeconds(L"60.001"));
	CHECK(!ParseSeconds(L"99999999999999999999"));
	CHECK(!ParseSeconds(L"0.1255"));
	CHECK(!ParseSeconds(L""));
	CHECK(!ParseSeconds(L"."));
	CHECK(!ParseSeconds(L"1.2.3"));
	CHECK(!ParseSeconds(L"-1"));
	CHECK(!ParseSeconds(L"2 s"));
}

TEST(Options_IntervalText) {
	CHECK_EQ(IntervalText::Seconds(2000), std::wstring(L"2"));
	CHECK_EQ(IntervalText::Seconds(250), std::wstring(L"0.25"));
	CHECK_EQ(IntervalText::Seconds(1500), std::wstring(L"1.5"));
	CHECK_EQ(IntervalText::Seconds(100), std::wstring(L"0.1"));
	CHECK_EQ(IntervalText::Seconds(60005), std::wstring(L"60.005"));
	CHECK_EQ(IntervalText::Describe(1000), std::wstring(L"1 second"));
	CHECK_EQ(IntervalText::Describe(500), std::wstring(L"0.5 seconds"));
	// what's shown parses back
	for (int ms : { 100, 125, 250, 999, 1000, 1001, 7500, 60000 })
		CHECK_EQ(IntervalText::ParseSeconds(IntervalText::Seconds(ms)), std::optional<int>(ms));
}

TEST(Options_HotKey) {
	auto key = [](BYTE vk, BYTE flags) { return MAKEWORD(vk, flags); };
	CHECK_EQ(GlobalHotKey::VirtualKey(key('W', HOTKEYF_CONTROL | HOTKEYF_ALT)), (UINT)'W');
	CHECK_EQ(GlobalHotKey::Modifiers(key('W', HOTKEYF_CONTROL | HOTKEYF_ALT)), (UINT)(MOD_CONTROL | MOD_ALT | MOD_NOREPEAT));
	CHECK_EQ(GlobalHotKey::Modifiers(key(VK_F12, HOTKEYF_SHIFT | HOTKEYF_CONTROL | HOTKEYF_EXT)), (UINT)(MOD_SHIFT | MOD_CONTROL | MOD_NOREPEAT));
	CHECK(GlobalHotKey::IsValid(key('W', HOTKEYF_ALT)));
	CHECK(GlobalHotKey::IsValid(key('W', HOTKEYF_CONTROL | HOTKEYF_SHIFT)));
	CHECK(!GlobalHotKey::IsValid(key('W', 0)));
	CHECK(!GlobalHotKey::IsValid(key('W', HOTKEYF_SHIFT)));
	CHECK(!GlobalHotKey::IsValid(key(0, HOTKEYF_CONTROL)));
	CHECK_EQ(GlobalHotKey::Text(0), std::wstring(L"None"));
	CHECK_EQ(GlobalHotKey::Text(key(VK_F12, HOTKEYF_CONTROL | HOTKEYF_SHIFT)), std::wstring(L"Ctrl+Shift+F12"));
}

TEST(Threads_CpuHistoryScale) {
	using CpuHistory::Scale;
	int processors = (int)::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	// a thread keeping one processor busy is 1/processors of the machine: 100% of its processor
	int32_t oneProcessor = 1000000 / processors;
	CHECK(std::abs(CpuHistory::Sample(oneProcessor, Scale::Processor) - 100.0f) < 0.01f * processors);
	CHECK(std::abs(CpuHistory::Sample(oneProcessor, Scale::Machine) - 100.0f / processors) < 0.01f);
	CHECK(std::abs(CpuHistory::Sample(oneProcessor / 2, Scale::Processor) - 50.0f) < 0.01f * processors);
	// a process's total stays machine relative: 25% of the machine is 25
	CHECK_EQ(CpuHistory::Sample(250000, Scale::Machine), 25.0f);
	CHECK_EQ(CpuHistory::Sample(1000000, Scale::Machine), 100.0f);
	// clamped, and unknown is a gap
	CHECK_EQ(CpuHistory::Sample(2000000, Scale::Processor), 100.0f);
	CHECK(GraphCtrl::IsMissing(CpuHistory::Sample(-1, Scale::Processor)));
	CHECK(GraphCtrl::IsMissing(CpuHistory::Sample(-1, Scale::Machine)));
}
