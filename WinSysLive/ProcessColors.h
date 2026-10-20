#pragma once

// What a process is, for the Processes view's row colors (flags: a process can be several)
enum class ProcessKind : uint8_t {
	None = 0,
	Secure = 1,
	Protected = 2,
	Immersive = 4,
	DotNet = 8,
	Service = 16,
	Job = 32,
	// the view's highlights, not what the process is
	New = 64,
	Exited = 128,
};
DEFINE_ENUM_FLAG_OPERATORS(ProcessKind);

// The row colors per kind, saved in the ProcessColors setting as "Key:1:RRGGBB:RRGGBB,..." (enabled, light and dark
// colors); kinds missing from the setting get their defaults. The keys are stored, so they must not change.
namespace ProcessColors {
	struct Entry {
		ProcessKind Kind;
		PCWSTR Key;
		bool Enabled;
		COLORREF Light, Dark;
	};

	// in precedence order: a process of several kinds gets the color of the first one that's enabled
	std::vector<Entry> Load();
	void Save(std::vector<Entry> const& entries);
	std::vector<Entry> Defaults();
	// the enabled entry for the kinds, or nullptr
	Entry const* Find(std::vector<Entry> const& entries, ProcessKind kinds);
}
