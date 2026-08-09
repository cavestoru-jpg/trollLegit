#pragma once

#include "rotation.h"
#include <cstdint>

// Rotation processors, ported from LiquidBounce's
// utils/aiming/features/processors/.
//
// A processor takes the rotation we *want* and returns the rotation we will
// actually send, adding the small imperfections that separate a human from a
// solver. They run in a chain each tick, each one seeing the previous one's
// output.
//
// Like rotation.h this is JNI-free. State that advances once per game tick
// lives in tick(); the per-use transform lives in process().

namespace enhance::modules::aiming
{
	struct float_range_t { float min = 0.0f; float max = 0.0f; };
	struct int_range_t   { int   min = 0;    int   max = 0;    };

	// Small deterministic PRNG. Explicit rather than rand() so a sequence can
	// be replayed when something looks wrong, and so no processor accidentally
	// shares global state with another thread.
	struct rng_t
	{
		uint64_t state = 0x853c49e6748fea9bULL;

		void     seed(uint64_t s);
		uint64_t next_u64();
		double   next01();          // [0, 1)
		bool     next_bool();
		float    range(float lo, float hi);
		int      range_i(int lo, int hi);   // inclusive
		float    range(const float_range_t& r) { return range(r.min, r.max); }
		int      range_i(const int_range_t& r) { return range_i(r.min, r.max); }
		float    with_random_sign(float v) { return next_bool() ? v : -v; }
		double   next_gaussian();

	private:
		bool   m_has_spare = false;
		double m_spare = 0.0;
	};

	// Occasionally aim slightly wrong on purpose, for a few ticks at a time.
	// Perfect tracking is the easiest thing in the world to spot; this makes
	// the aim overshoot and recover the way a hand does.
	struct fail_processor_t
	{
		bool  enabled = false;
		int   rate = 3;                                   // % chance per tick
		float factor = 0.04f;                             // pull-back strength
		float_range_t strength_horizontal { 5.0f, 10.0f };
		float_range_t strength_vertical   { 0.0f,  2.0f };
		int_range_t   transition_duration { 1, 4 };       // ticks

		void tick(rng_t& rng);
		bool in_fail_state() const;

		// `previous` and `server` come from the rotation manager's history.
		rotation_t process(const rotation_t& target,
		                   const rotation_t& previous,
		                   const rotation_t& server) const;

	private:
		int        m_ticks_elapsed = 0;
		int        m_current_duration = 1;
		rotation_t m_shift;
	};

	// Never hold perfectly still. Three independent sources, because real hand
	// tremor is not one frequency:
	//   micro  - per-tick white noise, always present
	//   burst  - a fixed offset held for a few ticks, fired occasionally
	//   drift  - a slow mean-reverting random walk
	struct jitter_processor_t
	{
		bool enabled = false;

		bool micro_enabled = true;
		float_range_t micro_yaw   { 0.0f, 0.6f };
		float_range_t micro_pitch { 0.0f, 0.3f };

		bool burst_enabled = true;
		int  burst_rate = 25;                        // % chance per tick
		int_range_t   burst_duration { 2, 6 };       // ticks
		float_range_t burst_yaw   { 0.5f, 2.5f };
		float_range_t burst_pitch { 0.0f, 1.0f };

		bool  drift_enabled = true;
		float drift_max_yaw = 2.0f;
		float drift_max_pitch = 1.0f;
		float drift_step_strength = 0.15f;
		float drift_mean_reversion = 0.05f;

		void tick(rng_t& rng);
		rotation_t process(const rotation_t& target, rng_t& rng) const;

	private:
		int   m_burst_ticks = 0;
		float m_burst_yaw = 0.0f;
		float m_burst_pitch = 0.0f;
		float m_drift_yaw = 0.0f;
		float m_drift_pitch = 0.0f;
	};

	// Occasionally stall the turn for a tick or two, as though the player
	// hesitated, then resume. Implemented as a near-zero step rather than a
	// hard freeze so the rotation still creeps forward.
	struct short_stop_processor_t
	{
		bool enabled = false;
		int  rate = 3;                          // % chance per tick
		int_range_t stop_duration { 1, 2 };     // ticks

		rotation_t process(const rotation_t& current,
		                   const rotation_t& target,
		                   rng_t& rng);

	private:
		int m_ticks_elapsed = 0;
		int m_current_duration = 1;
	};

	// LiquidBounce's AngleSmooth modes, minus Multipoint and Clone.
	//
	// Linear, Sigmoid and Interpolation are all "factor" smooths: they compute
	// a per-axis degrees-per-tick cap and hand it to towards_linear. What
	// differs is how the cap responds to how far there is left to go.
	// Acceleration works differently -- it steers the *change* in the step
	// rather than the step, so the aim has momentum.
	enum class smooth_mode_t
	{
		linear = 0,        // constant speed
		sigmoid,           // slow at the ends, fast in the middle
		interpolation,     // sigmoid near the target, bezier far from it
		acceleration,      // momentum, with error injected per tick
	};

	// Head of the chain, and the only non-optional processor: with no smoother
	// the chain returns the target unchanged and the aim teleports.
	struct angle_smooth_t
	{
		smooth_mode_t mode = smooth_mode_t::linear;

		// -- linear, and the speed ceiling for sigmoid ---------------------
		// Ranges rather than constants so successive turns differ; a fixed
		// turn rate is its own signature.
		float_range_t horizontal { 180.0f, 180.0f };
		float_range_t vertical   { 180.0f, 180.0f };

		// -- sigmoid -------------------------------------------------------
		float sigmoid_steepness = 10.0f;
		float sigmoid_midpoint  = 0.3f;

		// -- interpolation -------------------------------------------------
		// Percentages of the remaining distance, not degrees.
		int_range_t interp_horizontal { 80, 85 };
		int_range_t interp_vertical   { 20, 25 };
		// Extra speed when the target moved a lot since last tick.
		int_range_t interp_direction_change { 95, 100 };
		float interp_midpoint = 0.35f;

		// -- acceleration --------------------------------------------------
		float_range_t yaw_acceleration   { 20.0f, 25.0f };
		float_range_t pitch_acceleration { 20.0f, 25.0f };

		// Error proportional to the acceleration, and a constant floor. Both
		// on by default in LiquidBounce: an acceleration curve with no noise
		// is smoother than a hand can be.
		bool  acceleration_error_enabled = true;
		float yaw_acceleration_error   = 0.1f;
		float pitch_acceleration_error = 0.1f;

		bool  constant_error_enabled = true;
		float yaw_constant_error   = 0.1f;
		float pitch_constant_error = 0.1f;

		// Ease off as the target is approached instead of arriving at speed.
		bool  sigmoid_deceleration_enabled = false;
		float deceleration_steepness = 10.0f;
		float deceleration_midpoint  = 0.3f;

		// `previous` is the rotation held one tick ago -- acceleration needs
		// it to know the step it is accelerating from.
		rotation_t process(const rotation_t& current,
		                   const rotation_t& target,
		                   const rotation_t& previous,
		                   rng_t& rng) const;

		// Over-estimate of the ticks needed to cover `from` -> `to`. Callers
		// time attacks with it, so it must never be optimistic.
		int ticks_to_cover(const rotation_t& from, const rotation_t& to) const;
	};

	// The chain, in LiquidBounce's order: smooth, then fail, then short stop,
	// then jitter. Jitter is deliberately last -- it is the one perturbation
	// that should always survive to the output.
	//
	// Every processor is handed the SAME `current` and the running `target`.
	// The smoother turns `target` into the *next* rotation (one step along the
	// way); the humanizers then perturb that step. So their offsets are
	// absolute degrees on this tick's output, not on the eventual goal.
	struct processor_chain_t
	{
		angle_smooth_t         smooth;
		fail_processor_t       fail;
		short_stop_processor_t short_stop;
		jitter_processor_t     jitter;
		rng_t                  rng;

		void tick();

		// `resetting` means we are walking back to the player's real rotation
		// after the target went away. Only the smoother runs then, and this is
		// not a detail: the humanizers would wobble around the real rotation
		// forever, the residual would never reach exactly zero, and the silent
		// rotation could never be released.
		rotation_t process(const rotation_t& current,
		                   const rotation_t& target,
		                   const rotation_t& previous,
		                   const rotation_t& server,
		                   bool resetting);
	};
}
