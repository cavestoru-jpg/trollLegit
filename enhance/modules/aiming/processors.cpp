#include "processors.h"

#include <cmath>
#include <algorithm>

namespace enhance::modules::aiming
{
	// ---------------------------------------------------------------- rng

	void rng_t::seed(uint64_t s)
	{
		// Zero is a fixed point for xorshift, so fold it away.
		state = s ? s : 0x853c49e6748fea9bULL;
		m_has_spare = false;
	}

	uint64_t rng_t::next_u64()
	{
		// xorshift64*, plenty for jitter and cheap enough to call several
		// times per tick.
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return state * 0x2545f4914f6cdd1dULL;
	}

	double rng_t::next01()
	{
		// Top 53 bits — the mantissa width of a double, so every representable
		// value in [0,1) is reachable and none is favoured.
		return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
	}

	bool rng_t::next_bool()
	{
		return (next_u64() >> 63) != 0;
	}

	float rng_t::range(float lo, float hi)
	{
		if (hi <= lo) return lo;
		return lo + static_cast<float>(next01()) * (hi - lo);
	}

	int rng_t::range_i(int lo, int hi)
	{
		if (hi <= lo) return lo;
		const uint64_t span = static_cast<uint64_t>(hi - lo) + 1;
		return lo + static_cast<int>(next_u64() % span);
	}

	double rng_t::next_gaussian()
	{
		// Marsaglia polar method. Produces two normals at a time; keeping the
		// spare halves the cost across calls.
		if (m_has_spare)
		{
			m_has_spare = false;
			return m_spare;
		}

		double u, v, s;
		do
		{
			u = next01() * 2.0 - 1.0;
			v = next01() * 2.0 - 1.0;
			s = u * u + v * v;
		} while (s >= 1.0 || s == 0.0);

		const double scale = std::sqrt(-2.0 * std::log(s) / s);
		m_spare = v * scale;
		m_has_spare = true;
		return u * scale;
	}

	// --------------------------------------------------------------- fail

	void fail_processor_t::tick(rng_t& rng)
	{
		if (!enabled)
		{
			// Leave the state consistent so re-enabling mid-fight doesn't
			// resume a half-finished fail.
			m_ticks_elapsed = m_current_duration;
			m_shift = rotation_t();
			return;
		}

		if (static_cast<float>(rate) > rng.range(0.0f, 100.0f))
		{
			m_current_duration = rng.range_i(transition_duration);
			m_shift = rotation_t(rng.with_random_sign(rng.range(strength_horizontal)),
			                     rng.with_random_sign(rng.range(strength_vertical)));
			m_ticks_elapsed = 0;
		}


		// Unconditional: an in-progress fail has to age even on ticks where the
		// trigger roll did not fire, or its duration is measured in "ticks the
		// dice missed" rather than ticks.
		++m_ticks_elapsed;
	}

	bool fail_processor_t::in_fail_state() const
	{
		return enabled && m_ticks_elapsed < m_current_duration;
	}

	rotation_t fail_processor_t::process(const rotation_t& target,
	                                     const rotation_t& previous,
	                                     const rotation_t& server) const
	{
		if (!in_fail_state())
			return target;

		// Drag back toward where we were a moment ago, on top of the fixed
		// shift. The result is an overshoot that decays instead of a jump.
		// Wrapped: both operands are unwrapped absolute angles, and after any
		// release/re-acquire they can sit a full turn apart. Subtracting them
		// raw then yields ~360 and injects a large bogus pull.
		const float delta_yaw   = angle_difference(previous.yaw,   server.yaw)   * factor;
		const float delta_pitch = angle_difference(previous.pitch, server.pitch) * factor;

		return rotation_t(target.yaw   + delta_yaw   + m_shift.yaw,
		                  target.pitch + delta_pitch + m_shift.pitch);
	}

	// ------------------------------------------------------------- jitter

	void jitter_processor_t::tick(rng_t& rng)
	{
		if (enabled && burst_enabled)
		{
			if (m_burst_ticks > 0)
			{
				--m_burst_ticks;
				if (m_burst_ticks == 0)
				{
					m_burst_yaw = 0.0f;
					m_burst_pitch = 0.0f;
				}
			}
			else if (static_cast<float>(burst_rate) > rng.range(0.0f, 100.0f))
			{
				m_burst_ticks = rng.range_i(burst_duration);
				m_burst_yaw   = rng.with_random_sign(rng.range(burst_yaw));
				m_burst_pitch = rng.with_random_sign(rng.range(burst_pitch));
			}
		}
		else
		{
			m_burst_ticks = 0;
			m_burst_yaw = 0.0f;
			m_burst_pitch = 0.0f;
		}

		if (enabled && drift_enabled)
		{
			// Mean-reverting random walk: each tick decays toward zero and
			// takes a gaussian step, so it wanders without ever running away.
			m_drift_yaw = std::clamp(
				(1.0f - drift_mean_reversion) * m_drift_yaw +
					static_cast<float>(rng.next_gaussian()) * drift_step_strength,
				-drift_max_yaw, drift_max_yaw);
			m_drift_pitch = std::clamp(
				(1.0f - drift_mean_reversion) * m_drift_pitch +
					static_cast<float>(rng.next_gaussian()) * drift_step_strength,
				-drift_max_pitch, drift_max_pitch);
		}
		else
		{
			m_drift_yaw = 0.0f;
			m_drift_pitch = 0.0f;
		}
	}

	rotation_t jitter_processor_t::process(const rotation_t& target, rng_t& rng) const
	{
		if (!enabled)
			return target;

		float dy = 0.0f;
		float dp = 0.0f;

		if (micro_enabled)
		{
			dy += rng.with_random_sign(rng.range(micro_yaw));
			dp += rng.with_random_sign(rng.range(micro_pitch));
		}

		dy += m_burst_yaw + m_drift_yaw;
		dp += m_burst_pitch + m_drift_pitch;

		return rotation_t(target.yaw + dy, target.pitch + dp);
	}

	// --------------------------------------------------------- short stop

	rotation_t short_stop_processor_t::process(const rotation_t& current,
	                                           const rotation_t& target,
	                                           rng_t& rng)
	{
		if (!enabled)
			return target;

		if (static_cast<float>(rate) > rng.range(0.0f, 100.0f))
		{
			m_current_duration = rng.range_i(stop_duration);
			m_ticks_elapsed = 0;
		}

		if (m_ticks_elapsed < m_current_duration)
		{
			++m_ticks_elapsed;
			// Not a freeze — a very slow step. A rotation that stops dead and
			// restarts is as unnatural as one that never stops.
			return current.towards_linear(target, rng.range(0.0f, 0.1f), rng.range(0.0f, 0.1f));
		}

		return target;
	}

	// ------------------------------------------------------ angle smooth

	namespace
	{
		// Shared by Sigmoid and Acceleration's deceleration. 120 degrees is
		// LiquidBounce's normalisation constant, not a derived quantity.
		float sigmoid_curve(float rotation_difference, float steepness, float midpoint)
		{
			const double scaled = static_cast<double>(rotation_difference) / 120.0;
			const double s = 1.0 / (1.0 + std::exp(-steepness * (scaled - midpoint)));
			return static_cast<float>(s);
		}

		float normalize_direction_change(float angle)
		{
			return std::clamp(angle / 180.0f, 0.0f, 1.0f);
		}
	}

	rotation_t angle_smooth_t::process(const rotation_t& current,
	                                   const rotation_t& target,
	                                   const rotation_t& previous,
	                                   rng_t& rng) const
	{
		switch (mode)
		{
			case smooth_mode_t::sigmoid:
			{
				// Speed follows an S-curve in the remaining distance: creeps
				// while far away and while nearly there, quick in between.
				const float diff = current.angle_to(target);
				const float s = sigmoid_curve(diff, sigmoid_steepness, sigmoid_midpoint);

				return current.towards_linear(
					target,
					std::clamp(s * rng.range(horizontal), 0.0f, 180.0f),
					std::clamp(s * rng.range(vertical),   0.0f, 180.0f));
			}

			case smooth_mode_t::interpolation:
			{
				const rotation_delta_t d = current.delta_to(target);

				// A target that jumped since last tick gets extra speed, so
				// switching victims does not crawl.
				const float direction_change =
					normalize_direction_change(previous.angle_to(target)) *
					(static_cast<float>(rng.range_i(interp_direction_change)) / 100.0f);

				const float h_speed = static_cast<float>(rng.range_i(interp_horizontal)) / 100.0f;
				const float v_speed = static_cast<float>(rng.range_i(interp_vertical))   / 100.0f;

				// Bezier far from the target, sigmoid close to it. The two
				// curves meet at interp_midpoint.
				auto factor = [&](float difference, float turn_speed) -> float
				{
					const float t = normalize_direction_change(difference);

					if (t > interp_midpoint)
					{
						// Quadratic bezier from 0.05 to 1 with control 1.
						const float u = 1.0f - t;
						const float bez = u * u * 0.05f + 2.0f * u * t * 1.0f + t * t * 1.0f;
						return bez * turn_speed;
					}

					const float sig = 1.0f / (1.0f + std::exp(-0.5f * (t - 0.3f)));
					return sig * std::clamp(turn_speed + direction_change, 0.0f, 1.0f);
				};

				// Multiplied by the distance, which turns the fraction into
				// degrees and bypasses towards_linear's own capping.
				return current.towards_linear(
					target,
					factor(std::fabs(d.yaw),   std::clamp(h_speed, 0.0f, 1.0f)) * std::fabs(d.yaw),
					factor(std::fabs(d.pitch), std::clamp(v_speed, 0.0f, 1.0f)) * std::fabs(d.pitch));
			}

			case smooth_mode_t::acceleration:
			{
				// Steers the change in the step rather than the step itself,
				// so the aim carries momentum and cannot start or stop dead.
				const rotation_delta_t prev_diff = previous.delta_to(current);
				const rotation_delta_t diff      = current.delta_to(target);

				const float decel = sigmoid_deceleration_enabled
					? sigmoid_curve(diff.length(), deceleration_steepness, deceleration_midpoint)
					: 1.0f;

				auto accelerate = [&](float d, float prev_d, const float_range_t& accel) -> float
				{
					const float limit = rng.range(accel);
					// NOT wrapped. Both operands already come from delta_to and
					// are in [-180, 180]; their difference is a rate of change,
					// not an angle. Wrapping it flips the sign on a hard
					// reversal -- +170 following -170 needs +340, and wrapping
					// turns that into -20, accelerating further the wrong way.
					const float raw = d - prev_d;
					return std::clamp(raw, -limit, limit) * decel;
				};

				const float yaw_accel   = accelerate(diff.yaw,   prev_diff.yaw,   yaw_acceleration);
				const float pitch_accel = accelerate(diff.pitch, prev_diff.pitch, pitch_acceleration);

				auto error = [&](float accel, float accel_err, float const_err) -> float
				{
					float e = 0.0f;
					if (acceleration_error_enabled)
						e += accel * rng.range(-accel_err, accel_err);
					if (constant_error_enabled)
						e += rng.range(-const_err, const_err);
					return e;
				};

				return rotation_t(
					current.yaw + prev_diff.yaw + yaw_accel +
						error(yaw_accel, yaw_acceleration_error, yaw_constant_error),
					current.pitch + prev_diff.pitch + pitch_accel +
						error(pitch_accel, pitch_acceleration_error, pitch_constant_error));
			}

			case smooth_mode_t::linear:
			default:
				return current.towards_linear(target, rng.range(horizontal), rng.range(vertical));
		}
	}

	int angle_smooth_t::ticks_to_cover(const rotation_t& from, const rotation_t& to) const
	{
		const rotation_delta_t d = from.delta_to(to);

		// Deliberately crude, and deliberately pessimistic: the slowest end of
		// the speed range, ignoring the curves. A caller that attacks before
		// the turn has landed is worse off than one that waits a tick too
		// long, so this must never come out low.
		const float h = horizontal.min > 0.0f ? horizontal.min : 1.0f;
		const float v = vertical.min   > 0.0f ? vertical.min   : 1.0f;

		const float ticks_yaw   = std::fabs(d.yaw)   / h;
		const float ticks_pitch = std::fabs(d.pitch) / v;

		return static_cast<int>(std::ceil((std::max)(ticks_yaw, ticks_pitch)));
	}

	// -------------------------------------------------------------- chain

	void processor_chain_t::tick()
	{
		fail.tick(rng);
		jitter.tick(rng);
	}

	rotation_t processor_chain_t::process(const rotation_t& current,
	                                      const rotation_t& target,
	                                      const rotation_t& previous,
	                                      const rotation_t& server,
	                                      bool resetting)
	{
		// Step toward the goal first; everything after perturbs this step.
		rotation_t r = smooth.process(current, target, previous, rng);

		// On the way back to the real rotation the humanizers are skipped --
		// see the header. They would keep the residual from ever hitting zero.
		if (resetting)
			return r;

		r = fail.process(r, previous, server);
		r = short_stop.process(current, r, rng);
		r = jitter.process(r, rng);
		return r;
	}
}
