#include "pch.h"
#include "Snapshot.h"
// Capture is in SnapshotCapture.cpp (it needs the app's process snapshot); the file format and the comparison here,
// apart, so the tests take them
#include <WinSys/Helpers.h>

namespace {
	constexpr PCWSTR Header = L"WinSysLive Snapshot";
	constexpr int Version = 1;

	std::wstring Lowercase(std::wstring text) {
		::CharLowerBuff(text.data(), (DWORD)text.size());
		return text;
	}

	std::wstring Escape(std::wstring const& text) {
		std::wstring result;
		result.reserve(text.size());
		for (auto ch : text)
			switch (ch) {
				case L'\\': result += L"\\\\"; break;
				case L'\t': result += L"\\t"; break;
				case L'\n': result += L"\\n"; break;
				case L'\r': result += L"\\r"; break;
				default: result += ch; break;
			}
		return result;
	}

	std::wstring Unescape(std::wstring_view text) {
		std::wstring result;
		result.reserve(text.size());
		for (size_t i = 0; i < text.size(); i++) {
			if (text[i] != L'\\' || i + 1 == text.size()) {
				result += text[i];
				continue;
			}
			switch (text[++i]) {
				case L't': result += L'\t'; break;
				case L'n': result += L'\n'; break;
				case L'r': result += L'\r'; break;
				default: result += text[i]; break;
			}
		}
		return result;
	}

	std::vector<std::wstring> Fields(std::wstring_view line) {
		std::vector<std::wstring> fields;
		size_t start = 0;
		while (true) {
			auto tab = line.find(L'\t', start);
			fields.push_back(Unescape(line.substr(start, tab == std::wstring_view::npos ? std::wstring_view::npos : tab - start)));
			if (tab == std::wstring_view::npos)
				break;
			start = tab + 1;
		}
		return fields;
	}

	template<typename... Args>
	void Line(std::wstring& text, Args const&... fields) {
		bool first = true;
		((text += (first ? L"" : L"\t") + Escape(fields), first = false), ...);
		text += L"\n";
	}

	uint64_t Number(std::wstring const& text) {
		return text.empty() ? 0 : std::wcstoull(text.c_str(), nullptr, 10);
	}

	std::wstring Joined(std::vector<std::wstring> const& items, size_t max = 5) {
		std::wstring text;
		for (size_t i = 0; i < items.size() && i < max; i++)
			text += (i ? L", " : L"") + items[i];
		if (items.size() > max)
			text += std::format(L" and {} more", items.size() - max);
		return text;
	}
}

bool Snapshot::Save(std::wstring const& path) const {
	std::wstring text;
	Line(text, std::wstring(Header), std::to_wstring(Version));
	Line(text, std::wstring(L"Computer"), Computer);
	Line(text, std::wstring(L"OS"), Os);
	Line(text, std::wstring(L"Time"), std::to_wstring(Time));
	for (auto& p : Processes) {
		Line(text, std::wstring(L"P"), std::to_wstring(p.Pid), std::to_wstring(p.ParentPid), p.Name, p.Path, p.CommandLine, p.User,
			std::to_wstring(p.CreateTime), std::to_wstring(p.Threads), std::to_wstring(p.Handles), std::to_wstring(p.PrivateBytes));
		for (auto& m : p.Modules)
			Line(text, std::wstring(L"M"), m);
	}
	for (auto& svc : Services)
		Line(text, std::wstring(L"S"), svc.Name, svc.DisplayName, svc.State, svc.StartType, svc.Account, std::to_wstring(svc.Pid), svc.Path);
	for (auto& d : Drivers)
		Line(text, std::wstring(L"D"), d.Name, d.Path);
	for (auto& a : Autoruns)
		Line(text, std::wstring(L"A"), a.Category, a.Location, a.Name, a.Enabled, a.Command, a.ImagePath);

	int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string utf8(size, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(), size, nullptr, nullptr);
	wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return false;
	DWORD written = 0;
	return ::WriteFile(file.get(), utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size();
}

std::optional<Snapshot> Snapshot::Load(std::wstring const& path, std::wstring& error) {
	wil::unique_hfile file(::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
	LARGE_INTEGER size{};
	if (!file || !::GetFileSizeEx(file.get(), &size) || size.QuadPart > 512 * 1024 * 1024) {
		error = std::format(L"Can't read the file: {}", WinSys::Helpers::GetErrorText(file ? ERROR_FILE_TOO_LARGE : ::GetLastError()));
		return {};
	}
	std::string utf8((size_t)size.QuadPart, '\0');
	DWORD read = 0;
	if (!::ReadFile(file.get(), utf8.data(), (DWORD)utf8.size(), &read, nullptr)) {
		error = std::format(L"Can't read the file: {}", WinSys::Helpers::GetErrorText(::GetLastError()));
		return {};
	}
	utf8.resize(read);
	int chars = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
	std::wstring text(chars, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), text.data(), chars);

	Snapshot s;
	bool header = false;
	size_t start = 0;
	while (start < text.size()) {
		auto end = text.find(L'\n', start);
		std::wstring_view line(text.data() + start, (end == std::wstring::npos ? text.size() : end) - start);
		start = end == std::wstring::npos ? text.size() : end + 1;
		if (!line.empty() && line.back() == L'\r')
			line.remove_suffix(1);
		if (line.empty())
			continue;
		auto f = Fields(line);
		auto field = [&](size_t i) -> std::wstring const& {
			static const std::wstring empty;
			return i < f.size() ? f[i] : empty;
		};
		if (!header) {
			if (f[0] != Header) {
				error = L"The file isn't a WinSysLive snapshot.";
				return {};
			}
			if (Number(field(1)) > Version) {
				error = L"The snapshot was saved by a newer version of WinSysLive.";
				return {};
			}
			header = true;
			continue;
		}
		auto& kind = f[0];
		if (kind == L"Computer")
			s.Computer = field(1);
		else if (kind == L"OS")
			s.Os = field(1);
		else if (kind == L"Time")
			s.Time = (int64_t)Number(field(1));
		else if (kind == L"P")
			s.Processes.push_back({ (uint32_t)Number(field(1)), (uint32_t)Number(field(2)), field(3), field(4), field(5), field(6),
				(int64_t)Number(field(7)), (uint32_t)Number(field(8)), (uint32_t)Number(field(9)), Number(field(10)) });
		else if (kind == L"M") {
			if (!s.Processes.empty())
				s.Processes.back().Modules.push_back(field(1));
		}
		else if (kind == L"S")
			s.Services.push_back({ field(1), field(2), field(3), field(4), field(5), field(7), (uint32_t)Number(field(6)) });
		else if (kind == L"D")
			s.Drivers.push_back({ field(1), field(2) });
		else if (kind == L"A")
			s.Autoruns.push_back({ field(1), field(2), field(3), field(4), field(5), field(6) });
		// other kinds: from a later version, skipped
	}
	if (!header) {
		error = L"The file is empty.";
		return {};
	}
	return s;
}

PCWSTR SnapshotRow::KindName(Kind kind) {
	switch (kind) {
		case Kind::Process: return L"Process";
		case Kind::Module: return L"Module";
		case Kind::Service: return L"Service";
		case Kind::Driver: return L"Driver";
		case Kind::Autorun: return L"Autorun";
	}
	return L"";
}

PCWSTR SnapshotRow::ChangeName(Change change) {
	switch (change) {
		case Change::Added: return L"Added";
		case Change::Removed: return L"Removed";
		case Change::Changed: return L"Changed";
	}
	return L"";
}

namespace {
	using Kind = SnapshotRow::Kind;
	using Change = SnapshotRow::Change;

	// the processes running one image
	struct ProcessGroup {
		std::wstring Name, Path, CommandLine;
		std::vector<uint32_t> Pids;
	};

	std::map<std::wstring, ProcessGroup> GroupProcesses(Snapshot const& s) {
		std::map<std::wstring, ProcessGroup> groups;
		for (auto& p : s.Processes) {
			auto& g = groups[Lowercase(p.Path.empty() ? p.Name : p.Path)];
			if (g.Pids.empty())
				g = { p.Name, p.Path, p.CommandLine };
			g.Pids.push_back(p.Pid);
		}
		return groups;
	}

	struct ModuleUse {
		std::wstring Path;
		std::vector<std::wstring> Loaders;	// process names, once each
	};

	std::map<std::wstring, ModuleUse> GroupModules(Snapshot const& s) {
		std::map<std::wstring, ModuleUse> modules;
		for (auto& p : s.Processes)
			for (auto& m : p.Modules) {
				auto& use = modules[Lowercase(m)];
				use.Path = m;
				if (std::ranges::find(use.Loaders, p.Name) == use.Loaders.end())
					use.Loaders.push_back(p.Name);
			}
		return modules;
	}

	std::wstring FileName(std::wstring const& path) {
		auto slash = path.rfind(L'\\');
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}

	std::wstring Pids(std::vector<uint32_t> const& pids) {
		std::vector<std::wstring> items;
		for (auto pid : pids)
			items.push_back(std::to_wstring(pid));
		return (pids.size() == 1 ? L"PID " : L"PIDs ") + Joined(items, 8);
	}

	std::wstring ServiceDetails(Snapshot::Service const& s) {
		return std::format(L"{}, {}{}", s.State, s.StartType, s.Account.empty() ? L"" : L", " + s.Account);
	}

	std::wstring AutorunKey(Snapshot::Autorun const& a) {
		return Lowercase(a.Category + L"|" + a.Location + L"|" + a.Name);
	}

	// "Field: before -> after" when they differ
	void Difference(std::vector<std::wstring>& changes, PCWSTR field, std::wstring const& before, std::wstring const& after) {
		if (before != after)
			changes.push_back(std::format(L"{}: {} -> {}", field, before.empty() ? L"(none)" : before, after.empty() ? L"(none)" : after));
	}

	// both maps by the same keys: what only one has, and what both have
	template<typename TMap, typename TAdded, typename TRemoved, typename TBoth>
	void Match(TMap const& before, TMap const& after, TAdded added, TRemoved removed, TBoth both) {
		for (auto& [key, a] : after)
			if (auto it = before.find(key); it == before.end())
				added(a);
			else
				both(it->second, a);
		for (auto& [key, b] : before)
			if (!after.contains(key))
				removed(b);
	}
}

std::vector<SnapshotRow> SnapshotCompare::Compare(Snapshot const& before, Snapshot const& after) {
	std::vector<SnapshotRow> rows;

	Match(GroupProcesses(before), GroupProcesses(after),
		[&](ProcessGroup const& g) { rows.push_back({ Kind::Process, Change::Added, g.Name, Pids(g.Pids) + L": " + g.CommandLine, g.Path }); },
		[&](ProcessGroup const& g) { rows.push_back({ Kind::Process, Change::Removed, g.Name, Pids(g.Pids) + L": " + g.CommandLine, g.Path }); },
		[&](ProcessGroup const& b, ProcessGroup const& a) {
			if (a.Pids.size() != b.Pids.size())
				rows.push_back({ Kind::Process, Change::Changed, a.Name, std::format(L"Instances: {} -> {}", b.Pids.size(), a.Pids.size()), a.Path });
		});

	Match(GroupModules(before), GroupModules(after),
		[&](ModuleUse const& m) { rows.push_back({ Kind::Module, Change::Added, FileName(m.Path), L"Loaded by " + Joined(m.Loaders), m.Path }); },
		[&](ModuleUse const& m) { rows.push_back({ Kind::Module, Change::Removed, FileName(m.Path), L"Was loaded by " + Joined(m.Loaders), m.Path }); },
		[](ModuleUse const&, ModuleUse const&) {});

	auto services = [](Snapshot const& s) {
		std::map<std::wstring, Snapshot::Service const*> map;
		for (auto& svc : s.Services)
			map[Lowercase(svc.Name)] = &svc;
		return map;
	};
	Match(services(before), services(after),
		[&](auto svc) { rows.push_back({ Kind::Service, Change::Added, svc->Name, ServiceDetails(*svc) + L": " + svc->Path }); },
		[&](auto svc) { rows.push_back({ Kind::Service, Change::Removed, svc->Name, ServiceDetails(*svc) + L": " + svc->Path }); },
		[&](auto b, auto a) {
			std::vector<std::wstring> changes;
			Difference(changes, L"State", b->State, a->State);
			Difference(changes, L"Start type", b->StartType, a->StartType);
			Difference(changes, L"Account", b->Account, a->Account);
			Difference(changes, L"Path", b->Path, a->Path);
			Difference(changes, L"Display name", b->DisplayName, a->DisplayName);
			if (!changes.empty())
				rows.push_back({ Kind::Service, Change::Changed, a->Name, Joined(changes, 10) });
		});

	auto drivers = [](Snapshot const& s) {
		std::map<std::wstring, Snapshot::Driver const*> map;
		for (auto& d : s.Drivers)
			map[Lowercase(d.Path.empty() ? d.Name : d.Path)] = &d;
		return map;
	};
	Match(drivers(before), drivers(after),
		[&](auto d) { rows.push_back({ Kind::Driver, Change::Added, d->Name, d->Path, d->Path }); },
		[&](auto d) { rows.push_back({ Kind::Driver, Change::Removed, d->Name, d->Path, d->Path }); },
		[](auto, auto) {});

	auto autoruns = [](Snapshot const& s) {
		std::map<std::wstring, Snapshot::Autorun const*> map;
		for (auto& a : s.Autoruns)
			map[AutorunKey(a)] = &a;
		return map;
	};
	Match(autoruns(before), autoruns(after),
		[&](auto a) { rows.push_back({ Kind::Autorun, Change::Added, a->Name, std::format(L"{}, {}: {}", a->Category, a->Location, a->Command), a->ImagePath }); },
		[&](auto a) { rows.push_back({ Kind::Autorun, Change::Removed, a->Name, std::format(L"{}, {}: {}", a->Category, a->Location, a->Command), a->ImagePath }); },
		[&](auto b, auto a) {
			std::vector<std::wstring> changes;
			Difference(changes, L"Command", b->Command, a->Command);
			Difference(changes, L"Enabled", b->Enabled, a->Enabled);
			if (!changes.empty())
				rows.push_back({ Kind::Autorun, Change::Changed, a->Name, a->Category + L": " + Joined(changes, 10), a->ImagePath });
		});
	return rows;
}

std::vector<SnapshotRow> SnapshotCompare::Contents(Snapshot const& s) {
	std::vector<SnapshotRow> rows;
	for (auto& p : s.Processes)
		rows.push_back({ Kind::Process, Change::None, p.Name, std::format(L"PID {}, parent {}{}: {}", p.Pid, p.ParentPid,
			p.User.empty() ? L"" : L", " + p.User, p.CommandLine), p.Path });
	for (auto& [key, m] : GroupModules(s))
		rows.push_back({ Kind::Module, Change::None, FileName(m.Path), L"Loaded by " + Joined(m.Loaders), m.Path });
	for (auto& svc : s.Services)
		rows.push_back({ Kind::Service, Change::None, svc.Name, ServiceDetails(svc) + L": " + svc.Path });
	for (auto& d : s.Drivers)
		rows.push_back({ Kind::Driver, Change::None, d.Name, d.Path, d.Path });
	for (auto& a : s.Autoruns)
		rows.push_back({ Kind::Autorun, Change::None, a.Name, std::format(L"{}, {}: {}", a.Category, a.Location, a.Command), a.ImagePath });
	return rows;
}
