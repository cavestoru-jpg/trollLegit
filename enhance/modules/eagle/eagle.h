#pragma once

#include <cstddef>

namespace enhance::modules::eagle
{
	void run();

	// Formats the live detector state for the debug overlay.
	// Returns `out`. Safe to call from the render thread (only reads
	// atomically-published statics; no JNI).
	const char* debug_status_string(char* out, size_t out_size);
}
