#define NOMINMAX
#include "target_selector.h"
#include "friends.h"
#include "../teams/teams.h"
#include "../../enhance.h"
#include <sdk/minecraft/world/world.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace enhance::modules::killaura
{

static double distance_to_entity(double px, double py, double pz, sdk::entity_client& e)
{
	const double dx = e.get_x() - px;
	const double dy = e.get_y() - py;
	const double dz = e.get_z() - pz;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// FOV check: angle between current view direction and direction-to-target.
static bool in_fov(double dx, double dy, double dz, float yaw, float pitch, float max_fov_deg)
{
	if (max_fov_deg >= 360.0f) return true;

	constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
	const float vx = -std::sin(yaw * kDeg2Rad) * std::cos(pitch * kDeg2Rad);
	const float vy = -std::sin(pitch * kDeg2Rad);
	const float vz =  std::cos(yaw * kDeg2Rad) * std::cos(pitch * kDeg2Rad);

	const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (dist < 1e-6) return true;
	const double tx = dx / dist, ty = dy / dist, tz = dz / dist;
	const double dot = vx * tx + vy * ty + vz * tz;
	const double angle = std::acos(std::max(-1.0, std::min(1.0, dot))) * 180.0 / 3.14159265358979323846;
	return angle <= max_fov_deg * 0.5;
}

// Simple stepped block-sampling LOS check. Not as accurate as a real DDA
// raycast, but good enough — and the BlazeDLC equivalent is a 10-point
// vertical hitbox scan, so we cover that by sampling along the segment
// from eye to one mid-hitbox point. The aim-point stage does the finer
// 10-point selection.
static bool has_line_of_sight(jobject world, double ex, double ey, double ez,
                               double tx, double ty, double tz)
{
	// Without a working world raycast helper in the SDK, we conservatively
	// return true. Wall checks happen at the aim-point stage which has
	// per-Y resolution and prunes blocked points. A future iteration can
	// hook into world.method_8320 (getBlockState) for a real DDA — that
	// requires reading BlockState.method_26204().isAir() which is not yet
	// in the SDK. Leaving the hook in place for that future work.
	(void)world; (void)ex; (void)ey; (void)ez; (void)tx; (void)ty; (void)tz;
	return true;
}

static bool type_passes(sdk::entity_client& e, const TargetFilter& f)
{
	if (e.is_player_class()) return f.players;
	if (e.is_animal_class()) return f.animals;
	if (e.is_mob_class())    return f.mobs;
	return false;
}

static SelectionStats g_stats;

const SelectionStats& last_selection_stats() { return g_stats; }

jobject pick_best_target(jobject world,
                          jobject local_player,
                          float max_distance,
                          float max_fov_degrees,
                          float current_yaw,
                          float current_pitch,
                          bool ignore_walls,
                          const TargetFilter& filter)
{
	if (!world || !local_player) return nullptr;

	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return nullptr;

	sdk::entity_client local(local_player);
	const double px = local.get_x();
	const double py = local.get_y() + 1.62;   // eye height (stand)
	const double pz = local.get_z();

	sdk::world_client wc(world);

	// Every loaded entity, not the players list. The old source made the mob
	// and animal filters dead letters -- a mob is not in `players`, so
	// type_passes could never see one and a scan for them always came back
	// empty however far the range was raised.
	std::vector<jobject> entities = wc.get_entities();

	g_stats = SelectionStats{};
	g_stats.scanned = static_cast<int>(entities.size());

	struct Candidate { jobject ref; double dist; };
	std::vector<Candidate> candidates;
	candidates.reserve(entities.size());

	for (jobject ent : entities)
	{
		if (!ent) continue;
		sdk::entity_client e(ent);

		if (e.is_same_object(local_player)) { env->DeleteLocalRef(ent); continue; }

		// Type gate FIRST. The scan walks every loaded entity now, so items,
		// arrows and boats reach here too, and the health check below is a
		// LivingEntity call -- asking a dropped item for its health invokes
		// whatever happens to sit at that vtable index. type_passes only admits
		// player, animal and mob, all of which are LivingEntity, so ordering it
		// ahead of the health test is what makes that test type-safe.
		if (!type_passes(e, filter)) { env->DeleteLocalRef(ent); continue; }

		if (!e.is_alive() || e.get_health() <= 0.0f) { env->DeleteLocalRef(ent); continue; }

		if (!filter.include_friends)
		{
			if (e.is_player_class())
			{
				std::string name = e.get_name_string();
				if (!name.empty() && friends_list::contains(name))
				{
					env->DeleteLocalRef(ent);
					continue;
				}
			}
		}

		// Teams: skip teammates detected by matching leather-armor color.
		if (e.is_player_class() && enhance::modules::teams::is_teammate(local_player, ent))
		{
			env->DeleteLocalRef(ent);
			continue;
		}

		const double ex = e.get_x();
		const double ey = e.get_y();
		const double ez = e.get_z();
		const double dx = ex - px;
		const double dy = ey - py;
		const double dz = ez - pz;
		const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		++g_stats.after_type;
		if (g_stats.nearest < 0.0 || dist < g_stats.nearest)
			g_stats.nearest = dist;

		if (dist > max_distance) { env->DeleteLocalRef(ent); continue; }
		++g_stats.after_range;

		// The FOV test aims at the body, not the feet. get_y() is the entity's
		// FEET while py is our EYES, so the raw vector points about 1.6 blocks
		// below where anyone actually aims — at 3 blocks that is nearly 30
		// degrees down. A wide cone never noticed (killaura defaults to 360),
		// but a narrow one rejected everything unless you were aiming at the
		// target's shoes. Same reference height the line-of-sight check below
		// already uses.
		if (!in_fov(dx, dy + 1.0, dz, current_yaw, current_pitch, max_fov_degrees)) { env->DeleteLocalRef(ent); continue; }
		++g_stats.after_fov;

		if (!ignore_walls && !has_line_of_sight(world, px, py, pz, ex, ey + 1.0, ez))
		{
			env->DeleteLocalRef(ent);
			continue;
		}

		candidates.push_back({ ent, dist });
	}

	if (candidates.empty()) return nullptr;

	std::sort(candidates.begin(), candidates.end(),
		[](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

	jobject best = candidates.front().ref;
	for (size_t i = 1; i < candidates.size(); ++i)
		env->DeleteLocalRef(candidates[i].ref);
	return best;
}

}
