#include "rotation_manager.h"
#include "../../globals/globals.h"

#include <algorithm>
#include <chrono>

namespace enhance::modules::aiming
{
	namespace
	{
		// Monotonic milliseconds. std::chrono rather than GetTickCount64 so this
		// file stays free of platform headers, like the rest of the module.
		unsigned long long now_ms()
		{
			using namespace std::chrono;
			return static_cast<unsigned long long>(
				duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
		}
	}

	rotation_manager_t& manager()
	{
		static rotation_manager_t s_manager;
		return s_manager;
	}

	void sync_settings_from_globals()
	{
		processor_chain_t& p = manager().processors;

		// A max below the min would make the step cap negative and the
		// rotation walk backwards, so clamp rather than trust the sliders.
		// (std::max) is parenthesised throughout because windows.h defines max
		// as a macro and would otherwise eat the call. A max below its min
		// would make a step cap negative and walk the rotation backwards, so
		// the sliders are clamped here rather than trusted.
		p.smooth.mode = static_cast<smooth_mode_t>(
			std::clamp(globals::aiming_smooth_mode, 0, 3));

		p.smooth.horizontal.min = globals::aiming_speed_min;
		p.smooth.horizontal.max = (std::max)(globals::aiming_speed_min, globals::aiming_speed_max);
		p.smooth.vertical = p.smooth.horizontal;

		p.smooth.sigmoid_steepness = globals::aiming_sigmoid_steepness;
		p.smooth.sigmoid_midpoint  = globals::aiming_sigmoid_midpoint;

		p.smooth.interp_horizontal = { globals::aiming_interp_h_min,
			(std::max)(globals::aiming_interp_h_min, globals::aiming_interp_h_max) };
		p.smooth.interp_vertical = { globals::aiming_interp_v_min,
			(std::max)(globals::aiming_interp_v_min, globals::aiming_interp_v_max) };
		p.smooth.interp_direction_change = { globals::aiming_interp_dirchange_min,
			(std::max)(globals::aiming_interp_dirchange_min, globals::aiming_interp_dirchange_max) };
		p.smooth.interp_midpoint = globals::aiming_interp_midpoint;

		p.smooth.yaw_acceleration = { globals::aiming_accel_yaw_min,
			(std::max)(globals::aiming_accel_yaw_min, globals::aiming_accel_yaw_max) };
		p.smooth.pitch_acceleration = { globals::aiming_accel_pitch_min,
			(std::max)(globals::aiming_accel_pitch_min, globals::aiming_accel_pitch_max) };
		p.smooth.acceleration_error_enabled = globals::aiming_accel_error_enabled;
		p.smooth.yaw_acceleration_error   = globals::aiming_accel_yaw_error;
		p.smooth.pitch_acceleration_error = globals::aiming_accel_pitch_error;
		p.smooth.constant_error_enabled = globals::aiming_const_error_enabled;
		p.smooth.yaw_constant_error   = globals::aiming_const_yaw_error;
		p.smooth.pitch_constant_error = globals::aiming_const_pitch_error;
		p.smooth.sigmoid_deceleration_enabled = globals::aiming_sigmoid_decel_enabled;
		p.smooth.deceleration_steepness = globals::aiming_decel_steepness;
		p.smooth.deceleration_midpoint  = globals::aiming_decel_midpoint;

		p.fail.enabled = globals::aiming_fail_enabled;
		p.fail.rate    = globals::aiming_fail_rate;
		p.fail.factor  = globals::aiming_fail_factor;
		p.fail.strength_horizontal = { globals::aiming_fail_horiz_min,
			(std::max)(globals::aiming_fail_horiz_min, globals::aiming_fail_horiz_max) };
		p.fail.strength_vertical = { globals::aiming_fail_vert_min,
			(std::max)(globals::aiming_fail_vert_min, globals::aiming_fail_vert_max) };
		p.fail.transition_duration = { globals::aiming_fail_dur_min,
			(std::max)(globals::aiming_fail_dur_min, globals::aiming_fail_dur_max) };

		p.jitter.enabled = globals::aiming_jitter_enabled;
		p.jitter.micro_enabled = globals::aiming_jitter_micro_enabled;
		p.jitter.micro_yaw   = { 0.0f, globals::aiming_jitter_micro_yaw };
		p.jitter.micro_pitch = { 0.0f, globals::aiming_jitter_micro_pitch };
		p.jitter.burst_enabled = globals::aiming_jitter_burst_enabled;
		p.jitter.burst_rate  = globals::aiming_jitter_burst_rate;
		p.jitter.burst_duration = { globals::aiming_jitter_burst_dur_min,
			(std::max)(globals::aiming_jitter_burst_dur_min, globals::aiming_jitter_burst_dur_max) };
		p.jitter.burst_yaw   = { 0.0f, globals::aiming_jitter_burst_yaw };
		p.jitter.burst_pitch = { 0.0f, globals::aiming_jitter_burst_pitch };
		p.jitter.drift_enabled = globals::aiming_jitter_drift_enabled;
		p.jitter.drift_max_yaw   = globals::aiming_jitter_drift_max_yaw;
		p.jitter.drift_max_pitch = globals::aiming_jitter_drift_max_pitch;
		p.jitter.drift_step_strength  = globals::aiming_jitter_drift_step;
		p.jitter.drift_mean_reversion = globals::aiming_jitter_drift_reversion;

		p.short_stop.enabled = globals::aiming_shortstop_enabled;
		p.short_stop.rate    = globals::aiming_shortstop_rate;
		p.short_stop.stop_duration = { globals::aiming_shortstop_dur_min,
			(std::max)(globals::aiming_shortstop_dur_min, globals::aiming_shortstop_dur_max) };
	}

	void rotation_manager_t::request(provider_t provider, const rotation_target_t& target, int priority)
	{
		const int index = static_cast<int>(provider);
		if (index < 0 || index >= static_cast<int>(provider_t::count))
			return;

		std::lock_guard<std::mutex> lock(m_mutex);

		request_t& r = m_requests[index];
		r.active = true;
		r.priority = priority;
		r.target = target;

		// Absolute deadline, so a request that stops being renewed ages out on
		// its own without anyone having to remember to cancel it.
		const int ticks = target.ticks_until_reset > 0 ? target.ticks_until_reset : 1;
		r.expires_at_ms = now_ms() + static_cast<unsigned long long>(ticks) * 50ull;
	}

	void rotation_manager_t::withdraw(provider_t provider)
	{
		const int index = static_cast<int>(provider);
		if (index < 0 || index >= static_cast<int>(provider_t::count))
			return;

		std::lock_guard<std::mutex> lock(m_mutex);
		m_requests[index].active = false;
	}

	bool rotation_manager_t::pick_target()
	{
		const unsigned long long now = now_ms();
		bool found = false;
		int  best_priority = 0;

		for (auto& r : m_requests)
		{
			if (!r.active)
				continue;

			// Expire in place. Linear scan over a handful of slots -- a real
			// priority queue would be more code than the problem deserves.
			if (r.expires_at_ms <= now)
			{
				r.active = false;
				continue;
			}

			if (!found || r.priority > best_priority)
			{
				found = true;
				best_priority = r.priority;
				m_target = r.target;
			}
		}

		return found;
	}

	bool rotation_manager_t::update(const rotation_t& player_rotation,
	                                const rotation_t& server_rotation,
	                                rotation_t& out)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		processors.tick();

		m_have_target = pick_target();

		if (m_have_target)
		{
			m_previous_target = m_target;
			m_have_previous_target = true;
		}

		// Nothing live and nothing to walk back from: the client is vanilla.
		if (!m_have_target && !m_have_previous_target)
		{
			m_has_rotation = false;
			m_resetting = false;
			return false;
		}

		// A request that has expired leaves its settings behind so the walk
		// back to the real rotation is as smooth as the walk out was.
		m_resetting = !m_have_target;
		const rotation_target_t& active = m_have_target ? m_target : m_previous_target;

		// Step from where we are. On the first tick of an override there is no
		// previous silent rotation, so start from the player's real one --
		// otherwise the first step would come from a stale value and jump.
		const bool fresh = !m_has_rotation;
		rotation_t from = fresh ? player_rotation : m_current;

		// THE RUNAWAY LIVES HERE.
		//
		// Each tick the step added is wrap(goal - from), which is by
		// construction the short way round in *direction* but is accumulated
		// into `from` as a raw value. So whenever the requested bearing winds
		// -- an opponent circling at melee range, or the player's own movement
		// being corrected along the silent yaw -- the trajectory winds with it
		// and nothing ever takes those turns back out. Measured in game: 78
		// degrees of direction change cost 798 degrees of value, and the yaw
		// handed to setYaw() and to the look packet reached -938 while the
		// player was looking at 7.
		//
		// Note this is the ONLY place a fix can bite. Re-expressing the *goal*
		// below cannot change anything at all -- see fixed_yaw's header comment
		// -- which is why the two previous attempts at this had no effect.
		//
		// unwind_yaw only removes whole revolutions, and only past a full turn,
		// so the tick-to-tick output stays continuous and the ordinary case is
		// untouched. `previous` is carried with it so the history the
		// processors difference stays in one frame.
		if (!fresh)
		{
			const float shift = unwind_yaw(from.yaw, player_rotation.yaw) - from.yaw;
			from.yaw       += shift;
			m_previous.yaw += shift;
		}

		// On a fresh acquisition there is no history. Seeding `previous` with
		// the starting point stops the processors reading a value left over
		// from the previous engagement -- in acceleration mode that stale
		// entry is interpreted as momentum and injects a phantom step of
		// however far the player turned in between.
		if (fresh)
			m_previous = from;

		// Cosmetic only, and kept solely so `anch` in the diagnostic reads as a
		// value near `from` instead of near zero. It does NOT affect the step:
		// the smoothing chain reaches the goal through wrapped deltas, so the
		// winding this puts on it is discarded again immediately. Do not
		// "fix" the runaway here -- it has already been tried twice and cannot
		// work. See fixed_yaw's header comment and the unwind above.
		rotation_t goal = m_resetting ? player_rotation : active.rotation;
		m_debug.requested = goal.yaw;
		goal.yaw = fixed_yaw(goal.yaw, from.yaw);
		m_debug.anchored = goal.yaw;
		m_debug.from = from.yaw;
		m_debug.mode = static_cast<int>(processors.smooth.mode);

		rotation_t next = processors.process(from, goal, m_previous, server_rotation, m_resetting);

		// Pitch past straight up or down is impossible for a real client and is
		// checked for directly. Yaw is deliberately NOT wrapped here: Minecraft
		// lets its own yaw accumulate past 360, so wrapping would put a
		// 360-degree step in the look packet stream every time the aim crossed
		// due south. The unwind above is what keeps it bounded instead.
		next.pitch = std::clamp(next.pitch, -90.0f, 90.0f);
		m_debug.next = next.yaw;

		if (m_resetting && next.approximately_equals(player_rotation, active.reset_threshold))
		{
			// Close enough that dropping the override is invisible. Releasing
			// is the whole point of the walk-back: an override that never ends
			// is a permanently desynced player.
			m_has_rotation = false;
			m_have_previous_target = false;
			m_resetting = false;
			m_current = rotation_t();
			m_previous = rotation_t();
			return false;
		}

		// `from` already is (m_has_rotation ? m_current : player_rotation), but
		// carrying any unwind applied above -- assigning m_current here instead
		// would put the turns straight back into the history.
		m_previous = from;
		m_current = next;
		m_has_rotation = true;

		out = next;
		return true;
	}

	movement_correction_t rotation_manager_t::active_movement_correction() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_have_target)
			return m_target.movement_correction;
		if (m_have_previous_target)
			return m_previous_target.movement_correction;
		return movement_correction_t::off;
	}

	void rotation_manager_t::reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& r : m_requests)
			r.active = false;

		m_have_target = false;
		m_have_previous_target = false;
		m_has_rotation = false;
		m_resetting = false;
		m_current = rotation_t();
		m_previous = rotation_t();
	}
}
