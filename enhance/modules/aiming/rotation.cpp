#include "rotation.h"

#include <cmath>
#include <algorithm>

namespace
{
	constexpr double k_rad_to_deg = 57.29577951308232;  // 180 / pi
}

namespace enhance::modules::aiming
{
	float rotation_delta_t::length() const
	{
		return std::hypot(yaw, pitch);
	}

	float wrap_degrees(float degrees)
	{
		// Mirrors Mth.wrapDegrees. fmod already gives (-360, 360), so a single
		// correction on each side is enough.
		float d = std::fmod(degrees, 360.0f);
		if (d >= 180.0f)  d -= 360.0f;
		if (d < -180.0f)  d += 360.0f;
		return d;
	}

	float angle_difference(float a, float b)
	{
		return wrap_degrees(a - b);
	}

	float fixed_yaw(float yaw, float reference)
	{
		// reference + wrap(yaw - reference).
		//
		// Take the short-way offset from the reference to the target and add it
		// back to the reference. The result points exactly where `yaw` points
		// and sits within 180 degrees of `reference`.
		//
		// The operand order matters and is easy to get backwards: yaw +
		// wrap(reference - yaw) looks symmetric but is not -- it shifts the
		// direction itself rather than re-expressing it, so applying it every
		// tick makes the rotation oscillate instead of converge.
		return reference + angle_difference(yaw, reference);
	}

	float unwind_yaw(float yaw, float reference)
	{
		// Dead band of a full turn, so this never fires during ordinary aiming
		// and cannot alternate when it does. See the header for why plain
		// fixed_yaw() is the wrong tool here.
		const float offset = yaw - reference;
		if (offset >= 360.0f || offset <= -360.0f)
			return fixed_yaw(yaw, reference);
		return yaw;
	}

	double mouse_gcd(float sensitivity)
	{
		const double f = static_cast<double>(sensitivity) * 0.6 + 0.2;
		return f * f * f * 8.0 * 0.15;
	}

	rotation_delta_t rotation_t::delta_to(const rotation_t& other) const
	{
		rotation_delta_t d;
		d.yaw   = angle_difference(other.yaw, yaw);
		d.pitch = angle_difference(other.pitch, pitch);
		return d;
	}

	float rotation_t::angle_to(const rotation_t& other) const
	{
		return std::min(delta_to(other).length(), 180.0f);
	}

	bool rotation_t::approximately_equals(const rotation_t& other, float tolerance) const
	{
		return angle_to(other) <= tolerance;
	}

	rotation_t rotation_t::towards_linear(const rotation_t& other,
	                                      float horizontal_factor,
	                                      float vertical_factor) const
	{
		const rotation_delta_t diff = delta_to(other);
		const float total = diff.length();

		// Already there. Guarding this also keeps the division below safe.
		if (total <= 0.0f)
			return rotation_t(yaw, pitch);

		// Each axis gets a cap proportional to its share of the movement, so
		// both axes finish together and the crosshair travels in a straight
		// line instead of turning then tilting.
		const float cap_yaw   = std::fabs(diff.yaw   / total) * horizontal_factor;
		const float cap_pitch = std::fabs(diff.pitch / total) * vertical_factor;

		return rotation_t(
			yaw   + std::clamp(diff.yaw,   -cap_yaw,   cap_yaw),
			pitch + std::clamp(diff.pitch, -cap_pitch, cap_pitch));
	}

	rotation_t rotation_t::interpolate_to(const rotation_t& other, float factor) const
	{
		return rotation_t(
			yaw   + factor * (other.yaw   - yaw),
			pitch + factor * (other.pitch - pitch));
	}

	rotation_t rotation_from_vector(double dx, double dy, double dz)
	{
		const double yaw = std::atan2(dz, dx) * k_rad_to_deg - 90.0;
		const double pitch = -std::atan2(dy, std::hypot(dx, dz)) * k_rad_to_deg;

		return rotation_t(
			wrap_degrees(static_cast<float>(yaw)),
			wrap_degrees(static_cast<float>(pitch)));
	}

	rotation_t looking_at(double point_x, double point_y, double point_z,
	                      double from_x, double from_y, double from_z)
	{
		return rotation_from_vector(point_x - from_x, point_y - from_y, point_z - from_z);
	}

	namespace
	{
		// Offset one quantised step by a fraction of a step, so the stream of
		// deltas does not consist of exact multiples. Amplitude is 5%-40% of a
		// step; the difference of two uniforms gives a symmetric, zero-mean
		// distribution rather than a flat one.
		double dither(double quantized_delta, double gcd, double (*random01)())
		{
			if (quantized_delta == 0.0)
				return 0.0;

			const double amplitude = gcd * (0.05 + 0.35 * random01());
			const double noise = (random01() - random01()) * amplitude;
			return quantized_delta + noise;
		}
	}

	rotation_t normalize(const rotation_t& target,
	                     const rotation_t& current,
	                     float sensitivity,
	                     double (*random01)())
	{
		if (target.normalized || !random01)
			return target;

		const double gcd = mouse_gcd(sensitivity);
		if (gcd <= 0.0)
			return rotation_t(target.yaw, std::clamp(target.pitch, -90.0f, 90.0f), true);

		const rotation_delta_t diff = current.delta_to(target);

		// Round the *delta* to whole mouse steps, not the absolute angle: what
		// the server can measure is the change between two look packets.
		const double q_yaw   = std::round(static_cast<double>(diff.yaw)   / gcd) * gcd;
		const double q_pitch = std::round(static_cast<double>(diff.pitch) / gcd) * gcd;

		const float yaw   = current.yaw   + static_cast<float>(dither(q_yaw,   gcd, random01));
		const float pitch = current.pitch + static_cast<float>(dither(q_pitch, gcd, random01));

		// Pitch past straight up or down is impossible for a real client and is
		// checked for directly. Yaw is left unwrapped on purpose -- Minecraft
		// itself lets it accumulate past 360.
		return rotation_t(yaw, std::clamp(pitch, -90.0f, 90.0f), true);
	}
}
