#include "target_tracker.h"
#include "target_selector.h"

#include "../../enhance.h"

#include <sdk/minecraft/entity/entity.h>

#include <cmath>
#include <chrono>

namespace
{
	// A global ref, because the committed target has to survive between worker
	// iterations and a local ref does not. Released on every replacement and on
	// reset(); leaking these fills the JVM's global table over a session.
	jobject g_locked = nullptr;

	bool   g_dbg_locked = false;
	int    g_dbg_candidates = 0;
	double g_dbg_distance = -1.0;

	void clear_lock(JNIEnv* env)
	{
		if (g_locked && env) env->DeleteGlobalRef(g_locked);
		g_locked = nullptr;
	}

	// Is the committed target still worth keeping? Alive, present, and inside
	// the slackened range.
	bool lock_still_valid(JNIEnv* env, jobject local_player, float range)
	{
		if (!g_locked) return false;

		sdk::entity_client te(g_locked);
		if (te.get_health() <= 0.0f) return false;

		sdk::entity_client le(local_player);
		const double dx = te.get_x() - le.get_x();
		const double dy = te.get_y() - le.get_y();
		const double dz = te.get_z() - le.get_z();
		return std::sqrt(dx * dx + dy * dy + dz * dz) <= range;
	}
}

void enhance::modules::killaura::reset()
{
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	clear_lock(env);
	g_dbg_locked = false;
}

enhance::modules::killaura::tracker_debug
enhance::modules::killaura::tracker_debug_state()
{
	tracker_debug d;
	d.locked = g_dbg_locked;
	d.candidates = g_dbg_candidates;
	d.distance = g_dbg_distance;
	return d;
}

jobject enhance::modules::killaura::update_target(jobject world, jobject local_player,
                                                  float current_yaw, float current_pitch,
                                                  const tracker_settings& s)
{
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env || !world || !local_player) return nullptr;

	// Keep the committed target if it is still valid. This is the whole point
	// of the tracker: switching every tick to whatever is marginally closer
	// produces an aim that serves two targets and hits neither.
	if (lock_still_valid(env, local_player, s.range + s.lock_range_slack))
	{
		g_dbg_locked = true;
		return env->NewLocalRef(g_locked);
	}

	clear_lock(env);
	g_dbg_locked = false;

	// Throttle the expensive entity-list scan to ~tick rate. run() now calls this
	// every worker pass (~10 ms) so the aim to a LOCKED target stays fresh, but a
	// full re-scan that often is wasted work; between scans there is simply no
	// target for that pass, which is a brief gap only while re-acquiring.
	static unsigned long long s_scan_ms = 0;
	const unsigned long long now_ms = static_cast<unsigned long long>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	if (s_scan_ms != 0 && now_ms - s_scan_ms < 50)
		return nullptr;
	s_scan_ms = now_ms;

	const TargetFilter filter{ s.players, s.mobs, s.animals, s.friends };

	// Sorting beyond nearest-first is not wired yet -- pick_best_target orders
	// by distance and FOV internally. The mode is carried here so the menu and
	// the config can settle before the comparator moves in, rather than adding
	// a setting later that silently did nothing in between.
	jobject best = pick_best_target(world, local_player,
	                                s.range, s.fov,
	                                current_yaw, current_pitch,
	                                /*ignore_walls=*/true,
	                                filter);

	const auto& stats = last_selection_stats();
	g_dbg_candidates = stats.scanned;
	g_dbg_distance = stats.nearest;

	if (best)
		g_locked = env->NewGlobalRef(best);

	return best;
}
