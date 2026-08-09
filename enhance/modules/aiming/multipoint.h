#pragma once

#include <sdk/includes.h>

// Multipoint aim point selection, ported from LiquidBounce's raytraceBoxes
// (utils/aiming/utils/RotationFinding.kt).
//
// The existing killaura aim point samples eleven heights straight up the middle
// of the hitbox: X and Z are pinned to the box centre, so it can only ever aim
// at the vertical axis of the target and never at a corner or an edge. That
// costs reach -- the nearest part of a hitbox is up to half its width closer
// than the centre -- and it means a target that is only visible around a corner
// cannot be aimed at at all.
//
// This scans the whole box instead, keeps the candidate that needs the smallest
// turn from where the player is already looking, and prefers a visible point
// over a hidden one.
namespace enhance::modules::aiming::multipoint
{
	struct result_t
	{
		bool   valid = false;
		double x = 0.0, y = 0.0, z = 0.0;
		bool   visible = false;    // whether the chosen point passed the visibility test
		double distance = 0.0;     // eye to the point where the ray enters the box
		int    considered = 0;     // candidates that survived the range gate
	};

	// `range` gates points that are visible, `walls_range` points that are not,
	// exactly as LiquidBounce does: a hidden point is still worth aiming at when
	// it is close enough, but not at full reach.
	//
	// Runs on the enhance worker thread -- it reads the target's bounding box
	// through the SDK, which uses the worker's JNIEnv.
	result_t compute(jobject target,
	                 jobject local_player,
	                 double range,
	                 double walls_range,
	                 float current_yaw,
	                 float current_pitch,
	                 int resolution,
	                 int point_mode,   // 0 centre-biased, 1 least rotation
	                 bool ignore_walls);
}
