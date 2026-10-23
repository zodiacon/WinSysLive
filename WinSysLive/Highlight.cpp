#include "pch.h"
#include "Highlight.h"
#include <atomic>

namespace {
	// the defaults at the default interval (1 s), until the frame sets them
	std::atomic<int> s_NewTicks{ 2 }, s_ExitedTicks{ 1 };
}

int Highlight::NewTicks() {
	return s_NewTicks;
}

int Highlight::ExitedTicks() {
	return s_ExitedTicks;
}

int Highlight::ToTicks(int seconds, int interval) {
	if (seconds <= 0)
		return 0;
	// rounded to the nearest refresh, at least one
	return (std::max)(1, (seconds * 1000 + interval / 2) / (std::max)(interval, 1));
}

void Highlight::Set(int newSeconds, int exitedSeconds, int interval) {
	s_NewTicks = ToTicks(newSeconds, interval);
	s_ExitedTicks = ToTicks(exitedSeconds, interval);
}
