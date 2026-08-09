#pragma once

#include <sdk/includes.h>

namespace sdk
{
	namespace java
	{
		// Fraction between the last tick and the frame currently being drawn.
		//
		// Minecraft renders entities at lerp(progress, lastRenderPos, pos), so
		// anything that draws on top of an entity has to apply the same shift.
		// Reading the raw tick position instead makes overlays step at 20 Hz
		// while the model moves smoothly, which reads as the box lagging behind
		// (or leading) the player.
		//
		// Uses the calling thread's JNI env, so it is safe from the render
		// thread. Returns 1.0 — the current tick position, i.e. the old
		// uninterpolated behaviour — when the mapping isn't available.
		float tick_progress();

		// Resolves Entity.lastRenderX/Y/Z once. False when unavailable, in
		// which case callers must fall back to the raw position.
		bool last_render_fields(JNIEnv* env, jfieldID& x, jfieldID& y, jfieldID& z);

		// Largest interpolation shift treated as real. A mis-identified field
		// would otherwise fling boxes across the map; beyond this the offset is
		// discarded and the raw position used.
		constexpr double k_max_interp_offset = 8.0;
	}
}
