#include "pch.h"
#include "CommandLine.h"
#include "ListExport.h"
#include <WinSys/Helpers.h>
#include <WinSys/SecurityHelper.h>

// CommandLine::Run, apart from the parsing and the tables (CommandLine.cpp, which the tests take): Snapshot::Capture
// needs the app's process snapshot

namespace {
	// the caller's console (WinSysLive is a GUI program: it has none of its own), or what it redirected the output to
	class Output {
	public:
		Output() {
			// a redirected handle (a file, a pipe) came with the process; else the console of the shell that started it
			m_Out = ::GetStdHandle(STD_OUTPUT_HANDLE);
			m_Err = ::GetStdHandle(STD_ERROR_HANDLE);
			if (!Usable(m_Out) || !Usable(m_Err)) {
				if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
					if (!Usable(m_Out))
						m_Out = ::CreateFile(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
					if (!Usable(m_Err))
						m_Err = m_Out;
				}
			}
		}
		void Write(std::wstring const& text) {
			WriteTo(m_Out, text);
		}
		void Error(std::wstring const& text) {
			WriteTo(m_Err, text + L"\r\n");
		}

	private:
		static bool Usable(HANDLE h) {
			return h && h != INVALID_HANDLE_VALUE && ::GetFileType(h) != FILE_TYPE_UNKNOWN;
		}
		static void WriteTo(HANDLE h, std::wstring const& text) {
			if (!Usable(h))
				return;
			DWORD written;
			// a console takes UTF-16 as it is; a file or a pipe gets UTF-8
			if (::GetFileType(h) == FILE_TYPE_CHAR && ::WriteConsole(h, text.data(), (DWORD)text.size(), &written, nullptr))
				return;
			auto size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
			std::string utf8(size, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), utf8.data(), size, nullptr, nullptr);
			::WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
		}

		HANDLE m_Out{ nullptr }, m_Err{ nullptr };
	};

	std::optional<Snapshot> Load(Output& out, std::wstring const& path) {
		std::wstring error;
		auto snapshot = Snapshot::Load(path, error);
		if (!snapshot)
			out.Error(path + L": " + error);
		return snapshot;
	}

	// to the console, or to /out's file
	int Emit(Output& out, std::vector<std::vector<std::wstring>> const& table, std::wstring const& path, PCWSTR what) {
		auto text = CommandLine::Render(table, path);
		if (path.empty()) {
			out.Write(text);
			return CommandLine::Success;
		}
		if (!ListExport::Save(path, text)) {
			out.Error(path + L": " + WinSys::Helpers::GetErrorText(::GetLastError()));
			return CommandLine::Failure;
		}
		out.Write(std::format(L"{}: {} {}\r\n", path, table.size() - 1, what));
		return CommandLine::Success;
	}

	void WarnIfLimited(Output& out) {
		if (!WinSys::SecurityHelper::IsRunningElevated())
			out.Error(L"Not elevated: other users' processes have no modules or command lines.");
	}
}

int CommandLine::Run(CommandLineRequest const& request) {
	Output out;
	if (!request.Error.empty()) {
		out.Error(request.Error + L"\r\n");
		out.Write(Usage());
		return UsageError;
	}
	switch (request.Command) {
		case CommandLineRequest::Action::Help:
			out.Write(Usage());
			return Success;

		case CommandLineRequest::Action::Snapshot:
		{
			WarnIfLimited(out);
			auto snapshot = Snapshot::Capture();
			if (!snapshot.Save(request.File)) {
				out.Error(request.File + L": " + WinSys::Helpers::GetErrorText(::GetLastError()));
				return Failure;
			}
			size_t modules = 0;
			for (auto& p : snapshot.Processes)
				modules += p.Modules.size();
			out.Write(std::format(L"{}: {} processes ({} modules), {} services, {} drivers, {} autoruns\r\n", request.File,
				snapshot.Processes.size(), modules, snapshot.Services.size(), snapshot.Drivers.size(), snapshot.Autoruns.size()));
			return Success;
		}

		case CommandLineRequest::Action::Compare:
		{
			auto before = Load(out, request.Before);
			if (!before)
				return Failure;
			std::optional<Snapshot> after;
			if (request.After.empty()) {
				WarnIfLimited(out);
				after = Snapshot::Capture();
			}
			else if (!(after = Load(out, request.After)))
				return Failure;
			return Emit(out, CompareTable(SnapshotCompare::Compare(*before, *after)), request.Out, L"changes");
		}

		case CommandLineRequest::Action::Export:
		{
			std::optional<Snapshot> snapshot;
			if (request.From.empty()) {
				WarnIfLimited(out);
				snapshot = Snapshot::Capture();
			}
			else if (!(snapshot = Load(out, request.From)))
				return Failure;
			return Emit(out, ExportTable(*snapshot, request.What), request.Out, request.What.c_str());
		}
	}
	return UsageError;
}
