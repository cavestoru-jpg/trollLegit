#pragma once

#include <sdk/includes.h>

// Target tracking, ported from KillAuraTargetTracker.kt.
//
// The selector picks the best candidate this instant; the tracker decides
// whether to keep the one already committed to. Without it the aim flickers
// between two equally good targets and lands hits on neither -- which is what
// the old module did, and why it looked like the range was wrong.
namespace enhance::modules::killaura
{
	enum class sort_mode
	{
		distance = 0,   // nearest first
		health,         // weakest first
		fov,            // smallest turn first
		hurt_time,      // prefer a target still in its damage-immunity window
	};

	struct tracker_settings
	{
		float range = 4.5f;
		float fov = 180.0f;
		sort_mode sort = sort_mode::distance;
		// A committed target is kept until it dies, leaves this range or the
		// module stops. Slightly larger than `range` on purpose: dropping the
		// lock at exactly the same distance it was acquired makes a target
		// walking the boundary flicker in and out every tick.
		float lock_range_slack = 1.0f;
		bool players = true;
		bool mobs = false;
		bool animals = false;
		bool friends = false;
	};

	// Worker thread only -- it goes through the SDK, which uses the worker's
	// JNIEnv. Returns a local ref the caller owns, or nullptr.
	jobject update_target(jobject world, jobject local_player,
	                      float current_yaw, float current_pitch,
	                      const tracker_settings& s);

	// Drops any committed target. Call when the module is switched off, so the
	// next enable starts from a clean choice rather than an ancient lock.
	void reset();

	struct tracker_debug
	{
		bool locked = false;      // kept a previously committed target
		int  candidates = 0;      // what the selector scanned
		double distance = -1.0;   // to the nearest eligible entity
	};
	tracker_debug tracker_debug_state();
}
