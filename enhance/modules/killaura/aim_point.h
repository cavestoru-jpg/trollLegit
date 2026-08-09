#pragma once

#include <sdk/includes.h>

namespace enhance::modules::killaura
{
	struct AimPoint
	{
		double x, y, z;            // world point to aim at
		double box_min[3];
		double box_max[3];
		bool valid;
	};

	// Pick the best aim point on the target hitbox: 10 vertical samples
	// across the box, filter by reach and (optional) LOS, return the one
	// with the smallest angular delta from the current rotation. Adds
	// gaussian jitter scaled by player velocity for AC bypass.
	//
	// Returns valid=false if no point is reachable.
	AimPoint compute_aim_point(jobject target,
	                            jobject local_player,
	                            float max_reach,
	                            float current_yaw,
	                            float current_pitch,
	                            bool ignore_walls,
	                            float jitter_x,
	                            float jitter_y,
	                            float jitter_z);

	// Convert a world-space displacement (dx, dy, dz) to yaw/pitch
	// matching Minecraft's convention.
	void displacement_to_angles(double dx, double dy, double dz,
	                              float& out_yaw, float& out_pitch);
}
