#define NOMINMAX
#include "aim_point.h"
#include "../../enhance.h"
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>
#include <sdk/mappings/mappings.hpp>
#include <cmath>
#include <algorithm>
#include <limits>

namespace enhance::modules::killaura
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

void displacement_to_angles(double dx, double dy, double dz, float& out_yaw, float& out_pitch)
{
	const double horiz = std::sqrt(dx * dx + dz * dz);
	if (horiz < 1e-6)
	{
		out_yaw = 0.0f;
		out_pitch = (dy > 0.0) ? -90.0f : 90.0f;
		return;
	}
	// Minecraft yaw: 0 = +Z, 90 = -X (i.e. looking south at 0, east at -90).
	out_yaw   = static_cast<float>(std::atan2(-dx, dz) * kRad2Deg);
	out_pitch = static_cast<float>(-std::atan2(dy, horiz) * kRad2Deg);
	if (out_pitch >  90.0f) out_pitch =  90.0f;
	if (out_pitch < -90.0f) out_pitch = -90.0f;
}

// Standard normal, Marsaglia polar. Used to live in rotation_profile.h, which
// went with the RotationController; kept here because the jitter parameters
// below are still part of this function's contract.
static double gaussian01()
{
	static uint64_t state = 0x9e3779b97f4a7c15ULL;
	auto next01 = []() -> double {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return static_cast<double>((state * 0x2545f4914f6cdd1dULL) >> 11) *
		       (1.0 / 9007199254740992.0);
	};

	double u, v, s;
	do
	{
		u = next01() * 2.0 - 1.0;
		v = next01() * 2.0 - 1.0;
		s = u * u + v * v;
	} while (s >= 1.0 || s == 0.0);

	return u * std::sqrt(-2.0 * std::log(s) / s);
}

static double velocity_magnitude(jobject local_player)
{
	if (!local_player) return 0.0;
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return 0.0;
	sdk::entity_client e(local_player);
	jobject vel = e.get_velocity();
	if (!vel) return 0.0;

	jclass vec_cls = env->GetObjectClass(vel);
	if (!vec_cls) { env->DeleteLocalRef(vel); return 0.0; }
	jfieldID fx = env->GetFieldID(vec_cls, sdk::mappings::vec3d_x_name, sdk::mappings::vec3d_x_sig);
	jfieldID fy = env->GetFieldID(vec_cls, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
	jfieldID fz = env->GetFieldID(vec_cls, sdk::mappings::vec3d_z_name, sdk::mappings::vec3d_z_sig);
	double vx = 0, vy = 0, vz = 0;
	if (fx) vx = env->GetDoubleField(vel, fx);
	if (fy) vy = env->GetDoubleField(vel, fy);
	if (fz) vz = env->GetDoubleField(vel, fz);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(vec_cls);
	env->DeleteLocalRef(vel);
	return std::sqrt(vx * vx + vy * vy + vz * vz);
}

AimPoint compute_aim_point(jobject target,
                            jobject local_player,
                            float max_reach,
                            float current_yaw,
                            float current_pitch,
                            bool ignore_walls,
                            float jitter_x,
                            float jitter_y,
                            float jitter_z)
{
	AimPoint out{ 0, 0, 0, {0, 0, 0}, {0, 0, 0}, false };
	if (!target || !local_player) return out;

	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return out;

	sdk::entity_client te(target);
	jobject box = te.get_bounding_box();
	if (!box) return out;

	sdk::box_client bc(box);
	const double min_x = bc.get_min_x();
	const double max_x = bc.get_max_x();
	const double min_y = bc.get_min_y();
	const double max_y = bc.get_max_y();
	const double min_z = bc.get_min_z();
	const double max_z = bc.get_max_z();
	env->DeleteLocalRef(box);

	sdk::entity_client le(local_player);
	const double ex = le.get_x();
	const double ey = le.get_y() + 1.62;
	const double ez = le.get_z();

	const double center_x = (min_x + max_x) * 0.5;
	const double center_z = (min_z + max_z) * 0.5;
	const double step = (max_y - min_y) / 10.0;

	float best_score = std::numeric_limits<float>::max();
	double best_x = center_x, best_y = (min_y + max_y) * 0.5, best_z = center_z;
	bool found = false;

	for (int i = 0; i <= 10; ++i)
	{
		const double py = min_y + step * i;
		const double dx = center_x - ex;
		const double dy = py - ey;
		const double dz = center_z - ez;
		const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (dist > max_reach) continue;

		// LOS check: same conservative approach as target_selector. Real
		// per-Y wall raycast goes here once we have world.method_8320
		// wrapped in SDK.
		if (!ignore_walls)
		{
			// Placeholder — accept all points.
		}

		float py_yaw, py_pitch;
		displacement_to_angles(dx, dy, dz, py_yaw, py_pitch);
		const float dyaw   = std::abs(py_yaw - current_yaw);
		const float dpitch = std::abs(py_pitch - current_pitch);
		const float yaw_norm = (dyaw > 180.0f) ? 360.0f - dyaw : dyaw;
		const float score = yaw_norm + dpitch;
		if (score < best_score)
		{
			best_score = score;
			best_x = center_x; best_y = py; best_z = center_z;
			found = true;
		}
	}

	if (!found) return out;

	// Velocity-scaled gaussian jitter.
	//
	// Killaura now passes zero here: jitter moved onto the ROTATION, where the
	// aiming processors apply it. Shaking the aim POINT and then smoothing the
	// rotation toward it largely averages the shake away, which is why it
	// never did much. The parameters stay for any caller that still wants it.
	const double vmag = velocity_magnitude(local_player);
	if (vmag > 1e-3 && (jitter_x > 0 || jitter_y > 0 || jitter_z > 0))
	{
		best_x += gaussian01() * jitter_x * vmag;
		best_y += gaussian01() * jitter_y * vmag;
		best_z += gaussian01() * jitter_z * vmag;
		// Keep the point inside the hitbox so we don't aim into empty air.
		best_x = std::clamp(best_x, min_x + 0.05, max_x - 0.05);
		best_y = std::clamp(best_y, min_y + 0.05, max_y - 0.05);
		best_z = std::clamp(best_z, min_z + 0.05, max_z - 0.05);
	}

	out.x = best_x; out.y = best_y; out.z = best_z;
	out.box_min[0] = min_x; out.box_min[1] = min_y; out.box_min[2] = min_z;
	out.box_max[0] = max_x; out.box_max[1] = max_y; out.box_max[2] = max_z;
	out.valid = true;
	return out;
}

}
