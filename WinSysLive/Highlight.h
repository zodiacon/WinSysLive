#pragma once

//
// How long new items and exited ones (closed, unloaded, removed) stay highlighted, in refreshes: the durations from
// Options > Highlighting... (the NewHighlight and ExitedHighlight settings, in seconds) at the update interval, at least
// one refresh unless the duration is zero (then new items aren't highlighted and exited ones go at once). The frame sets
// them on the UI thread (at startup, after the dialog and when the interval changes); they're read anywhere, as the
// property sheets' pages run on threads of their own
//
namespace Highlight {
	[[nodiscard]] int NewTicks();
	[[nodiscard]] int ExitedTicks();
	// seconds; interval: the update interval in milliseconds
	void Set(int newSeconds, int exitedSeconds, int interval);
	// a duration in refreshes at the interval
	[[nodiscard]] int ToTicks(int seconds, int interval);
}
