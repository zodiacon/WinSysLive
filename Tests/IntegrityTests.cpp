#include "pch.h"
#include "TestFramework.h"
#include <WinSys/ProcessIntegrity.h>
#include <WinSys/PEFile.h>
#include <WinSys/ProcessMemory.h>

using namespace WinSys;

namespace {
	std::wstring SystemFile(PCWSTR name) {
		WCHAR dir[MAX_PATH];
		::GetSystemDirectory(dir, _countof(dir));
		return std::wstring(dir) + L"\\" + name;
	}

	// the file's first page, as CheckImage reads it
	std::vector<uint8_t> ReadHeaders(std::wstring const& path) {
		std::vector<uint8_t> data(4096);
		wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
		DWORD read = 0;
		if (!file || !::ReadFile(file.get(), data.data(), (DWORD)data.size(), &read, nullptr))
			return {};
		data.resize(read);
		return data;
	}

	// cmd.exe, suspended before it ran a line: a process to tamper with, killed at the end
	struct SuspendedChild {
		PROCESS_INFORMATION pi{};
		SuspendedChild() {
			STARTUPINFO si{ sizeof(si) };
			auto cmd = SystemFile(L"cmd.exe");
			std::wstring line = L"\"" + cmd + L"\" /c exit";
			::CreateProcess(cmd.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		}
		~SuspendedChild() {
			if (pi.hProcess) {
				::TerminateProcess(pi.hProcess, 1);
				::CloseHandle(pi.hThread);
				::CloseHandle(pi.hProcess);
			}
		}
		uint64_t PebImageBaseAddress() const {
			PROCESS_BASIC_INFORMATION info{};
			::NtQueryInformationProcess(pi.hProcess, ProcessBasicInformation, &info, sizeof(info), nullptr);
			return (uint64_t)info.PebBaseAddress + offsetof(PEB, ImageBaseAddress);
		}
	};
}

// the headers of an image against themselves, and with a field changed as a hollowing would
TEST(Integrity_CompareHeaders) {
	auto data = ReadHeaders(SystemFile(L"notepad.exe"));
	CHECK(!data.empty());
	auto file = PEFile::Parse(data);
	CHECK(file.has_value());
	if (!file)
		return;
	CHECK(ProcessIntegrity::CompareHeaders(*file, *file).empty());

	// the entry point (AddressOfEntryPoint, at the same place in both optional headers)
	auto changed = data;
	auto nt = *reinterpret_cast<LONG const*>(changed.data() + offsetof(IMAGE_DOS_HEADER, e_lfanew));
	auto entry = nt + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + offsetof(IMAGE_OPTIONAL_HEADER64, AddressOfEntryPoint);
	*reinterpret_cast<DWORD*>(changed.data() + entry) += 0x100;
	auto memory = PEFile::Parse(changed);
	CHECK(memory.has_value());
	if (!memory)
		return;
	auto diff = ProcessIntegrity::CompareHeaders(*memory, *file);
	CHECK(diff.find(L"entry point") != std::wstring::npos);
	// the image base is the loader's to change
	auto relocated = data;
	auto base = nt + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + offsetof(IMAGE_OPTIONAL_HEADER64, ImageBase);
	*reinterpret_cast<ULONGLONG*>(relocated.data() + base) += 0x10000;
	if (auto moved = PEFile::Parse(relocated))
		CHECK(ProcessIntegrity::CompareHeaders(*moved, *file).empty());
}

TEST(Integrity_OwnProcess) {
	auto result = ProcessIntegrity::CheckImage(::GetCurrentProcess());
	CHECK(result.Status == ImageIntegrityStatus::Ok);
	CHECK_EQ(result.ImageBase, (uint64_t)::GetModuleHandle(nullptr));
	auto memory = ProcessIntegrity::GetExecutableMemory(::GetCurrentProcess());
	CHECK(memory.has_value());
}

TEST(Integrity_SuspendedChildOk) {
	SuspendedChild child;
	CHECK(child.pi.hProcess != nullptr);
	if (!child.pi.hProcess)
		return;
	auto result = ProcessIntegrity::CheckImage(child.pi.hProcess);
	CHECK(result.Status == ImageIntegrityStatus::Ok);
}

// the PEB pointed at private memory, as a hollowing that unmaps the image and allocates its replacement leaves it
TEST(Integrity_ImagePrivate) {
	SuspendedChild child;
	if (!child.pi.hProcess) {
		CHECK(false);
		return;
	}
	auto h = child.pi.hProcess;
	auto region = ::VirtualAllocEx(h, nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	CHECK(region != nullptr);
	uint64_t base = (uint64_t)region;
	CHECK(::WriteProcessMemory(h, (PVOID)child.PebImageBaseAddress(), &base, sizeof(base), nullptr));
	auto result = ProcessIntegrity::CheckImage(h);
	CHECK(result.Status == ImageIntegrityStatus::ImagePrivate);
	CHECK_EQ(result.ImageBase, base);
	// and the region counts as private executable, writable memory
	auto memory = ProcessIntegrity::GetExecutableMemory(h);
	CHECK(memory && memory->PrivateRegions >= 1 && memory->WritableRegions >= 1 && memory->PrivateBytes >= 0x10000);
}

// the entry point rewritten in the mapped headers
TEST(Integrity_HeadersDiffer) {
	SuspendedChild child;
	if (!child.pi.hProcess) {
		CHECK(false);
		return;
	}
	auto h = child.pi.hProcess;
	uint64_t base = 0;
	ProcessMemory::Read(h, child.PebImageBaseAddress(), &base, sizeof(base));
	LONG nt = 0;
	ProcessMemory::Read(h, base + offsetof(IMAGE_DOS_HEADER, e_lfanew), &nt, sizeof(nt));
	auto entry = base + nt + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + offsetof(IMAGE_OPTIONAL_HEADER64, AddressOfEntryPoint);
	DWORD value = 0;
	ProcessMemory::Read(h, entry, &value, sizeof(value));
	value += 0x100;
	// the headers' page is a read-only image page (a copy-on-write one once writable)
	DWORD old = 0;
	CHECK(::VirtualProtectEx(h, (PVOID)entry, sizeof(value), PAGE_READWRITE, &old));
	CHECK(ProcessMemory::Write(h, entry, &value, sizeof(value)));
	auto result = ProcessIntegrity::CheckImage(h);
	CHECK(result.Status == ImageIntegrityStatus::HeadersDiffer);
	CHECK(result.Detail.find(L"entry point") != std::wstring::npos);
}

// the PEB pointed at another image (ntdll's, mapped in every process at the same address): not the process's file
TEST(Integrity_DifferentFile) {
	SuspendedChild child;
	if (!child.pi.hProcess) {
		CHECK(false);
		return;
	}
	auto h = child.pi.hProcess;
	uint64_t base = (uint64_t)::GetModuleHandle(L"ntdll.dll");
	CHECK(::WriteProcessMemory(h, (PVOID)child.PebImageBaseAddress(), &base, sizeof(base), nullptr));
	auto result = ProcessIntegrity::CheckImage(h);
	CHECK(result.Status == ImageIntegrityStatus::DifferentFile);
	CHECK(result.Detail.find(L"ntdll.dll") != std::wstring::npos);
}
