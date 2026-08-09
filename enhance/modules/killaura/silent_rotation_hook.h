#pragma once

#include <sdk/includes.h>

namespace enhance::modules::silent_rotation_hook
{
	// One-time JNIHook attach on ClientPlayerEntity.sendMovementPackets.
	// Safe to call multiple times; returns true once the hook is in place.
	bool init();
	void shutdown();

	// False when shutdown could not undo the class redefinition. Unloading the
	// DLL in that state guarantees a crash, so the caller must refuse to.
	bool detached_cleanly();

	// Schedule a fake rotation for the *next* sendMovementPackets call.
	// The hook briefly swaps player.yaw/pitch (and bodyYaw/headYaw) to the
	// fake values around the original method, so outgoing PlayerMoveC2SPacket
	// look packets carry the fake angles while client state stays untouched.
	// Cleared automatically after one application — set every tick to keep
	// the rotation alive.
	void set_fake_rotation(float yaw, float pitch);

	// Queue an attack on `target` to fire IMMEDIATELY after the next
	// sendMovementPackets returns — same JVM thread, packet-channel order
	// guaranteed Look(fake) -> Attack. Without this we would call
	// interactionManager.attackEntity from the enhance worker thread and
	// race the JVM tick, causing Grim's PacketOrder check to fail. The
	// reference is promoted to a GlobalRef so it can survive thread
	// boundaries.
	// Who queued the pending attack. There is one slot, and both killaura and
	// the triggerbot use it, so a cancel has to say whose attack it is
	// cancelling: killaura clears on every tick it has no target, and the
	// worker runs the triggerbot first, so an untagged cancel silently ate the
	// triggerbot's attack whenever killaura was enabled but idle.
	enum class attack_owner { killaura, triggerbot };

	void queue_attack(jobject target, attack_owner owner);

	// Drop the pending attack only if `owner` is the one who queued it.
	void cancel_attack(attack_owner owner);

	// Drop any pending fake rotation. Cheap, idempotent.
	void clear();

	// True while a fake rotation is queued. Used by movement_correction_hook
	// to decide whether to rotate the input vector this tick.
	bool is_active();

	// Last fake yaw/pitch we asked for (whether or not it's been applied yet).
	// Movement correction reads this to compute the input-vector delta.
	float current_fake_yaw();
	float current_fake_pitch();
}
