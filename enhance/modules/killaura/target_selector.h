#pragma once

#include <sdk/includes.h>

namespace enhance::modules::killaura
{
	struct TargetFilter
	{
		bool players;
		bool mobs;
		bool animals;
		bool include_friends;   // if false, names in friends list are skipped
	};

	// Finds the best target in the world by distance, FOV and (optional)
	// line-of-sight to the hitbox. Returns a local-ref jobject (caller
	// owns and must DeleteLocalRef) or nullptr.
	//
	// max_fov_degrees = 360 → no FOV gate. current_yaw/pitch are used for
	// FOV cone test only — silent rotation is irrelevant here.
	// How many entities the last pick_best_target call looked at, and how many
	// survived each stage. Diagnostic only, written on the calling thread.
	struct SelectionStats
	{
		int scanned = 0;      // entities the world handed us
		int after_type = 0;   // still eligible after alive/type/friend/team
		int after_range = 0;  // still eligible after the distance test
		int after_fov = 0;    // still eligible after the FOV cone
		// Distance to the closest type-eligible entity, whether or not it
		// passed the range test. Separates "the range is too small" from "the
		// thing I am aiming at is not in the scan at all" without guesswork.
		double nearest = -1.0;
	};
	const SelectionStats& last_selection_stats();

	jobject pick_best_target(jobject world,
	                          jobject local_player,
	                          float max_distance,
	                          float max_fov_degrees,
	                          float current_yaw,
	                          float current_pitch,
	                          bool ignore_walls,
	                          const TargetFilter& filter);
}
