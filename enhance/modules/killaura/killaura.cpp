#include "killaura.h"
#include "target_tracker.h"
#include "aim_point.h"
#include "silent_rotation_hook.h"

#include "../aiming/silent_aim.h"
#include "../aiming/multipoint.h"
#include "../aiming/rotation.h"

#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../hooks/Hook.h"
#include "../../utils/client_thread.h"
#include "../../utils/logger.h"
#include "../../gui/GUI.h"

#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/player/player.h>
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>

#include <windows.h>
#include <cstdint>
#include <cmath>
#include <random>

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

	// --- clicker state ------------------------------------------------------
	//
	// All on the enhance worker thread, which is the only thread run() touches.
	std::mt19937 g_rng{ std::random_device{}() };
	uint64_t     g_last_attack_ms = 0;
	int          g_current_delay_ms = 0;   // 0 = pick a fresh one next attack

	// When the current target's rotation first went out. The attack is now sent
	// BEFORE the tick's flying packet (to satisfy Grim's Post/PacketOrder), so
	// the server validates the hit against the PREVIOUS flying packet's rotation.
	// That rotation is only on-target once we have sent at least one fake-angle
	// move for this target, so the first swing waits this long after acquiring it
	// -- otherwise the first hit is checked against the real (pre-rotation) angle
	// and flags Hitbox. 0 = no rotation established yet.
	uint64_t     g_rotation_start_ms = 0;

	// --- sprint reset (W-tap) state -----------------------------------------
	//
	// To land a crit the hit must NOT be a sprint attack. The legit way to stop
	// sprinting is to let go of forward: release W and Minecraft stops sprint
	// through its own input path -- a valid STOP_SPRINTING packet AND matching
	// deceleration. Forcing setSprinting(false) + an out-of-band packet instead
	// desynced movement and spammed packets, which is what tripped Grim's
	// Simulation / BadPacketsX / PacketOrder. So we tap W: release it, wait a
	// couple of ticks for the stop to reach the server, attack, re-press W.
	bool     g_w_released = false;
	uint64_t g_w_release_ms = 0;

	void w_release()
	{
		if (g_w_released) return;
		INPUT in = {};
		in.type = INPUT_KEYBOARD;
		in.ki.wVk = 'W';
		in.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &in, sizeof(INPUT));
		g_w_released = true;
		g_w_release_ms = GetTickCount64();
	}

	void w_restore()
	{
		if (!g_w_released) return;
		INPUT in = {};
		in.type = INPUT_KEYBOARD;
		in.ki.wVk = 'W';
		in.ki.dwFlags = 0;   // key-down
		SendInput(1, &in, sizeof(INPUT));
		g_w_released = false;
	}

	// Milliseconds between clicks for a CPS drawn uniformly from [min, max].
	// Redrawn after every hit so the interval never settles into a constant --
	// a fixed period is exactly what a click-pattern check keys on.
	int cps_delay_ms()
	{
		int lo = globals::killaura_min_cps;
		int hi = globals::killaura_max_cps;
		if (lo < 1) lo = 1;
		if (hi < lo) hi = lo;
		std::uniform_int_distribution<int> d(lo, hi);
		const int cps = d(g_rng);
		return 1000 / (cps > 0 ? cps : 1);
	}

	// Local player's Y velocity, for the crit gate (Minecraft only lands a crit
	// while the attacker is falling). Returns false if it could not be read.
	bool velocity_y(sdk::entity_client& le, double& out)
	{
		auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
		if (!env) return false;
		jobject vel = le.get_velocity();
		if (!vel) return false;

		bool ok = false;
		jclass vc = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
		if (vc)
		{
			jfieldID fy = env->GetFieldID(vc, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
			if (fy)
			{
				out = env->GetDoubleField(vel, fy);
				if (env->ExceptionCheck()) env->ExceptionClear();
				else ok = true;
			}
			env->DeleteLocalRef(vc);
		}
		env->DeleteLocalRef(vel);
		return ok;
	}

	// The attack itself runs inside silent_rotation_hook's sendMovementPackets
	// hook, so that hook must be attached before a queued attack can ever
	// drain. Triggerbot brings it up the same way; init() returns early once
	// attached, so both callers racing is harmless. Posted to the client
	// thread because attaching from the worker needs can_suspend, which is
	// routinely unavailable right after a re-injection.
	bool ensure_attack_hook()
	{
		static bool s_ok = false;
		if (s_ok) return true;

		static uint64_t s_next_try = 0;
		const uint64_t now = GetTickCount64();
		if (now < s_next_try) return false;
		s_next_try = now + 2000;

		enhance::client_thread::post([]() {
			try { s_ok = enhance::modules::silent_rotation_hook::init(); } catch (...) {}
		});
		return s_ok;
	}

	// Drop a queued-but-unfired attack and forget the click timer. Called
	// whenever killaura stops working this tick, so it never lands a swing on a
	// target it has already let go of -- attacking a stale target is precisely
	// what trips the anti-cheat.
	void cancel_pending_attack()
	{
		enhance::modules::silent_rotation_hook::cancel_attack(
			enhance::modules::silent_rotation_hook::attack_owner::killaura);
		g_current_delay_ms = 0;
		g_rotation_start_ms = 0;   // rotation stream ended; re-establish before hitting
		w_restore();               // never leave W held up if we bailed mid-reset
	}

	// A gate that stopped the module, for the menu. Returning early with no
	// explanation is what made the old module impossible to diagnose.
	void blocked(const char* why)
	{
		g_dbg = {};
		g_dbg.blocked_by = why;
		cancel_pending_attack();

		if (globals::aiming_debug_log)
		{
			static uint64_t s_last = 0;
			const uint64_t t = GetTickCount64();
			if (t - s_last > 1000)
			{
				s_last = t;
				logger::log(std::string("[ka] blocked=") + why);
			}
		}
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
	//
	// Use Hook::get_window(), the hooked window handle every other module reads.
	// globals::mc_window is declared but NEVER assigned -- it is always nullptr,
	// so the old `!wnd` here fired every tick and killaura was permanently
	// "not focused": it tracked nothing and looked dead, while silent aim (which
	// has no focus gate) worked on the same target. That one wrong handle is why
	// killaura never rotated.
	const HWND wnd = Hook::get_window();
	// The menu is a screen: attacking through it is the same mistake as
	// aiming through it.
	if (GUI::get_is_init() && GUI::get_do_draw())
	{
		blocked("menu open");
		return;
	}

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

	// Runs every worker pass (~10 ms), NOT throttled to the tick rate: the aim to
	// a locked target must be recomputed and republished as fresh as possible, or
	// the flying packet carries a stale angle and a circling target drifts out of
	// its own hitbox server-side (Grim's Hitbox check). The expensive entity-list
	// scan is throttled separately inside the target tracker; refreshing the aim
	// to an already-locked target is cheap.

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
		cancel_pending_attack();

		if (globals::aiming_debug_log)
		{
			static uint64_t s_last = 0;
			const uint64_t t = GetTickCount64();
			if (t - s_last > 1000)
			{
				s_last = t;
				logger::log(std::string("[ka] no target (candidates=") +
				            std::to_string(td.candidates) + ", nearest=" +
				            std::to_string(td.distance) + ")");
			}
		}

		env->DeleteLocalRef(local_player);
		env->DeleteLocalRef(world);
		return;
	}

	g_dbg.has_target = true;

	// The aim point comes from the shared multipoint scan rather than a
	// killaura-local copy, so the module and silent aim cannot disagree about
	// where the target is.
	bool   have_point = false;
	double px = 0.0, py = 0.0, pz = 0.0;

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
		have_point = true;
		px = mp.x; py = mp.y; pz = mp.z;
	}

	// Fall back to the single aim point when multipoint produced nothing, exactly
	// as silent aim does. Without this killaura simply did not rotate whenever
	// multipoint returned invalid for the tracked target -- the difference that
	// made silent aim work and killaura look dead on the same target.
	if (!have_point)
	{
		const enhance::modules::killaura::AimPoint pt =
			enhance::modules::killaura::compute_aim_point(
				target, local_player,
				globals::killaura_range,
				real_yaw, real_pitch,
				/*ignore_walls=*/true,
				0.0f, 0.0f, 0.0f);
		if (pt.valid)
		{
			have_point = true;
			px = pt.x; py = pt.y; pz = pt.z;
		}
	}

	if (globals::aiming_debug_log)
	{
		static uint64_t s_last = 0;
		const uint64_t t = GetTickCount64();
		if (t - s_last > 1000)
		{
			s_last = t;
			logger::log(std::string("[ka] target=yes mp=") + (mp.valid ? "1" : "0") +
			            " have_point=" + (have_point ? "1" : "0") +
			            " candidates=" + std::to_string(td.candidates) +
			            " dist=" + std::to_string(td.distance) +
			            " -> " + (have_point ? "publishing rotation" : "NO POINT, not rotating"));
		}
	}

	if (have_point)
	{
		float yaw = 0.0f, pitch = 0.0f;
		displacement_to_angles(px - le.get_x(),
		                       py - (le.get_y() + 1.62),
		                       pz - le.get_z(),
		                       yaw, pitch);
		yaw = enhance::modules::aiming::fixed_yaw(yaw, real_yaw);

		g_dbg.yaw = yaw;
		g_dbg.pitch = pitch;

		// Published into the same channel silent aim uses. Two producers
		// running their own selectors and fighting over the rotation is exactly
		// how the old module and the aiming rework ended up contradicting each
		// other; killaura takes precedence while it is on.
		enhance::modules::aiming::silent_aim::publish_external(yaw, pitch);

		// Mark when this target's fake rotation first went out, so the clicker can
		// wait until the server has actually received it before the first swing.
		if (g_rotation_start_ms == 0)
			g_rotation_start_ms = GetTickCount64();

		// Safety: with auto-attack off we never arm a reset, so make sure a W-tap
		// left over from a moment ago is undone rather than sticking.
		if (!globals::killaura_autoattack)
			w_restore();

		// --- clicker --------------------------------------------------------
		//
		// Swing on our own. The rotation was just published, so on the next JVM
		// tick the tick hook holds the fake angle across sendMovementPackets
		// (the look packet carries it) and the attack queued here drains right
		// after, on the same thread -- order Look(fake) -> Attack, which is what
		// the packet-order check wants. cancel_pending_attack() on every path
		// that loses the target makes sure a swing never lands late.
		if (globals::killaura_autoattack)
		{
			// Bring the attack path up UNCONDITIONALLY while we have a target,
			// not behind the timing/cooldown gates: it initialises asynchronously
			// (posted to the client thread), so gating the call itself behind
			// `due && cooldown_ok` meant a stuck cooldown read would keep it from
			// ever being posted. Pumped here, it is up within a tick or two.
			ensure_attack_hook();

			// Gate on attackEntity being RESOLVED, not on the hook having
			// attached: init() resolves the method before it installs the hook,
			// and tick_movement_hook drains the queued attack even when this
			// hook's own sendMovementPackets redefinition failed to install.
			const bool hook_ready = enhance::modules::silent_rotation_hook::attack_ready();

			const uint64_t now_ms = GetTickCount64();

			// Attack charge. Read always (not just when Require Cooldown is on),
			// because Dynamic Cooldown paces the clicker by it as well.
			float cooldown = 1.0f;
			{
				player_client pc;
				cooldown = pc.get_attack_cooldown_progress(0.5f);
			}
			const bool cooldown_full = cooldown >= 1.0f;
			// Require Cooldown gates on full charge; off, any charge is allowed.
			const bool cooldown_ok = !globals::killaura_require_cooldown || cooldown_full;

			// --- attack modifiers (the menu toggles that used to do nothing) ---
			//
			// Critical: Minecraft only lands a crit while the attacker is airborne
			// and moving down. only_critical holds fire until then; smart_critical
			// additionally waits through the rising half of a jump instead of
			// firing at the apex.
			bool crit_ok = true;
			bool waiting_crit = false;
			if (globals::killaura_only_critical || globals::killaura_smart_critical)
			{
				crit_ok = false;
				if (!le.is_on_ground())
				{
					double vy = 0.0;
					if (!velocity_y(le, vy))       crit_ok = true;        // unknown -> allow
					else if (vy < -0.08)           crit_ok = true;        // falling -> crit
					else if (globals::killaura_smart_critical && vy > 0.1) waiting_crit = true;
				}
			}

			// Don't swing while the local player is using an item (eating,
			// drinking, blocking, drawing a bow) -- the swing would cancel it.
			const bool using_item = le.is_using_item();
			const bool eat_ok = !(globals::killaura_no_attack_when_eat && using_item);

			if (g_current_delay_ms == 0)
				g_current_delay_ms = cps_delay_ms();

			// Real distance to the CURRENT target, computed here rather than read
			// from the tracker's debug field: that field is only refreshed on a
			// fresh scan and goes stale the moment the target is locked, so gating
			// the swing on it was gating on the distance at acquisition.
			sdk::entity_client te(target);
			const double ddx = te.get_x() - le.get_x();
			const double ddy = te.get_y() - le.get_y();
			const double ddz = te.get_z() - le.get_z();
			const double target_dist = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);

			const bool in_range = target_dist <= static_cast<double>(globals::killaura_range);

			// Pacing: Dynamic Cooldown hits at the weapon's own attack speed (as
			// soon as the charge is full again); otherwise the randomized CPS.
			bool due;
			if (globals::killaura_dynamic_cooldown)
				due = cooldown_full;
			else
				due = (g_last_attack_ms == 0) ||
				      (now_ms - g_last_attack_ms >= static_cast<uint64_t>(g_current_delay_ms));

			// The rotation must have been on the wire for a couple of ticks before
			// the first swing, so the before-move attack is validated against a
			// fake (on-target) flying packet rather than the real pre-rotation one.
			const bool established = g_rotation_start_ms != 0 &&
			                         (now_ms - g_rotation_start_ms >= 100);

			const bool fire = hook_ready && in_range && cooldown_ok &&
			                  crit_ok && eat_ok && established && due;
			if (fire)
			{
				// Sprint reset (W-tap) before the swing so a falling hit lands as
				// a crit, not a sprint attack. Only while the player is actually
				// holding W and Sprint Bypass is on.
				const bool want_reset = globals::killaura_sprint_bypass != 0 &&
				                        (GetAsyncKeyState('W') & 0x8000) != 0;

				if (want_reset && !g_w_released)
				{
					// Start the reset: release W now. Minecraft stops sprinting on
					// its own over the next tick (a valid STOP_SPRINTING packet and
					// real deceleration); the attack waits for that below.
					w_release();
				}
				else if (!want_reset || (now_ms - g_w_release_ms >= 100))
				{
					// No reset needed, or the sprint has had ~2 ticks to stop
					// server-side. Swing now, then let sprint resume.
					enhance::modules::silent_rotation_hook::queue_attack(
						target, enhance::modules::silent_rotation_hook::attack_owner::killaura);
					w_restore();

					g_last_attack_ms = now_ms;
					g_current_delay_ms = cps_delay_ms();

					globals::killaura_last_attack_ms = now_ms;
					globals::killaura_attack_count++;
					g_dbg.attacked = true;
				}
				// else: reset in progress, still waiting for the stop -- hold.
			}

			// If a reset was armed but the swing is off the table now (crit window
			// passed, target left range, cooldown), never leave W held up.
			if (g_w_released && !fire)
				w_restore();

			// Decisive per-gate readout: "does nothing" and "blocked by gate X"
			// must not look the same. One line a second, only with the aiming
			// debug log on.
			if (globals::aiming_debug_log)
			{
				static uint64_t s_last = 0;
				if (now_ms - s_last > 1000)
				{
					s_last = now_ms;
					logger::log(std::string("[ka-click] fire=") + (fire ? "YES" : "no") +
					            " hook=" + (hook_ready ? "up" : "DOWN") +
					            " dist=" + std::to_string(target_dist) +
					            " in_range=" + (in_range ? "1" : "0") +
					            " cooldown=" + std::to_string(cooldown) +
					            " cd_ok=" + (cooldown_ok ? "1" : "0") +
					            " crit_ok=" + (crit_ok ? "1" : "0") +
					            (waiting_crit ? " (waiting apex)" : "") +
					            " eat_ok=" + (eat_ok ? "1" : "0") +
					            " est=" + (established ? "1" : "0") +
					            " due=" + (due ? "1" : "0") +
					            " dyn_cd=" + (globals::killaura_dynamic_cooldown ? "1" : "0") +
					            " count=" + std::to_string(globals::killaura_attack_count));
				}
			}
		}
	}
	else
	{
		enhance::modules::aiming::silent_aim::clear_external();
		cancel_pending_attack();
	}

	env->DeleteLocalRef(target);
	env->DeleteLocalRef(local_player);
	env->DeleteLocalRef(world);
}
