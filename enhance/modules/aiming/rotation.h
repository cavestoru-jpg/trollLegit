#pragma once

// Rotation maths, ported from LiquidBounce's utils/aiming/data/Rotation.kt and
// utils/aiming/utils/RotationUtil.kt.
//
// Deliberately free of JNI: everything here is a pure function of its inputs so
// it can be reasoned about (and fixed) without launching the game. Anything
// needing the JVM -- the player's real angles, the sensitivity setting -- is
// passed in by the caller.

namespace enhance::modules::aiming
{
	// Signed yaw/pitch difference between two rotations, each already wrapped
	// into [-180, 180].
	struct rotation_delta_t
	{
		float yaw   = 0.0f;
		float pitch = 0.0f;

		float length() const;
	};

	struct rotation_t
	{
		float yaw   = 0.0f;
		float pitch = 0.0f;

		// True once the angles have been quantised to the mouse grid by
		// normalize(). Rotations we send to the server must have this set;
		// the flag exists so normalising twice is a no-op rather than a
		// second dose of dither noise.
		bool normalized = false;

		rotation_t() = default;
		rotation_t(float yaw_, float pitch_, bool normalized_ = false)
			: yaw(yaw_), pitch(pitch_), normalized(normalized_) {}

		rotation_delta_t delta_to(const rotation_t& other) const;

		// Angular distance, capped at 180 degrees.
		float angle_to(const rotation_t& other) const;

		bool approximately_equals(const rotation_t& other, float tolerance = 2.0f) const;

		// Step toward `other`, capping yaw and pitch independently but scaling
		// each cap by that axis's share of the total movement, so the path is a
		// straight line rather than an L-shape. LiquidBounce's towardsLinear.
		rotation_t towards_linear(const rotation_t& other,
		                          float horizontal_factor,
		                          float vertical_factor) const;

		// Plain linear interpolation, no per-axis capping.
		rotation_t interpolate_to(const rotation_t& other, float factor) const;
	};

	// Minecraft's Mth.wrapDegrees: fold into [-180, 180].
	float wrap_degrees(float degrees);

	// Signed difference a - b, wrapped. LiquidBounce's angleDifference.
	float angle_difference(float a, float b);

	// Re-express `yaw` in the same winding as `reference`.
	//
	// Minecraft never normalises the player's yaw: stand still and turn in one
	// direction and it climbs past 360, 720 and on. A rotation computed from a
	// direction vector always comes back in [-180, 180], so writing it to the
	// player straight puts the two hundreds of degrees apart even when they
	// point the same way.
	//
	// That matters because it is the *difference between consecutive look
	// packets* a server measures. An unwound value produces apparent jumps of
	// hundreds of degrees per tick -- not a movement a hand can make, and the
	// rotation gets thrown out. Symptom: the aim looks right on the client and
	// the server behaves as though it never arrived.
	//
	// Returns the value pointing exactly where `yaw` points, expressed within
	// 180 degrees of `reference`.
	//
	// IMPORTANT, and the reason two attempts at "the winding is mixed up"
	// changed nothing: calling this on a *goal* has no effect whatsoever.
	// Everything that consumes a goal -- delta_to, angle_difference,
	// towards_linear, every smoothing mode -- works on wrapped deltas, so it
	// throws the goal's winding away before using it. Verified by feeding the
	// same direction in seven different windings and getting bit-identical
	// output. Only re-expressing something that is *accumulated across ticks*
	// (the silent rotation itself) can change anything.
	float fixed_yaw(float yaw, float reference);

	// Take whole revolutions out of `yaw` once it has drifted a full turn or
	// more from `reference`. Below that it is returned untouched.
	//
	// This is deliberately not fixed_yaw. fixed_yaw re-expresses
	// unconditionally, so the value it returns flips by a whole turn the moment
	// the offset crosses 180 degrees -- apply that to the silent rotation every
	// tick and consecutive look packets differ by 360. That is the measured
	// "same aim alternating between -385 and -25".
	//
	// Acting only past a full turn, and landing back inside half a turn, means
	// another 180 degrees of relative motion is needed before it can act again,
	// so it cannot alternate; and in ordinary use -- the aim less than a
	// revolution away from the player's view -- it never fires at all.
	float unwind_yaw(float yaw, float reference);

	// The smallest rotation step a real mouse can produce at this sensitivity.
	// Minecraft turns a raw mouse delta into degrees by multiplying an integer
	// count by this value, so every legitimate rotation a player sends is a
	// multiple of it. `sensitivity` is the raw 0..2 slider value.
	//
	//     f = sensitivity * 0.6 + 0.2 ;  gcd = f * f * f * 8.0 * 0.15
	double mouse_gcd(float sensitivity);

	// Build the rotation that looks along a vector.
	rotation_t rotation_from_vector(double dx, double dy, double dz);

	// Build the rotation that looks from `from` at `point`.
	rotation_t looking_at(double point_x, double point_y, double point_z,
	                      double from_x, double from_y, double from_z);

	// Snap `target` onto the mouse grid relative to `current`, then jitter each
	// axis by a fraction of one grid step.
	//
	// This is the single most important function here. An anti-cheat can divide
	// the angle change between two look packets by the sensitivity step; a
	// human always yields close to a whole number, and a bot that computes an
	// exact angle to a hitbox does not. Quantising fixes that, but a perfectly
	// quantised stream is *itself* a signature -- real mice accumulate
	// sub-step remainders -- hence the dither.
	//
	// `random01` must return a uniform value in [0, 1). It is a parameter
	// rather than a call to rand() so this stays a pure function and the
	// dither can be pinned in a test.
	rotation_t normalize(const rotation_t& target,
	                     const rotation_t& current,
	                     float sensitivity,
	                     double (*random01)());
}
