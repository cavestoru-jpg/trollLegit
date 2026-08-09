#include "killaura.h"
#include "target_tracker.h"
#include "aim_point.h"

#include "../aiming/silent_aim.h"
#include "../aiming/multipoint.h"
#include "../aiming/rotation.h"

#include "../../enhance.h"
#include "../../globals/globals.h"

#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>

#include <windows.h>

// Killaura, rebuilt as a port of LiquidBounce's ModuleKillAura.
//
// This step is the skeleton: gates, target tracking and aiming. It does not
// attack yet -- the clicker lands next -- so that "the aim does not track" and
// "the attack does not fire" are separable in game rather than tangled.
//
// Threading: everything here runs on the enhance worker, because target
// selection and the aim point go through the SDK and the SDK uses the worker's
// JNIEnv. Nothing in this file may write game state or send packets; that is
// the tick thread's job and it reaches it through the published rotation and,
// later, through silent_rotation_hook::queue_attack.
namespace
{
	enhance::modules::killaura::debug_info g_dbg;

	// A gate that stopped the module, for the menu. Returning early with no
	// explanation is what made the old module impossible to diagnose.
	void blocked(const char* why)
	{
		g_dbg = {};
		g_dbg.blocked_by = why;
	}

	bool keybind_active()
	{
		// Mode 2 ("Always") is the historical default and means ignore the key.
		if (globals::killaura_keybind == 0 || globals::killaura_keybind_mode == 2)
			return true;

		const bool down = (GetAsyncKeyState(globals::killaura_keybind) & 0x8000) != 0;
		if (globals::killaura_keybind_mode == 0)
			return down;   // hold

		// Toggle: flip on the press edge, not while held.
		static bool s_prev = false;
		static bool s_state = false;
		if (down && !s_prev) s_state = !s_state;
		s_prev = down;
		return s_state;
	}
}

enhance::modules::killaura::debug_info enhance::modules::killaura::debug_state()
{
	return g_dbg;
}

void enhance::modules::killaura::run()
{
	if (!globals::killaura_enabled)
	{
		reset();
		blocked("off");
		return;
	}

	// Only while Minecraft has focus. Swinging at someone because a background
	// window still ticked is both useless and conspicuous.
	const HWND wnd = globals::mc_window;
	if (!wnd || GetForegroundWindow() != wnd)
	{
		reset();
		blocked("not focused");
		return;
	}

	if (!keybind_active())
	{
		reset();
		blocked("keybind");
		return;
	}

	// Matched to the tick rate. Selecting more often than the server is told
	// anything walks the entity list for nothing.
	static uint64_t s_last_ms = 0;
	const uint64_t now = GetTickCount64();
	if (s_last_ms != 0 && now - s_last_ms < 50) return;
	s_last_ms = now;

	if (!sdk::instance || !enhance::instance) { blocked("no sdk"); return; }

	auto env = enhance::instance->get_env();
	if (!env) { blocked("no env"); return; }

	jobject world = sdk::instance->get_world();
	if (!world) { blocked("no world"); return; }

	jobject local_player = sdk::instance->get_player();
	if (!local_player)
	{
		env->DeleteLocalRef(world);
		blocked("no player");
		return;
	}

	sdk::entity_client le(local_player);
	const float real_yaw   = le.get_yaw();
	const float real_pitch = le.get_pitch();

	tracker_settings ts;
	ts.range   = globals::killaura_range;
	ts.fov     = globals::killaura_fov;
	ts.sort    = static_cast<sort_mode>(globals::killaura_sort_mode);
	ts.players = globals::killaura_target_players;
	ts.mobs    = globals::killaura_target_mobs;
	ts.animals = globals::killaura_target_animals;
	ts.friends = globals::killaura_target_friends;

	jobject target = update_target(world, local_player, real_yaw, real_pitch, ts);

	const auto td = tracker_debug_state();

	g_dbg = {};
	g_dbg.active = true;
	g_dbg.locked = td.locked;
	g_dbg.candidates = td.candidates;
	g_dbg.distance = td.distance;

	if (!target)
	{
		enhance::modules::aiming::silent_aim::clear_external();
		env->DeleteLocalRef(local_player);
		env->DeleteLocalRef(world);
		return;
	}

	g_dbg.has_target = true;

	// The aim point comes from the shared multipoint scan rather than a
	// killaura-local copy, so the module and silent aim cannot disagree about
	// where the target is.
	const auto mp = enhance::modules::aiming::multipoint::compute(
		target, local_player,
		globals::killaura_range,
		globals::killaura_range,
		real_yaw, real_pitch,
		globals::silent_aim_resolution,
		globals::silent_aim_point_mode,
		/*ignore_walls=*/true);

	if (mp.valid)
	{
		float yaw = 0.0f, pitch = 0.0f;
		displacement_to_angles(mp.x - le.get_x(),
		                       mp.y - (le.get_y() + 1.62),
		                       mp.z - le.get_z(),
		                       yaw, pitch);
		yaw = enhance::modules::aiming::fixed_yaw(yaw, real_yaw);

		g_dbg.yaw = yaw;
		g_dbg.pitch = pitch;

		// Published into the same channel silent aim uses. Two producers
		// running their own selectors and fighting over the rotation is exactly
		// how the old module and the aiming rework ended up contradicting each
		// other; killaura takes precedence while it is on.
		enhance::modules::aiming::silent_aim::publish_external(yaw, pitch);
	}
	else
	{
		enhance::modules::aiming::silent_aim::clear_external();
	}

	env->DeleteLocalRef(target);
	env->DeleteLocalRef(local_player);
	env->DeleteLocalRef(world);
}
