#pragma once

#include <sdk/includes.h>

// Silent aim: the angles the server is told point at a target while the camera
// keeps whatever the mouse says.
//
// The work is split across two threads on purpose, and the split is not
// negotiable. Target selection walks the entity list, reads hitboxes and picks
// an aim point -- all of which goes through the SDK, and every one of those
// paths uses the enhance worker's JNIEnv. A JNIEnv belongs to the thread it was
// obtained on; using the worker's from the JVM tick thread crashes the JVM.
//
// So: run() computes on the worker and publishes a plain pair of floats.
// The hooks on the tick thread only read that pair, which needs no JNI at all.
namespace enhance::modules::aiming::silent_aim
{
	// Worker thread. Selects a target and publishes the angles for it, or
	// publishes nothing when there is no target.
	void run();

	// Tick thread. The most recently published angles, or active=false when
	// there is no current target.
	struct angles_t
	{
		bool  active = false;
		float yaw = 0.0f;
		float pitch = 0.0f;
	};
	angles_t current();

	// Diagnostics for the menu, so "aim does nothing" and "aim never found a
	// target" stop looking the same.
	struct debug_t
	{
		bool   has_target = false;
		float  yaw = 0.0f;
		float  pitch = 0.0f;
		double distance = -1.0;
		int    scanned = 0;
		int    points = 0;   // multipoint candidates that passed the range gate
	};
	debug_t debug_state();

	// Another module driving the same published rotation.
	//
	// Killaura and silent aim both select a target and aim at it; letting each
	// own its own channel means two producers contradicting one another, which
	// is precisely how the old killaura and the aiming rework ended up fighting
	// over the yaw. There is one channel, and an external producer takes
	// precedence over silent aim's own scan while it is publishing.
	void publish_external(float yaw, float pitch);
	void clear_external();
}
