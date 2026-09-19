#include "silent_aim.h"
#include "rotation.h"
#include "../killaura/target_selector.h"
#include "../killaura/aim_point.h"
#include "../killaura/silent_rotation_hook.h"
#include "multipoint.h"
#include "tick_movement_hook.h"
#include "../../utils/client_thread.h"

#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"

#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/player/player.h>

#include <atomic>
#include <cstring>
#include <cmath>
#include <random>
#include <windows.h>

namespace
{
	// yaw and pitch travel as one 64-bit word so the tick thread can never read
	// a half-updated pair -- a fresh yaw against a stale pitch would aim at a
	// point that was never chosen.
	std::atomic<uint64_t> g_packed{0};
	std::atomic<uint64_t> g_stamp_ms{0};

	// How long a published pair stays usable. If the worker stalls or the
	// module is switched off mid-swing, the angles must expire rather than
	// leave the player frozen onto a target that is gone. Three ticks.
	constexpr uint64_t k_max_age_ms = 150;

	uint64_t pack(float yaw, float pitch)
	{
		uint32_t y, p;
		std::memcpy(&y, &yaw, 4);
		std::memcpy(&p, &pitch, 4);
		return (static_cast<uint64_t>(y) << 32) | p;
	}

	void unpack(uint64_t v, float& yaw, float& pitch)
	{
		const uint32_t y = static_cast<uint32_t>(v >> 32);
		const uint32_t p = static_cast<uint32_t>(v & 0xFFFFFFFFull);
		std::memcpy(&yaw, &y, 4);
		std::memcpy(&pitch, &p, 4);
	}

	std::atomic<bool>   g_dbg_has_target{false};
	std::atomic<double> g_dbg_distance{-1.0};
	std::atomic<int>    g_dbg_scanned{0};
	std::atomic<int>    g_dbg_points{0};

	// Set while an external producer (killaura) owns the channel. run() then
	// leaves the published pair alone rather than overwriting it with its own
	// scan a few milliseconds later.
	std::atomic<bool> g_external{false};

	void publish_none()
	{
		g_stamp_ms.store(0, std::memory_order_release);
		g_dbg_has_target.store(false, std::memory_order_relaxed);
	}

	// --- auto-attack (worker thread) ---------------------------------------
	std::mt19937 g_click_rng{ std::random_device{}() };
	uint64_t     g_click_last_ms = 0;
	int          g_click_delay_ms = 0;

	int click_delay_ms()
	{
		int lo = globals::silent_aim_min_cps;
		int hi = globals::silent_aim_max_cps;
		if (lo < 1) lo = 1;
		if (hi < lo) hi = lo;
		std::uniform_int_distribution<int> d(lo, hi);
		const int cps = d(g_click_rng);
		return 1000 / (cps > 0 ? cps : 1);
	}

	// Bring silent_rotation_hook up so a queued attack can drain. Posted to the
	// client thread (attaching from the worker needs can_suspend), same as
	// triggerbot and killaura. init() resolves attackEntity before it installs
	// the hook, so the attack still fires via tick_movement_hook even if the
	// hook itself fails to attach.
	bool click_ensure_hook()
	{
		static bool s_ok = false;
		if (s_ok) return true;
		static uint64_t s_next = 0;
		const uint64_t now = GetTickCount64();
		if (now < s_next) return false;
		s_next = now + 2000;
		enhance::client_thread::post([]() {
			try { s_ok = enhance::modules::silent_rotation_hook::init(); } catch (...) {}
		});
		return s_ok;
	}

	void click_cancel()
	{
		enhance::modules::silent_rotation_hook::cancel_attack(
			enhance::modules::silent_rotation_hook::attack_owner::killaura);
		g_click_delay_ms = 0;
	}
}

enhance::modules::aiming::silent_aim::angles_t
enhance::modules::aiming::silent_aim::current()
{
	angles_t out;

	const uint64_t stamp = g_stamp_ms.load(std::memory_order_acquire);
	if (stamp == 0) return out;
	if (GetTickCount64() - stamp > k_max_age_ms) return out;

	unpack(g_packed.load(std::memory_order_acquire), out.yaw, out.pitch);
	out.active = true;
	return out;
}

enhance::modules::aiming::silent_aim::debug_t
enhance::modules::aiming::silent_aim::debug_state()
{
	debug_t d;
	d.has_target = g_dbg_has_target.load(std::memory_order_relaxed);
	d.distance   = g_dbg_distance.load(std::memory_order_relaxed);
	d.scanned    = g_dbg_scanned.load(std::memory_order_relaxed);
	d.points     = g_dbg_points.load(std::memory_order_relaxed);
	unpack(g_packed.load(std::memory_order_relaxed), d.yaw, d.pitch);
	return d;
}

void enhance::modules::aiming::silent_aim::publish_external(float yaw, float pitch)
{
	g_external.store(true, std::memory_order_release);
	g_packed.store(pack(yaw, pitch), std::memory_order_release);
	g_stamp_ms.store(GetTickCount64(), std::memory_order_release);
	g_dbg_has_target.store(true, std::memory_order_relaxed);
}

void enhance::modules::aiming::silent_aim::clear_external()
{
	if (g_external.exchange(false, std::memory_order_acq_rel))
		publish_none();
}

void enhance::modules::aiming::silent_aim::run()
{
	// Install the hooks that APPLY the angles.
	//
	// These used to be installed only from killaura::run(), below its own
	// early return -- so with killaura switched off they were never installed
	// at all, and silent aim published angles that nothing consumed. The module
	// looked alive from the log (it found targets and computed rotations) while
	// doing nothing whatsoever in game. Silent aim does not depend on killaura,
	// so it must not depend on killaura having been enabled either.
	//
	// Posted to the client thread: attaching from the worker needs can_suspend,
	// which is routinely unavailable after a re-injection, and that failure is
	// silent. init() returns early once attached, so calling it from here and
	// from killaura both is harmless.
	if (globals::silent_aim_enabled || globals::silent_rotation_enabled ||
	    globals::killaura_enabled)
	{
		static bool s_hooks_tried = false;
		if (!s_hooks_tried)
		{
			s_hooks_tried = true;
			enhance::client_thread::post([]() {
				try { enhance::modules::aiming::tick_movement_hook::init(); } catch (...) {}
			});
		}
	}

	// An external producer -- killaura -- owns the channel while it is running.
	// Two selectors publishing into the same pair a few milliseconds apart is
	// how the aim ends up serving two targets and hitting neither.
	if (g_external.load(std::memory_order_acquire))
		return;

	if (!globals::silent_aim_enabled)
	{
		publish_none();
		return;
	}

	// Matched to the tick rate. Selecting more often than the server is told
	// anything buys nothing and walks the entity list for free.
	static uint64_t s_last_ms = 0;
	const uint64_t now = GetTickCount64();
	if (s_last_ms != 0 && now - s_last_ms < 50) return;
	s_last_ms = now;

	if (!sdk::instance || !enhance::instance) { publish_none(); return; }

	auto env = enhance::instance->get_env();
	if (!env) { publish_none(); return; }

	jobject world = sdk::instance->get_world();
	if (!world) { publish_none(); return; }

	jobject local_player = sdk::instance->get_player();
	if (!local_player)
	{
		env->DeleteLocalRef(world);
		publish_none();
		return;
	}

	sdk::entity_client le(local_player);
	// From the tick thread, not from the live field: the hooks hold the silent
	// angle in it for the length of a tick, so sampling here is a coin toss
	// between the real angle and the fake one -- and aiming off the fake one
	// feeds straight back into the next tick.
	float real_yaw   = le.get_yaw();
	float real_pitch = le.get_pitch();
	enhance::modules::aiming::tick_movement_hook::true_rotation(real_yaw, real_pitch);

	const enhance::modules::killaura::TargetFilter filter{
		globals::killaura_target_players,
		globals::killaura_target_mobs,
		globals::killaura_target_animals,
		globals::killaura_target_friends,
	};

	jobject target = enhance::modules::killaura::pick_best_target(
		world, local_player,
		globals::silent_aim_range,
		globals::silent_aim_fov,
		real_yaw, real_pitch,
		/*ignore_walls=*/true,
		filter);

	const auto& stats = enhance::modules::killaura::last_selection_stats();
	g_dbg_scanned.store(stats.scanned, std::memory_order_relaxed);
	g_dbg_distance.store(stats.nearest, std::memory_order_relaxed);

	if (!target)
	{
		click_cancel();
		env->DeleteLocalRef(world);
		env->DeleteLocalRef(local_player);
		publish_none();
		return;
	}

	// No jitter here. The processors that add human-looking noise live in
	// processors.cpp and belong on top of a working baseline, not underneath
	// one being brought up for the first time -- with them on, an aim that is
	// simply pointing at the wrong place and an aim that is being deliberately
	// perturbed look identical.
	bool   have_point = false;
	double px = 0.0, py = 0.0, pz = 0.0;

	if (globals::silent_aim_multipoint)
	{
		const auto mp = enhance::modules::aiming::multipoint::compute(
			target, local_player,
			globals::silent_aim_range,
			globals::silent_aim_walls_range,
			real_yaw, real_pitch,
			globals::silent_aim_resolution,
			globals::silent_aim_point_mode,
			/*ignore_walls=*/true);

		if (mp.valid)
		{
			have_point = true;
			px = mp.x; py = mp.y; pz = mp.z;
		}
		g_dbg_points.store(mp.considered, std::memory_order_relaxed);
	}

	// Falls through to the single-point path when multipoint produced nothing,
	// rather than giving up on the target. Multipoint is an improvement to WHERE
	// the aim points; it must never be the reason the aim stops pointing at all.
	if (!have_point)
	{
		const enhance::modules::killaura::AimPoint pt =
			enhance::modules::killaura::compute_aim_point(
				target, local_player,
				globals::silent_aim_range,
				real_yaw, real_pitch,
				/*ignore_walls=*/true,
				0.0f, 0.0f, 0.0f);
		if (pt.valid)
		{
			have_point = true;
			px = pt.x; py = pt.y; pz = pt.z;
		}
	}

	if (have_point)
	{
		float yaw = 0.0f, pitch = 0.0f;
		// 1.62 is the standing eye height, matching what killaura uses. It is
		// wrong while sneaking or crawling; that is a known shared inaccuracy
		// rather than something introduced here, and worth fixing in one place
		// once the baseline works.
		enhance::modules::killaura::displacement_to_angles(
			px - le.get_x(),
			py - (le.get_y() + 1.62),
			pz - le.get_z(),
			yaw, pitch);

		// Wind the yaw onto the same turn as the player's real one. The angle
		// itself is direction-only, so an unwound value here would send the
		// body the long way round to a bearing it is already close to.
		yaw = enhance::modules::aiming::fixed_yaw(yaw, real_yaw);

		g_packed.store(pack(yaw, pitch), std::memory_order_release);
		g_stamp_ms.store(GetTickCount64(), std::memory_order_release);
		g_dbg_has_target.store(true, std::memory_order_relaxed);

		// --- auto-attack ----------------------------------------------------
		//
		// Silent aim on its own only turns; this makes it swing too, so it is a
		// full aura without needing killaura. Reached only when killaura is NOT
		// publishing (run() returns early on g_external above), so the single
		// pending-attack slot is never contended. Same tick-thread drain as
		// killaura: the rotation was just published, so on the next tick the
		// look packet carries it and the queued attack fires right after.
		if (globals::silent_aim_autoattack)
		{
			click_ensure_hook();

			const uint64_t now_ms = GetTickCount64();

			float cooldown = 1.0f;
			if (globals::silent_aim_require_cooldown)
			{
				player_client pc;
				cooldown = pc.get_attack_cooldown_progress(0.5f);
			}
			const bool cooldown_ok = cooldown >= 1.0f;

			if (g_click_delay_ms == 0)
				g_click_delay_ms = click_delay_ms();

			sdk::entity_client te(target);
			const double ddx = te.get_x() - le.get_x();
			const double ddy = te.get_y() - le.get_y();
			const double ddz = te.get_z() - le.get_z();
			const double target_dist = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
			const bool in_range = target_dist <= static_cast<double>(globals::silent_aim_range);

			const bool ready = enhance::modules::silent_rotation_hook::attack_ready();
			const bool due = (g_click_last_ms == 0) ||
			                 (now_ms - g_click_last_ms >= static_cast<uint64_t>(g_click_delay_ms));

			const bool fire = ready && in_range && cooldown_ok && due;
			if (fire)
			{
				enhance::modules::silent_rotation_hook::queue_attack(
					target, enhance::modules::silent_rotation_hook::attack_owner::killaura);
				g_click_last_ms = now_ms;
				g_click_delay_ms = click_delay_ms();
			}

			if (globals::aiming_debug_log)
			{
				static uint64_t s_last = 0;
				if (now_ms - s_last > 1000)
				{
					s_last = now_ms;
					logger::log(std::string("[saim-click] fire=") + (fire ? "YES" : "no") +
					            " hook=" + (ready ? "up" : "DOWN") +
					            " dist=" + std::to_string(target_dist) +
					            " range=" + std::to_string(globals::silent_aim_range) +
					            " in_range=" + (in_range ? "1" : "0") +
					            " cooldown=" + std::to_string(cooldown) +
					            " cd_ok=" + (cooldown_ok ? "1" : "0") +
					            " due=" + (due ? "1" : "0") +
					            " delay=" + std::to_string(g_click_delay_ms));
				}
			}
		}
	}
	else
	{
		click_cancel();
		publish_none();
	}

	if (globals::aiming_debug_log)
	{
		static uint64_t s_last_log = 0;
		const uint64_t t = GetTickCount64();
		if (t - s_last_log > 1000)
		{
			s_last_log = t;
			float y = 0.0f, p = 0.0f;
			unpack(g_packed.load(std::memory_order_relaxed), y, p);
			logger::log(std::string("[saim] point=") + (have_point ? "yes" : "no") +
			            " yaw=" + std::to_string(y) +
			            " pitch=" + std::to_string(p) +
			            " real_yaw=" + std::to_string(real_yaw) +
			            " real_pitch=" + std::to_string(real_pitch) +
			            " scanned=" + std::to_string(stats.scanned) +
			            " points=" + std::to_string(g_dbg_points.load(std::memory_order_relaxed)));
		}
	}

	env->DeleteLocalRef(target);
	env->DeleteLocalRef(local_player);
	env->DeleteLocalRef(world);
}
