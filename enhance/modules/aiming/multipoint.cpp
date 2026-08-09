#include "multipoint.h"
#include "rotation.h"
#include "../killaura/aim_point.h"

#include "../../enhance.h"

#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	struct box_t
	{
		double min[3];
		double max[3];
	};

	// Where the segment eyes->target first enters the box, as a parameter t in
	// [0,1]. Slab method. Returns false when the segment misses.
	//
	// LiquidBounce calls this box.firstHit and uses it for two things: the
	// distance gate is measured to the ENTRY point rather than to the candidate
	// (so a point deep inside a wide hitbox is not rejected for being far), and
	// a candidate whose ray somehow misses the box is dropped.
	bool first_hit(const box_t& b, const double from[3], const double to[3], double& out_t)
	{
		double t_near = 0.0;
		double t_far  = 1.0;

		for (int i = 0; i < 3; ++i)
		{
			const double d = to[i] - from[i];

			if (std::fabs(d) < 1e-9)
			{
				// Parallel to this slab: inside it or the segment misses entirely.
				if (from[i] < b.min[i] || from[i] > b.max[i]) return false;
				continue;
			}

			double t1 = (b.min[i] - from[i]) / d;
			double t2 = (b.max[i] - from[i]) / d;
			if (t1 > t2) std::swap(t1, t2);

			t_near = (std::max)(t_near, t1);
			t_far  = (std::min)(t_far, t2);
			if (t_near > t_far) return false;
		}

		out_t = t_near;
		return true;
	}

	bool contains(const box_t& b, const double p[3])
	{
		return p[0] >= b.min[0] && p[0] <= b.max[0] &&
		       p[1] >= b.min[1] && p[1] <= b.max[1] &&
		       p[2] >= b.min[2] && p[2] <= b.max[2];
	}
}

enhance::modules::aiming::multipoint::result_t
enhance::modules::aiming::multipoint::compute(jobject target,
                                              jobject local_player,
                                              double range,
                                              double walls_range,
                                              float current_yaw,
                                              float current_pitch,
                                              int resolution,
                                              int point_mode,
                                              bool ignore_walls)
{
	result_t out;

	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env || !target || !local_player) return out;

	sdk::entity_client te(target);
	jobject jbox = te.get_bounding_box();
	if (!jbox) return out;

	sdk::box_client bc(jbox);
	box_t box;
	box.min[0] = bc.get_min_x(); box.max[0] = bc.get_max_x();
	box.min[1] = bc.get_min_y(); box.max[1] = bc.get_max_y();
	box.min[2] = bc.get_min_z(); box.max[2] = bc.get_max_z();
	env->DeleteLocalRef(jbox);

	sdk::entity_client le(local_player);
	const double eyes[3] = { le.get_x(), le.get_y() + 1.62, le.get_z() };

	const double range_sq = range * range;
	const double walls_sq = walls_range * walls_range;

	// Best visible and best hidden are tracked separately and the visible one
	// wins outright, however much further it is to turn. Preferring a hidden
	// point merely because it needs a smaller turn is what makes an aim
	// obviously stare through a wall.
	double best_vis_score = (std::numeric_limits<double>::max)();
	double best_hid_score = (std::numeric_limits<double>::max)();
	double best_vis[3] = {0,0,0}, best_hid[3] = {0,0,0};
	double best_vis_dist = 0.0, best_hid_dist = 0.0;
	bool   have_vis = false, have_hid = false;

	auto consider = [&](double px, double py, double pz)
	{
		const double spot[3] = { px, py, pz };

		// Elongate the ray well past the candidate before clipping, so a point
		// sitting exactly on the surface is not lost to floating-point noise.
		const double far_point[3] = {
			eyes[0] + 2.0 * (spot[0] - eyes[0]),
			eyes[1] + 2.0 * (spot[1] - eyes[1]),
			eyes[2] + 2.0 * (spot[2] - eyes[2]),
		};

		double t = 0.0;
		if (!first_hit(box, eyes, far_point, t)) return;

		const double on_box[3] = {
			eyes[0] + (far_point[0] - eyes[0]) * t,
			eyes[1] + (far_point[1] - eyes[1]) * t,
			eyes[2] + (far_point[2] - eyes[2]) * t,
		};

		// A candidate that sits exactly on the eye position -- which happens
		// when the eyes are inside the hitbox, because the nearest point is then
		// the eye itself -- has no direction to look along. Feeding a zero
		// displacement to the angle conversion yields a bearing of zero, which
		// scores like any other candidate and can win, throwing the aim off to
		// due south for no reason.
		{
			const double sx = spot[0] - eyes[0];
			const double sy = spot[1] - eyes[1];
			const double sz = spot[2] - eyes[2];
			if (sx * sx + sy * sy + sz * sz < 1e-12) return;
		}

		const double ddx = on_box[0] - eyes[0];
		const double ddy = on_box[1] - eyes[1];
		const double ddz = on_box[2] - eyes[2];
		const double dist_sq = ddx * ddx + ddy * ddy + ddz * ddz;

		// Visibility is not implemented: there is no world block raycast in the
		// SDK yet, so every point reports visible. With ignore_walls off this
		// therefore behaves as if nothing occludes the target, which is the
		// same assumption target_selector already makes -- not a new one
		// introduced here. When a block raycast lands, this is the only line
		// that has to change.
		const bool visible = true;

		if (dist_sq >= (visible ? range_sq : walls_sq)) return;

		out.considered++;

		float yaw = 0.0f, pitch = 0.0f;
		enhance::modules::killaura::displacement_to_angles(
			spot[0] - eyes[0], spot[1] - eyes[1], spot[2] - eyes[2], yaw, pitch);

		// Yaw is compared the short way round so a candidate across the wrap
		// point is not scored as half a turn away.
		const double dyaw = std::fabs(
			enhance::modules::aiming::angle_difference(yaw, current_yaw));
		const double dpitch = std::fabs(pitch - current_pitch);

		double score;
		if (point_mode == 1)
		{
			// LiquidBounce's preference: the smallest rotation change.
			score = dyaw + dpitch;
		}
		else
		{
			// Centre-biased. Distance to the middle of the hitbox dominates, so
			// the aim lands on the target rather than on whichever part of it
			// happens to be nearest the current view; the angular term only
			// breaks ties between points equally close to the centre.
			//
			// This is what keeps the pitch pointing at the target. Scoring
			// purely by rotation change lets the vertical choice slide to the
			// height already being looked at, because a hitbox is three times
			// taller than it is wide and so offers far more slack vertically
			// than horizontally.
			const double cx = (box.min[0] + box.max[0]) * 0.5;
			const double cy = (box.min[1] + box.max[1]) * 0.5;
			const double cz = (box.min[2] + box.max[2]) * 0.5;
			const double ox = spot[0] - cx, oy = spot[1] - cy, oz = spot[2] - cz;
			score = std::sqrt(ox * ox + oy * oy + oz * oz) * 100.0 + dyaw + dpitch;
		}

		if (visible)
		{
			if (score < best_vis_score)
			{
				best_vis_score = score;
				best_vis[0] = spot[0]; best_vis[1] = spot[1]; best_vis[2] = spot[2];
				best_vis_dist = std::sqrt(dist_sq);
				have_vis = true;
			}
		}
		else if (score < best_hid_score)
		{
			best_hid_score = score;
			best_hid[0] = spot[0]; best_hid[1] = spot[1]; best_hid[2] = spot[2];
			best_hid_dist = std::sqrt(dist_sq);
			have_hid = true;
		}
	};

	// The nearest point on the box: the eye position clamped into it. This is
	// the single most valuable candidate, because it is the one that maximises
	// effective reach, and a grid can miss it entirely.
	consider(std::clamp(eyes[0], box.min[0], box.max[0]),
	         std::clamp(eyes[1], box.min[1], box.max[1]),
	         std::clamp(eyes[2], box.min[2], box.max[2]));

	// Where the player is already looking, if that ray meets the box at all --
	// LiquidBounce's "preferred spot" fast path. Aiming here needs no turn at
	// all, so it wins whenever it is legal.
	{
		const float yaw_rad   = current_yaw   * 0.017453292519943295f;
		const float pitch_rad = current_pitch * 0.017453292519943295f;
		const double look[3] = {
			-std::sin(yaw_rad) * std::cos(pitch_rad),
			-std::sin(pitch_rad),
			 std::cos(yaw_rad) * std::cos(pitch_rad),
		};
		const double far_point[3] = {
			eyes[0] + look[0] * range,
			eyes[1] + look[1] * range,
			eyes[2] + look[2] * range,
		};
		double t = 0.0;
		if (first_hit(box, eyes, far_point, t))
			consider(eyes[0] + (far_point[0] - eyes[0]) * t,
			         eyes[1] + (far_point[1] - eyes[1]) * t,
			         eyes[2] + (far_point[2] - eyes[2]) * t);
	}

	// The grid. LiquidBounce projects points onto the box from the eye's
	// perspective so the samples are uniform ON SCREEN; that needs plane
	// projection and rotation matrices. A proportional grid in box space is
	// what LiquidBounce itself falls back to when the eye is inside the box,
	// and at melee distance against a player-sized hitbox the difference is not
	// worth the machinery.
	const int n = std::clamp(resolution, 2, 9);
	for (int ix = 0; ix < n; ++ix)
	{
		const double fx = (n == 1) ? 0.5 : static_cast<double>(ix) / (n - 1);
		const double px = box.min[0] + (box.max[0] - box.min[0]) * fx;
		for (int iy = 0; iy < n; ++iy)
		{
			const double fy = (n == 1) ? 0.5 : static_cast<double>(iy) / (n - 1);
			const double py = box.min[1] + (box.max[1] - box.min[1]) * fy;
			for (int iz = 0; iz < n; ++iz)
			{
				const double fz = (n == 1) ? 0.5 : static_cast<double>(iz) / (n - 1);
				const double pz = box.min[2] + (box.max[2] - box.min[2]) * fz;
				consider(px, py, pz);
			}
		}
	}

	if (have_vis)
	{
		out.x = best_vis[0]; out.y = best_vis[1]; out.z = best_vis[2];
		out.distance = best_vis_dist;
		out.visible = true;
		out.valid = true;
	}
	else if (have_hid)
	{
		out.x = best_hid[0]; out.y = best_hid[1]; out.z = best_hid[2];
		out.distance = best_hid_dist;
		out.visible = false;
		out.valid = true;
	}

	// Last resort: the centre of the hitbox.
	//
	// The target has already been accepted by the selector, so refusing to
	// produce a point means the aim silently gives up on something it agreed
	// was in range -- and the caller then falls back to a path that scores
	// candidates only by pitch difference, which makes the aim follow the
	// player's own pitch instead of the target. Aiming at the middle is always
	// defensible; returning nothing is not.
	if (!out.valid)
	{
		out.x = (box.min[0] + box.max[0]) * 0.5;
		out.y = (box.min[1] + box.max[1]) * 0.5;
		out.z = (box.min[2] + box.max[2]) * 0.5;
		out.valid = true;
	}

	(void)ignore_walls;
	return out;
}
