#pragma once

#include <sdk/includes.h>

// Hook on ClientPlayerEntity.tick (class_746.method_5773) -- the rotation
// rework's single point of application.
//
// WHY tick() AND NOT SOMETHING NARROWER
//
// Two narrower attempts failed, and the reason is worth keeping:
//
//   1. tickMovement() alone. It does contain travel(), so the body really did
//      move along the fake yaw -- but sendMovementPackets() lives in tick(),
//      not tickMovement(), so the look packet still carried the REAL yaw. The
//      result was movement and facing disagreeing, which is exactly what the
//      simulation checks are built to catch.
//
//   2. Entity.updateVelocity(), where the yaw is actually consumed and where
//      LiquidBounce corrects it. Refused at runtime: Fabric API instruments
//      Entity (handler$..$fabric-data-attachment-api-v1$readEntityAttachments),
//      and redefining a Mixin-instrumented class drops the other agent's
//      transforms.
//
// tick() is the smallest scope that contains both halves, verified against the
// bytecode of client-intermediary.jar:
//
//   ClientPlayerEntity.tick() -> sendMovementPackets()
//   ClientPlayerEntity.tick() -> super.tick() -> tickMovement() -> travel()
//                                                 -> ... -> updateVelocity()
//
// Holding the fake angles across it makes Minecraft compute the movement AND
// report the look with the same rotation, using its own maths throughout. The
// camera is unaffected because it samples at render time, outside this call.

namespace enhance::modules::aiming::tick_movement_hook
{
	// One-time JNIHook attach. Safe to call repeatedly; true once in place.
	bool init();
	void shutdown();

	// False when shutdown could not undo the class redefinition. Unloading the
	// DLL in that state guarantees a crash, so the caller must refuse to.
	bool detached_cleanly();

	bool attached();

	// True only while execution is inside the swap.
	//
	// silent_rotation_hook also hooks sendMovementPackets, which tick() calls,
	// so that hook runs *nested inside* this one whenever both are active.
	// The nesting is benign -- both save and restore symmetrically, and this
	// hook puts the real angles back when tick() returns -- but a reader who
	// has to work that out from scratch will assume it is a bug, so the query
	// exists to make the relationship explicit.
	bool is_swapping();

	// The player's rotation with no swap applied, as last seen by the tick thread.
	// False when nothing has run recently enough for the answer to be meaningful --
	// in which case the live field is honest and can be read directly.
	//
	// The worker MUST use this rather than sampling the field: the hooks hold the
	// silent angle in it for the length of a tick, so a live read is a coin toss
	// between the real angle and the fake one.
	bool true_rotation(float& yaw, float& pitch);
}
