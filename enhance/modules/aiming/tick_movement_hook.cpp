#include "tick_movement_hook.h"
#include "silent_aim.h"
#include "../killaura/sprint.h"
#include "rotation.h"

#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <sdk/java/jvmti_dump.h>
#include <sdk/render/render_view.h>
#include <atomic>
#include <string>
#include <windows.h>

// CRITICAL: the callback runs on the JVM tick thread. JNIEnv is thread-local,
// so every JNI call inside it MUST use the `env` JNIHook passes in, never
// enhance::instance->get_env() (bound to the enhance worker thread; using it
// cross-thread crashes the JVM). Method IDs are resolved once at init() on the
// attach thread -- those are safe to share.

static jmethodID ORIG_tick         = nullptr;
static jmethodID g_hooked_mid      = nullptr;
static bool      g_detached_cleanly = true;
static jclass    g_mc_class   = nullptr;   // MinecraftClient, the class we redefine
static jfieldID  g_fid_player = nullptr;   // MinecraftClient.player
static bool      g_attached = false;

static jmethodID g_mid_get_yaw   = nullptr;
static jmethodID g_mid_set_yaw   = nullptr;
static jmethodID g_mid_get_pitch = nullptr;
static jmethodID g_mid_set_pitch = nullptr;

// Previous-tick angles the renderer interpolates from. Entity.tick() writes
// them from getYaw()/getPitch(), so holding a fake rotation across tick()
// leaves the fake value behind: once the real angles are restored the renderer
// spends the following frames sweeping between fake and real, and the view
// visibly shakes with an amplitude equal to how far apart they are. Snapshot
// and put back, and the rotation is finally invisible on the client.
//
// Optional: a missing field costs the corresponding smoothing, not the hook.
static jfieldID g_fid_last_yaw      = nullptr;
static jfieldID g_fid_last_pitch    = nullptr;

// LivingEntity.headYaw. The interaction raycast is steered by THIS, not by the
// yaw field: ClientPlayerEntity.getCrosshairTarget -> Entity.getRotationVector
// reads getHeadYaw(). Clicking currently lands on the silent angle only by
// accident -- PlayerEntity.tickMovement assigns headYaw = getYaw() inside the
// tick wrap and nothing puts it back -- so the raycast wrap sets it explicitly
// rather than depending on that leftover.
static jfieldID g_fid_head_yaw      = nullptr;

// Set only between the swap and the restore, read by silent_rotation_hook.
// Single-threaded in practice (the tick thread), atomic because the reader is
// a different translation unit and this must not be optimised away.
static std::atomic<bool> g_swapping{false};

// Diagnostics only: the pitch as it stood when the original tick returned.
// If this still carries the silent value then the fake angle survived the
// whole tick, which means the look packet was built from it; if it has
// reverted, something inside the tick rewrote it and the swap is being
// undone rather than never applied.
static float g_dbg_pitch_after = 0.0f;

// --- second hook: GameRenderer.updateCrosshairTarget ------------------------
//
// The interaction raycast. It decides which block or entity a click acts on,
// and it runs in MinecraftClient.tick ABOVE the player's own tick -- so the
// swap held across ClientPlayerEntity.tick never reached it, and a click acted
// on whatever the REAL angle pointed at while the server had been told the
// silent one. That mismatch is exactly what an interaction raytrace check
// flags.
//
// It gets its own narrow hook rather than a wider bracket: wrapping the whole
// client tick does cover this, but it also covers keyboard and mouse handling,
// and a fake angle held across those stops the player moving and turning.
static jmethodID ORIG_crosshair    = nullptr;
static jmethodID g_hooked_mid_ch   = nullptr;
static jclass    g_gr_class        = nullptr;
static bool      g_ch_attached     = false;
static bool      g_ch_detached_ok  = true;

// Resolved once: the static MinecraftClient instance and its player field, so
// the callback can reach the player from a GameRenderer receiver.
static jclass    g_mc_class_ref    = nullptr;
static jfieldID  g_fid_mc_instance = nullptr;
static jfieldID  g_fid_mc_player   = nullptr;

// Returns a local ref to the local player, or nullptr.
static jobject fetch_local_player(JNIEnv* env)
{
	if (!g_mc_class_ref || !g_fid_mc_instance || !g_fid_mc_player)
		return nullptr;

	jobject mc = env->GetStaticObjectField(g_mc_class_ref, g_fid_mc_instance);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
	if (!mc) return nullptr;

	jobject player = env->GetObjectField(mc, g_fid_mc_player);
	if (env->ExceptionCheck()) { env->ExceptionClear(); player = nullptr; }
	env->DeleteLocalRef(mc);
	return player;
}

// --- what the server should be told ----------------------------------------
//
// One place decides the silent angles, and every hook below asks it. The three
// hooks used to each carry their own copy of the swap and only one of them
// handled pitch, which is precisely how a consumer ends up quietly using the
// real angle while everything looks correct elsewhere.
//
// The target is resolved once per tick, on the tick thread, and cached. The
// interaction raycast runs per frame on the same thread and deliberately reuses
// the cached value rather than recomputing: it must agree with what the last
// movement packet actually told the server, not with a fresher answer the
// server has not heard yet.
namespace
{
	struct silent_angles_t
	{
		bool  active = false;
		float yaw = 0.0f;
		float pitch = 0.0f;
	};

	// Absolute angles if silent aim has a target, else the fixed test offset
	// applied to the player's current rotation, else inactive.
	silent_angles_t resolve_silent_angles(JNIEnv* env, jobject player)
	{
		// Also consulted when silent aim itself is off: killaura publishes into
		// the same channel, and gating on silent_aim_enabled would compute its
		// rotation and then discard it.
		if (globals::silent_aim_enabled || globals::killaura_enabled)
		{
			const auto a = enhance::modules::aiming::silent_aim::current();
			if (a.active)
				return { true, a.yaw, a.pitch };
			// Deliberately falls through rather than returning inactive: with
			// no target the player should keep their own aim, not snap to a
			// stale one.
		}

		if (globals::silent_rotation_enabled && player && g_mid_get_yaw && g_mid_get_pitch)
		{
			silent_angles_t s;
			s.yaw = env->CallFloatMethod(player, g_mid_get_yaw);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
			s.pitch = env->CallFloatMethod(player, g_mid_get_pitch);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
			s.yaw += globals::silent_rotation_offset;
			s.active = true;
			return s;
		}

		return {};
	}
}

// --- remaining consumers ---------------------------------------------------
//
// Each of these samples the rotation on its own, outside the three wraps above,
// so each gets its own. They share one helper because the pattern is identical:
// raise the angle by the offset, run the original, put it back.
//
// headYaw is raised alongside yaw. In 1.21.11 several readers -- the
// interaction raycast most importantly -- go through getHeadYaw() rather than
// the yaw field, and leaving the two disagreeing is how a consumer silently
// keeps using the real angle.
struct scoped_yaw_swap
{
	JNIEnv* env;
	jobject player;
	bool    active = false;
	// The deltas actually applied, sampled once on entry. Everything is done in
	// terms of a delta rather than an absolute so the exit path can subtract
	// exactly what it added -- see the destructor for why that matters.
	float   dyaw = 0.0f;
	float   dpitch = 0.0f;
	bool    head = false;

	scoped_yaw_swap(JNIEnv* e, jobject p) : env(e), player(p)
	{
		if (!player || !g_mid_get_yaw || !g_mid_set_yaw || !g_mid_get_pitch || !g_mid_set_pitch)
			return;

		// Several callers hand us an entity from their argument list rather than
		// looking up the local player, and that entity is not guaranteed to be
		// us -- a nearby player's mount, or a trident thrown by someone else,
		// reaches the same code. Turning a foreign entity would visibly wrench
		// it sideways, so verify identity before touching anything.
		{
			jobject me = fetch_local_player(env);
			const bool mine = me && env->IsSameObject(me, player);
			if (me) env->DeleteLocalRef(me);
			if (!mine) return;
		}

		const silent_angles_t s = resolve_silent_angles(env, player);
		if (!s.active) return;

		const float real_yaw = env->CallFloatMethod(player, g_mid_get_yaw);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
		const float real_pitch = env->CallFloatMethod(player, g_mid_get_pitch);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return; }

		// Take the short way round. A raw subtraction would hand back deltas
		// like 350 instead of -10 whenever the two angles straddle the wrap
		// point, and the body would spin the long way to the same place.
		dyaw   = enhance::modules::aiming::angle_difference(s.yaw, real_yaw);
		dpitch = s.pitch - real_pitch;

		env->CallVoidMethod(player, g_mid_set_yaw, real_yaw + dyaw);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->CallVoidMethod(player, g_mid_set_pitch, real_pitch + dpitch);
		if (env->ExceptionCheck()) env->ExceptionClear();

		// headYaw travels with yaw. In 1.21.11 the interaction raycast reads
		// getHeadYaw() rather than the yaw field, so leaving the two disagreeing
		// is exactly how a consumer keeps silently using the real angle.
		if (g_fid_head_yaw)
		{
			const float h = env->GetFloatField(player, g_fid_head_yaw);
			env->SetFloatField(player, g_fid_head_yaw, h + dyaw);
			if (env->ExceptionCheck()) env->ExceptionClear();
			head = true;
		}
		active = true;
	}

	~scoped_yaw_swap()
	{
		if (!active) return;

		// Subtract what was added rather than writing the sampled value back.
		// Two of the callers -- the teleport acknowledgement and the rotation
		// reply -- legitimately assign the player's rotation from the server's
		// packet while inside this scope. Restoring the entry value would
		// silently discard that assignment, and the client would then argue with
		// the server about where it is looking: rubber-banding, and a stream of
		// corrective packets. Removing the delta preserves whatever the original
		// body decided.
		const float y = env->CallFloatMethod(player, g_mid_get_yaw);
		if (env->ExceptionCheck()) env->ExceptionClear();
		else
		{
			env->CallVoidMethod(player, g_mid_set_yaw, y - dyaw);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		const float pi = env->CallFloatMethod(player, g_mid_get_pitch);
		if (env->ExceptionCheck()) env->ExceptionClear();
		else
		{
			env->CallVoidMethod(player, g_mid_set_pitch, pi - dpitch);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		if (head)
		{
			const float h = env->GetFloatField(player, g_fid_head_yaw);
			if (env->ExceptionCheck()) env->ExceptionClear();
			else
			{
				env->SetFloatField(player, g_fid_head_yaw, h - dyaw);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
		}
	}
};

// --- third hook: ClientPlayerInteractionManager.interactItem ---------------
//
// The right-click use. PlayerInteractItemC2SPacket carries its own yaw and
// pitch, sampled inside this call, so neither the tick swap nor the raycast
// swap covers it -- a thrown pearl went out on the real angle while everything
// else agreed on the silent one. The player arrives as an argument here, so
// there is nothing to look up.
static jmethodID ORIG_interact_item = nullptr;
static jmethodID g_hooked_mid_ii    = nullptr;
static jclass    g_im_class         = nullptr;
static bool      g_ii_detached_ok   = true;

static jobject hkInteractItem(JNIEnv* env, jobject thiz, jobject player, jobject hand)
{
	scoped_yaw_swap swap(env, player);

	jobject result = nullptr;
	if (ORIG_interact_item && g_im_class && thiz)
	{
		result = env->CallNonvirtualObjectMethod(thiz, g_im_class, ORIG_interact_item, player, hand);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	return result;
}


// ClientPlayNetworkHandler: three replies that build a move packet from the
// player's current angles. onPlayerRotation also WRITES the yaw from the
// server's packet, so the swap has to bracket the whole call rather than sample
// once -- the destructor puts back whatever the original left, plus our offset
// removed.
static jmethodID ORIG_on_pos_look = nullptr, ORIG_on_rotation = nullptr, ORIG_on_ent_pos = nullptr;
static jmethodID g_mid_on_pos_look = nullptr, g_mid_on_rotation = nullptr, g_mid_on_ent_pos = nullptr;
static jclass    g_net_class = nullptr;
static bool      g_net_detached_ok = true;

static void hkOnPlayerPositionLook(JNIEnv* env, jobject thiz, jobject packet)
{
	jobject player = fetch_local_player(env);
	{
		scoped_yaw_swap swap(env, player);
		if (ORIG_on_pos_look && g_net_class && thiz)
		{
			env->CallNonvirtualVoidMethod(thiz, g_net_class, ORIG_on_pos_look, packet);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
	}
	if (player) env->DeleteLocalRef(player);
}

static void hkOnPlayerRotation(JNIEnv* env, jobject thiz, jobject packet)
{
	jobject player = fetch_local_player(env);
	{
		scoped_yaw_swap swap(env, player);
		if (ORIG_on_rotation && g_net_class && thiz)
		{
			env->CallNonvirtualVoidMethod(thiz, g_net_class, ORIG_on_rotation, packet);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
	}
	if (player) env->DeleteLocalRef(player);
}

static void hkOnEntityPosition(JNIEnv* env, jobject thiz, jobject packet)
{
	jobject player = fetch_local_player(env);
	{
		scoped_yaw_swap swap(env, player);
		if (ORIG_on_ent_pos && g_net_class && thiz)
		{
			env->CallNonvirtualVoidMethod(thiz, g_net_class, ORIG_on_ent_pos, packet);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
	}
	if (player) env->DeleteLocalRef(player);
}

// TridentItem.onStoppedUsing -- riptide launches the body along the look vector
// computed inside. The user is argument three, so no lookup is needed.
static jmethodID ORIG_trident = nullptr, g_mid_trident = nullptr;
static jclass    g_trident_class = nullptr;
static bool      g_trident_detached_ok = true;

static jboolean hkTridentStopped(JNIEnv* env, jobject thiz, jobject stack, jobject world,
                                 jobject user, jint remaining)
{
	scoped_yaw_swap swap(env, user);
	jboolean r = JNI_FALSE;
	if (ORIG_trident && g_trident_class && thiz)
	{
		r = env->CallNonvirtualBooleanMethod(thiz, g_trident_class, ORIG_trident,
		                                    stack, world, user, remaining);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	return r;
}

// AbstractHorseEntity.getControlledRotation -- a ridden mount copies the
// rider's angle here, during the MOUNT's tick, and that is what
// VehicleMoveC2SPacket reports. The rider is the argument.
static jmethodID ORIG_horse_rot = nullptr, g_mid_horse_rot = nullptr;
static jclass    g_horse_class = nullptr;
static bool      g_horse_detached_ok = true;

static jobject hkControlledRotation(JNIEnv* env, jobject thiz, jobject rider)
{
	scoped_yaw_swap swap(env, rider);
	jobject r = nullptr;
	if (ORIG_horse_rot && g_horse_class && thiz)
	{
		r = env->CallNonvirtualObjectMethod(thiz, g_horse_class, ORIG_horse_rot, rider);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	return r;
}


// --- model pitch ------------------------------------------------------------
//
// The one place where yaw and pitch are not symmetric. headYaw turns the model
// without touching the view, so leaving it fake already makes the body face the
// target. There is no equivalent for pitch: the model and the camera both read
// entity.getPitch(tickDelta), so a fake value in the field tilts the view too.
//
// The separation comes from WHEN each one reads. The camera reads during its
// own update; the model's pitch is baked into the render state inside
// updateRenderState. Holding the silent pitch across just that call tilts the
// model and leaves the camera alone.
static jmethodID ORIG_update_render_state = nullptr;
static jmethodID g_hooked_mid_rs          = nullptr;
static jclass    g_rs_class               = nullptr;
static bool      g_rs_detached_ok         = true;

static void hkUpdateRenderState(JNIEnv* env, jobject thiz, jobject entity, jobject state, jfloat tick_delta)
{
	// Pitch only. yaw and headYaw are already carrying the silent angle by the
	// time rendering happens, and swapping them again here would apply the
	// offset twice.
	bool  swapped = false;
	float saved_pitch = 0.0f;
	float saved_last_pitch = 0.0f;
	bool  swapped_last = false;

	// Cheapest test first. This runs once per frame for every living entity in
	// view -- at a high frame rate with a crowded scene that is tens of
	// thousands of calls a second, and the hook stays installed whether or not
	// the feature is switched on. Reading two bools costs nothing; looking the
	// local player up through JNI to then discard it does not.
	if ((globals::silent_aim_enabled || globals::silent_rotation_enabled) &&
	    entity && g_mid_get_pitch && g_mid_set_pitch)
	{
		jobject me = fetch_local_player(env);
		const bool mine = me && env->IsSameObject(me, entity);
		if (me) env->DeleteLocalRef(me);

		if (mine)
		{
			const silent_angles_t want = resolve_silent_angles(env, entity);
			if (want.active)
			{
				saved_pitch = env->CallFloatMethod(entity, g_mid_get_pitch);
				if (env->ExceptionCheck()) env->ExceptionClear();
				else
				{
					env->CallVoidMethod(entity, g_mid_set_pitch, want.pitch);
					if (env->ExceptionCheck()) env->ExceptionClear();
					swapped = true;

					// lastPitch has to move with it. The renderer does not read
					// the field, it interpolates lastPitch -> pitch by the frame
					// fraction, so setting only one end makes every frame show a
					// different blend of the real and the silent angle: the head
					// sweeps between them at the frame rate, which reads as
					// shaking, and for most of each tick it is still showing the
					// real value. Both ends equal means the interpolation is a
					// constant and the model simply holds the silent pitch.
					if (g_fid_last_pitch)
					{
						saved_last_pitch = env->GetFloatField(entity, g_fid_last_pitch);
						if (env->ExceptionCheck()) env->ExceptionClear();
						else
						{
							env->SetFloatField(entity, g_fid_last_pitch, want.pitch);
							if (env->ExceptionCheck()) env->ExceptionClear();
							swapped_last = true;
						}
					}
				}
			}
		}
	}

	if (ORIG_update_render_state && g_rs_class && thiz)
	{
		env->CallNonvirtualVoidMethod(thiz, g_rs_class, ORIG_update_render_state,
		                              entity, state, tick_delta);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	if (swapped)
	{
		env->CallVoidMethod(entity, g_mid_set_pitch, saved_pitch);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	if (swapped_last)
	{
		env->SetFloatField(entity, g_fid_last_pitch, saved_last_pitch);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
}

static void hkUpdateCrosshair(JNIEnv* env, jobject thiz, jfloat tick_delta)
{
	// The interaction raycast. Everything the player can click on is decided
	// here, and it samples the rotation itself rather than inheriting the tick
	// swap, so without this the crosshair picked targets with the real angle
	// while the server had been told another.
	jobject player = fetch_local_player(env);

	{
		scoped_yaw_swap swap(env, player);
		if (ORIG_crosshair && g_gr_class && thiz)
		{
			env->CallNonvirtualVoidMethod(thiz, g_gr_class, ORIG_crosshair, tick_delta);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
	}

	if (player) env->DeleteLocalRef(player);
}

static void hkTick(JNIEnv* env, jobject thiz)
{
	// Back on ClientPlayerEntity.tick, so `thiz` is the player itself.
	//
	// Wrapping MinecraftClient.tick instead was tried and reverted: it does
	// cover the interaction raycast, but it also covers keyboard and mouse
	// handling, the perspective toggle and much else, and holding a fake angle
	// across all of that broke movement, made F5 stop responding and produced
	// bad packets. The raycast needs its own narrow hook rather than a wider
	// bracket around everything.
	jobject player = thiz;

	// Sprint reset, requested from the worker and applied here because this is
	// the only thread on which setSprinting is safe. Runs before the original
	// body so its packet is queued ahead of the attack.
	if (player)
		enhance::modules::killaura::apply_on_tick(env, player);


	bool  swapped = false;
	float saved_yaw = 0.0f;
	float saved_pitch = 0.0f;
	float fake_yaw = 0.0f;
	float fake_pitch = 0.0f;
	// Applied as deltas so the restore below can subtract exactly what was
	// added, rather than overwriting whatever the tick decided in between.
	float dyaw = 0.0f;
	float dpitch = 0.0f;

	if (player && g_mid_get_yaw && g_mid_set_yaw)
	{
		const silent_angles_t want = resolve_silent_angles(env, player);
		if (want.active)
		{
			saved_yaw = env->CallFloatMethod(player, g_mid_get_yaw);
			if (env->ExceptionCheck()) env->ExceptionClear();

			if (g_mid_get_pitch)
			{
				saved_pitch = env->CallFloatMethod(player, g_mid_get_pitch);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			// Short way round, so a target across the wrap point does not send
			// the body spinning the long way to the same bearing.
			dyaw   = enhance::modules::aiming::angle_difference(want.yaw, saved_yaw);
			dpitch = want.pitch - saved_pitch;

			fake_yaw   = saved_yaw + dyaw;
			fake_pitch = saved_pitch + dpitch;

			env->CallVoidMethod(player, g_mid_set_yaw, fake_yaw);
			if (env->ExceptionCheck()) env->ExceptionClear();

			// Pitch was previously sampled and never applied, so the look
			// packet carried the silent yaw against the real pitch. Harmless
			// for a flat test offset; wrong the moment the angle has to point
			// at something above or below eye level.
			if (g_mid_set_pitch && dpitch != 0.0f)
			{
				env->CallVoidMethod(player, g_mid_set_pitch, fake_pitch);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			swapped = true;
			g_swapping.store(true, std::memory_order_release);
		}
	}

	// The whole of ClientPlayerEntity.tick runs with the fake yaw in place.
	// That is the point: sendMovementPackets is inside it and builds the look
	// packet from getYaw(), so the server is told the fake angle, and travel()
	// is inside it too, so the body moves along the same angle the server was
	// told. Verified against the bytecode of client-intermediary.jar.
	if (ORIG_tick && g_mc_class && thiz)
	{
		env->CallNonvirtualVoidMethod(thiz, g_mc_class, ORIG_tick);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	if (swapped)
	{
		g_swapping.store(false, std::memory_order_release);

		// Put back what the CAMERA reads, and nothing else.
		//
		// ClientPlayerEntity overrides getYaw(tickDelta) to return the raw
		// field for the local player, so the view direction comes straight from
		// these two -- restore them and the player's own view never moved.
		// lastYaw/lastPitch are the previous-tick pair the renderer interpolates
		// from; Entity.tick assigns them from getYaw()/getPitch(), which is the
		// fake value at that moment, so they need the offset taken back out or
		// the view sweeps between fake and real every frame.
		// Restore the OFFSET, not the old value.
		//
		// MinecraftClient.tick also processes mouse input, and turning is
		// applied as a delta on top of whatever the yaw currently holds -- the
		// fake angle, while we have it in place. Writing saved_yaw back
		// discarded every bit of that: the view could not be turned at all, and
		// input fighting the restore each tick showed up as shaking on the
		// spot. Reading the yaw again and subtracting only what we added keeps
		// the player's own turn and removes the offset.
		float after = env->CallFloatMethod(player, g_mid_get_yaw);
		if (env->ExceptionCheck()) { env->ExceptionClear(); after = fake_yaw; }

		const float dy = dyaw;

		env->CallVoidMethod(player, g_mid_set_yaw, after - dy);
		if (env->ExceptionCheck()) env->ExceptionClear();

		if (g_fid_last_yaw)
			env->SetFloatField(player, g_fid_last_yaw,
				env->GetFloatField(player, g_fid_last_yaw) - dy);
		if (env->ExceptionCheck()) env->ExceptionClear();

		// Pitch gets the same treatment, for the same reason: Entity.tick
		// copies getPitch() into lastPitch, which is the fake value while we
		// hold it, and the renderer interpolates the view from that pair.
		if (dpitch != 0.0f && g_mid_get_pitch && g_mid_set_pitch)
		{
			float after_pitch = env->CallFloatMethod(player, g_mid_get_pitch);
			if (env->ExceptionCheck()) { env->ExceptionClear(); after_pitch = fake_pitch; }
			g_dbg_pitch_after = after_pitch;

			env->CallVoidMethod(player, g_mid_set_pitch, after_pitch - dpitch);
			if (env->ExceptionCheck()) env->ExceptionClear();

			if (g_fid_last_pitch)
			{
				env->SetFloatField(player, g_fid_last_pitch,
					env->GetFloatField(player, g_fid_last_pitch) - dpitch);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
		}

		// Deliberately NOT restored, and this is the part to revisit as the
		// feature grows rather than to "fix" blindly:
		//
		//   headYaw / bodyYaw / renderYaw and their previous-tick copies
		//     these drive the player MODEL and the first-person hand, not the
		//     view. Each absorbs a different share of the offset -- headYaw all
		//     of it, renderYaw half, bodyYaw an eased and then nonlinearly
		//     clamped fraction -- so a blanket subtraction is wrong, and
		//     subtracting the full offset from bodyYaw made it spin.
		//
		//   ClientPlayerEntity.lastYawClient / lastPitchClient
		//     these MUST keep the fake value. sendMovementPackets compares
		//     getYaw() against them to decide whether to send a look packet at
		//     all; resetting them would make the next tick report the real
		//     angle and undo the rotation server-side.
	}

	if (globals::aiming_debug_log)
	{
		static uint64_t s_last_log = 0;
		const uint64_t now = GetTickCount64();
		if (now - s_last_log > 1000)
		{
			s_last_log = now;
			logger::log("[silent] swap=" + std::string(swapped ? "yes" : "no") +
			            " real=" + std::to_string(saved_yaw) +
			            " fake=" + std::to_string(fake_yaw) +
			            " real_pitch=" + std::to_string(saved_pitch) +
			            " fake_pitch=" + std::to_string(fake_pitch) +
			            " dpitch=" + std::to_string(dpitch) +
			            " pitch_at_end=" + std::to_string(g_dbg_pitch_after) +
			            " set_pitch=" + std::string(g_mid_set_pitch ? "ok" : "NULL"));
		}
	}

	// `player` aliases `thiz` here, which JNIHook owns -- nothing to release.
}

bool enhance::modules::aiming::tick_movement_hook::init()
{
	if (g_attached) return true;

	// init() is posted to the client thread so it can attach without
	// can_suspend, and JNIEnv is thread-local: the cached one belongs to the
	// enhance worker. Using it from here is undefined behaviour and crashed the
	// game natively, with no Java crash report to show for it.
	auto env = sdk::render::current_thread_env();
	auto jvm = enhance::instance ? enhance::instance->get_java_vm() : nullptr;
	if (!env || !jvm) return false;

	// No Mixin guard here, and that is deliberate.
	//
	// The guard existed on the belief that redefining an instrumented class
	// drops the other agent's work. JNIHook_ProbeClassMixins settled it by
	// capturing the bytes the JVM actually hands us for redefinition: they
	// already contain Mixin's members (measured on Entity ->
	// handler$..$fabric-data-attachment-api-v1$writeEntityAttachments, and on
	// GameRenderer -> handler$..$fabric-rendering-v1$guiRendererReady). The
	// transforms are baked in before we ever see the class, so redefining
	// preserves them, and MinecraftClient being instrumented is not a reason
	// to refuse.

	// Any non-OK result here means g_jnihook was left null, and every later
	// JNIHook_Attach would fall out at its null check with a misleading
	// ERR_JVMTI_OPERATION. JNIHook_Init is idempotent, so a second call while
	// already initialised returns OK rather than an error to excuse.
	//
	// This used to read `init_res != 3 /* already-initialized */`, but 3 is
	// ERR_ADD_JVMTI_CAPS -- the real "capabilities were refused" failure was
	// being swallowed and blamed on the attach.
	const jnihook_result_t init_res = JNIHook_Init(jvm);
	if (init_res != JNIHOOK_OK)
	{
		logger::log(std::string("[aiming] JNIHook_Init failed=") + std::to_string((int)init_res) +
		            " jvmti_err=" + std::to_string(JNIHook_LastJvmtiError()));
		return false;
	}

	jclass cp_cls = sdk::classloader::find_class(env, sdk::mappings::clientplayerentity_class_sig);
	if (!cp_cls) return false;

	// ClientPlayerEntity.tick. Deliberately NOT MinecraftClient.tick: that
	// scope reaches the interaction raycast but also wraps input handling, and
	// a fake angle held across the mouse and keyboard path stops the player
	// moving and turning. The raycast gets its own hook instead.
	jmethodID mid = env->GetMethodID(cp_cls,
		sdk::mappings::entity_tick_name,
		sdk::mappings::entity_tick_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!mid)
	{
		env->DeleteLocalRef(cp_cls);
		return false;
	}

	jclass entity_cls = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
	if (entity_cls)
	{
		g_mid_get_yaw = env->GetMethodID(entity_cls,
			sdk::mappings::entity_get_yaw_name, sdk::mappings::entity_get_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_get_yaw = nullptr; }

		g_mid_set_yaw = env->GetMethodID(entity_cls,
			sdk::mappings::entity_set_yaw_name, sdk::mappings::entity_set_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_set_yaw = nullptr; }

		// Pitch is optional: movement only needs yaw, so a missing accessor
		// costs us the vertical half of the silent look rather than the
		// feature. mappings.hpp's entity_get_pitch_sig is the (F)F tick-delta
		// variant, so the no-arg getter is named directly here -- the same
		// workaround silent_rotation_hook uses.
		g_mid_get_pitch = env->GetMethodID(entity_cls, "method_36455", "()F");
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_get_pitch = nullptr; }
		if (!g_mid_get_pitch)
		{
			// Same fallback silent_rotation_hook carries. Without a pitch
			// getter saved_pitch stays 0, and the manager then computes every
			// pitch delta -- and its release test -- against a fictitious
			// pitch of zero, so the walk-back can never converge.
			g_mid_get_pitch = env->GetMethodID(entity_cls, "method_5695", "()F");
			if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_get_pitch = nullptr; }
		}

		g_mid_set_pitch = env->GetMethodID(entity_cls,
			sdk::mappings::entity_set_pitch_name, sdk::mappings::entity_set_pitch_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_set_pitch = nullptr; }

		g_fid_last_yaw = env->GetFieldID(entity_cls,
			sdk::mappings::entity_last_yaw_name, sdk::mappings::entity_last_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_last_yaw = nullptr; }

		g_fid_last_pitch = env->GetFieldID(entity_cls,
			sdk::mappings::entity_last_pitch_name, sdk::mappings::entity_last_pitch_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_last_pitch = nullptr; }

		env->DeleteLocalRef(entity_cls);
		entity_cls = nullptr;
	}

	// headYaw lives on LivingEntity, not Entity.
	{
		jclass living_cls = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
		if (living_cls)
		{
			g_fid_head_yaw = env->GetFieldID(living_cls,
				sdk::mappings::living_entity_head_yaw_name,
				sdk::mappings::living_entity_head_yaw_sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_head_yaw = nullptr; }
			env->DeleteLocalRef(living_cls);
		}

		env->DeleteLocalRef(entity_cls);
	}

	// Without the accessors the hook could only call through, so there would
	// be nothing to gain and a redefinition to undo.
	if (!g_mid_get_yaw || !g_mid_set_yaw)
	{
		logger::log("[aiming] tick hook: yaw accessors unresolved, not attaching");
		env->DeleteLocalRef(cp_cls);
		return false;
	}

	jnihook_result_t r = JNIHook_Attach(mid, reinterpret_cast<void*>(hkTick), &ORIG_tick);
	if (r != JNIHOOK_OK)
	{
		// The jnihook result alone is ambiguous (JVMTI_OPERATION is returned
		// from a dozen call sites), so log the JVMTI error too. Codes worth
		// recognising: 62 UNMODIFIABLE_CLASS, 63 NOT_AVAILABLE,
		// 66..71 UNSUPPORTED_REDEFINITION_*, 62/64 verification failures.
		logger::log_error(std::string("[aiming] tick attach FAILED=") + std::to_string((int)r) +
		            " jvmti_err=" + std::to_string(JNIHook_LastJvmtiError()));
		env->DeleteLocalRef(cp_cls);
		return false;
	}
	g_hooked_mid = mid;

	g_mc_class = reinterpret_cast<jclass>(env->NewGlobalRef(cp_cls));
	env->DeleteLocalRef(cp_cls);
	if (!g_mc_class) return false;

	// Second hook: the interaction raycast. Independent of the first -- if it
	// fails the rotation still reaches the server, only clicks keep acting on
	// the real angle, so a failure here is reported and not fatal.
	{
		jclass mc_cls = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
		if (mc_cls)
		{
			g_fid_mc_instance = env->GetStaticFieldID(mc_cls,
				sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_mc_instance = nullptr; }

			g_fid_mc_player = env->GetFieldID(mc_cls,
				sdk::mappings::player_name, sdk::mappings::player_sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_mc_player = nullptr; }

			g_mc_class_ref = reinterpret_cast<jclass>(env->NewGlobalRef(mc_cls));
			env->DeleteLocalRef(mc_cls);
		}

		jclass gr_cls = sdk::classloader::find_class(env, sdk::mappings::gamerenderer_class_sig);
		if (gr_cls && g_mc_class_ref && g_fid_mc_instance && g_fid_mc_player)
		{
			jmethodID ch_mid = env->GetMethodID(gr_cls,
				sdk::mappings::update_crosshair_target_name,
				sdk::mappings::update_crosshair_target_sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); ch_mid = nullptr; }

			if (ch_mid)
			{
				const jnihook_result_t cr = JNIHook_Attach(ch_mid,
					reinterpret_cast<void*>(hkUpdateCrosshair), &ORIG_crosshair);
				if (cr == JNIHOOK_OK)
				{
					g_hooked_mid_ch = ch_mid;
					g_gr_class = reinterpret_cast<jclass>(env->NewGlobalRef(gr_cls));
					g_ch_attached = true;
					logger::log("[silent] crosshair raycast hook attached");
				}
				else
				{
					logger::log_error(std::string("[silent] crosshair hook attach failed=") +
					                  std::to_string((int)cr) + " jvmti_err=" +
					                  std::to_string(JNIHook_LastJvmtiError()) +
					                  " -- clicks will still use the real angle");
				}
			}
		}
		if (gr_cls) env->DeleteLocalRef(gr_cls);

		// Third hook: the right-click use, for the packet that carries its own
		// angles. Independent of the other two in the same way.
		jclass im_cls = sdk::classloader::find_class(env, "net/minecraft/class_636");   // ClientPlayerInteractionManager; no mapping constant exists, same literal silent_rotation_hook uses
		if (im_cls)
		{
			jmethodID ii_mid = env->GetMethodID(im_cls,
				sdk::mappings::interact_item_name, sdk::mappings::interact_item_sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); ii_mid = nullptr; }

			if (ii_mid)
			{
				const jnihook_result_t ir2 = JNIHook_Attach(ii_mid,
					reinterpret_cast<void*>(hkInteractItem), &ORIG_interact_item);
				if (ir2 == JNIHOOK_OK)
				{
					g_hooked_mid_ii = ii_mid;
					g_im_class = reinterpret_cast<jclass>(env->NewGlobalRef(im_cls));
					logger::log("[silent] item-use hook attached");
				}
				else
				{
					logger::log_error(std::string("[silent] item-use hook attach failed=") +
					                  std::to_string((int)ir2) + " jvmti_err=" +
					                  std::to_string(JNIHook_LastJvmtiError()) +
					                  " -- thrown items will still use the real angle");
				}
			}
			env->DeleteLocalRef(im_cls);
		}

		// The remaining consumers. Each is independent: a failure here costs
		// that one path, not the rotation as a whole.
		struct extra_hook
		{
			const char* class_sig;
			const char* name;
			const char* sig;
			void*       fn;
			jmethodID*  orig;
			jmethodID*  hooked;
			jclass*     cls;
			const char* label;
		};

		const extra_hook extras[] = {
			{ sdk::mappings::net_handler_class_sig, sdk::mappings::on_player_position_look_name,
			  sdk::mappings::on_player_position_look_sig, (void*)hkOnPlayerPositionLook,
			  &ORIG_on_pos_look, &g_mid_on_pos_look, &g_net_class, "net: teleport ack" },
			{ sdk::mappings::net_handler_class_sig, sdk::mappings::on_player_rotation_name,
			  sdk::mappings::on_player_rotation_sig, (void*)hkOnPlayerRotation,
			  &ORIG_on_rotation, &g_mid_on_rotation, &g_net_class, "net: rotation reply" },
			{ sdk::mappings::net_handler_class_sig, sdk::mappings::on_entity_position_name,
			  sdk::mappings::on_entity_position_sig, (void*)hkOnEntityPosition,
			  &ORIG_on_ent_pos, &g_mid_on_ent_pos, &g_net_class, "net: vehicle teleport" },
			{ sdk::mappings::trident_item_class_sig, sdk::mappings::trident_on_stopped_using_name,
			  sdk::mappings::trident_on_stopped_using_sig, (void*)hkTridentStopped,
			  &ORIG_trident, &g_mid_trident, &g_trident_class, "riptide" },
			{ sdk::mappings::horse_class_sig, sdk::mappings::controlled_rotation_name,
			  sdk::mappings::controlled_rotation_sig, (void*)hkControlledRotation,
			  &ORIG_horse_rot, &g_mid_horse_rot, &g_horse_class, "mount rotation" },
			{ sdk::mappings::living_renderer_class_sig, sdk::mappings::update_render_state_name,
			  sdk::mappings::update_render_state_sig, (void*)hkUpdateRenderState,
			  &ORIG_update_render_state, &g_hooked_mid_rs, &g_rs_class, "model pitch" },
		};

		for (const auto& h : extras)
		{
			jclass c = sdk::classloader::find_class(env, h.class_sig);
			if (!c)
			{
				logger::log_error(std::string("[silent] ") + h.label + ": class not found");
				continue;
			}

			jmethodID m = env->GetMethodID(c, h.name, h.sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); m = nullptr; }
			if (!m)
			{
				logger::log_error(std::string("[silent] ") + h.label + ": method not found");
				env->DeleteLocalRef(c);
				continue;
			}

			const jnihook_result_t r = JNIHook_Attach(m, h.fn, h.orig);
			if (r == JNIHOOK_OK)
			{
				*h.hooked = m;
				if (!*h.cls) *h.cls = reinterpret_cast<jclass>(env->NewGlobalRef(c));
				logger::log(std::string("[silent] hook attached: ") + h.label);
			}
			else
			{
				logger::log_error(std::string("[silent] ") + h.label + " attach failed=" +
				                  std::to_string((int)r) + " jvmti_err=" +
				                  std::to_string(JNIHook_LastJvmtiError()));
			}
			env->DeleteLocalRef(c);
		}
	}

	g_attached = true;
	logger::log(std::string("[aiming] tick hook attached, jvmti caps=") +
	            JNIHook_AcquiredCapabilities());
	return true;
}

void enhance::modules::aiming::tick_movement_hook::shutdown()
{
	// Undo the class redefinition FIRST. Everything after is bookkeeping; this
	// is the part that decides whether unloading the DLL is survivable.
	if (g_hooked_mid)
	{
		const jnihook_result_t r = JNIHook_Detach(g_hooked_mid);
		g_detached_cleanly = (r == JNIHOOK_OK);
		logger::log(std::string("[unload] tick detach result=") + std::to_string((int)r));
		g_hooked_mid = nullptr;
	}
	else
	{
		logger::log("[unload] tick was never attached");
	}

	if (g_hooked_mid_ch)
	{
		const jnihook_result_t r = JNIHook_Detach(g_hooked_mid_ch);
		g_ch_detached_ok = (r == JNIHOOK_OK);
		logger::log(std::string("[unload] crosshair detach result=") + std::to_string((int)r));
		g_hooked_mid_ch = nullptr;
	}
	ORIG_crosshair = nullptr;
	g_ch_attached = false;


	{
		struct { jmethodID* mid; jmethodID* orig; bool* ok; const char* label; } extras[] = {
			{ &g_mid_on_pos_look, &ORIG_on_pos_look, &g_net_detached_ok,     "net: teleport ack" },
			{ &g_mid_on_rotation, &ORIG_on_rotation, &g_net_detached_ok,     "net: rotation reply" },
			{ &g_mid_on_ent_pos,  &ORIG_on_ent_pos,  &g_net_detached_ok,     "net: vehicle teleport" },
			{ &g_mid_trident,     &ORIG_trident,     &g_trident_detached_ok, "riptide" },
			{ &g_mid_horse_rot,   &ORIG_horse_rot,   &g_horse_detached_ok,   "mount rotation" },
			{ &g_hooked_mid_rs, &ORIG_update_render_state, &g_rs_detached_ok, "model pitch" },
		};

		for (auto& e : extras)
		{
			if (!*e.mid) continue;
			const jnihook_result_t r = JNIHook_Detach(*e.mid);
			if (r != JNIHOOK_OK) *e.ok = false;
			logger::log(std::string("[unload] ") + e.label + " detach result=" + std::to_string((int)r));
			*e.mid = nullptr;
			*e.orig = nullptr;
		}
	}

	if (g_hooked_mid_ii)
	{
		const jnihook_result_t r = JNIHook_Detach(g_hooked_mid_ii);
		g_ii_detached_ok = (r == JNIHOOK_OK);
		logger::log(std::string("[unload] item-use detach result=") + std::to_string((int)r));
		g_hooked_mid_ii = nullptr;
	}
	ORIG_interact_item = nullptr;

	g_swapping.store(false, std::memory_order_release);
	ORIG_tick = nullptr;
	g_mid_get_yaw = nullptr;
	g_mid_set_yaw = nullptr;
	g_mid_get_pitch = nullptr;
	g_mid_set_pitch = nullptr;

	if (g_mc_class && enhance::instance)
	{
		try
		{
			auto env = enhance::instance->get_env();
			if (env) env->DeleteGlobalRef(g_mc_class);
		}
		catch (...) {}
	}
	g_mc_class = nullptr;

	if (enhance::instance)
	{
		try
		{
			auto e = enhance::instance->get_env();
			if (e)
			{
				if (g_gr_class)     e->DeleteGlobalRef(g_gr_class);
				if (g_im_class)      e->DeleteGlobalRef(g_im_class);
				if (g_net_class)     e->DeleteGlobalRef(g_net_class);
				if (g_trident_class) e->DeleteGlobalRef(g_trident_class);
				if (g_horse_class)   e->DeleteGlobalRef(g_horse_class);
				if (g_rs_class)      e->DeleteGlobalRef(g_rs_class);
				if (g_mc_class_ref) e->DeleteGlobalRef(g_mc_class_ref);
			}
		}
		catch (...) {}
	}
	g_gr_class = nullptr;
	g_im_class = nullptr;
	g_net_class = nullptr;
	g_trident_class = nullptr;
	g_horse_class = nullptr;
	g_rs_class = nullptr;
	g_mc_class_ref = nullptr;
	g_fid_mc_instance = nullptr;
	g_fid_mc_player = nullptr;

	g_attached = false;
}

bool enhance::modules::aiming::tick_movement_hook::detached_cleanly()
{
	// Both hooks must be undone before the DLL can be unmapped; either one left
	// in place is a call into freed memory.
	return g_detached_cleanly && g_ch_detached_ok && g_ii_detached_ok &&
	       g_net_detached_ok && g_trident_detached_ok && g_horse_detached_ok &&
	       g_rs_detached_ok;
}
bool enhance::modules::aiming::tick_movement_hook::attached()         { return g_attached; }
bool enhance::modules::aiming::tick_movement_hook::is_swapping()      { return g_swapping.load(std::memory_order_acquire); }

