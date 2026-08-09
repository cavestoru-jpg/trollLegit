#pragma once

#include <sdk/includes.h>

// Sprint control, split across two threads because it has to be.
//
// setSprinting mutates the entity's attribute-modifier map, which the JVM tick
// thread also mutates and which is not thread-safe. Calling it from the enhance
// worker crashed the game with ArrayIndexOutOfBoundsException(-1) inside
// Object2ObjectArrayMap.remove (crash-2026-08-06_16.31.18).
//
// So the worker only records the intent, and the tick thread acts on it from
// inside aiming::tick_movement_hook -- which is also early enough in the tick
// for the resulting packet to stay ahead of the attack.
//
// This is the surviving half of the old sprint_bypass. The rest of that module
// went with the old killaura and its replacement, ported from
// KillAuraSprint.kt, lands with the killaura rebuild.
namespace enhance::modules::killaura
{
	// Worker thread. Asks for sprint to be stopped on the next tick.
	void request_stop_sprint();

	// JVM tick thread. Applies a pending request, if any.
	void apply_on_tick(JNIEnv* env, jobject player);
}
