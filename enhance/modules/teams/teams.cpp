#include "teams.h"
#include "../../enhance.h"
#include "../../globals/globals.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <atomic>
#include <cstdlib>

// Teams module — detects teammates by leather armor color.
//
// Bedwars-style team detection: each team gets the same colored leather
// armor on a player. We sample the local player's leather armor pieces,
// extract the dye color via LeatherArmorItem.getColor(ItemStack), and
// remember them. For each candidate target, we do the same and compare.
// If ANY of our colors matches ANY of theirs (within configured tolerance),
// they're treated as a teammate and the killaura skips them.
//
// If color lookup fails (the deprecated getColor method may no longer be
// present in this MC build), we fall back to "any-leather-counts": as long
// as both sides have leather armor of some kind, treat as teammate. This
// covers the early game where everyone on a team has team-colored leather.

namespace enhance::modules::teams
{

// Cached JNI ids (resolved once on the worker thread).
static bool      g_ready                        = false;
static bool      g_color_lookup_available       = false;
static jclass    g_equipment_slot_cls           = nullptr;
static jobject   g_slot_head                    = nullptr;
static jobject   g_slot_chest                   = nullptr;
static jobject   g_slot_legs                    = nullptr;
static jobject   g_slot_feet                    = nullptr;
static jclass    g_living_entity_cls            = nullptr;
static jmethodID g_get_equipped_stack_mid       = nullptr;
static jclass    g_itemstack_cls                = nullptr;
static jmethodID g_itemstack_is_empty_mid       = nullptr;
static jmethodID g_itemstack_get_item_mid       = nullptr;
static jclass    g_dyed_color_cls               = nullptr;
static jmethodID g_dyed_get_color_mid           = nullptr;

static bool resolve_ids(JNIEnv* env)
{
	if (g_ready) return true;

	jclass slot_cls_local = sdk::classloader::find_class(env, sdk::mappings::equipment_slot_class_sig);
	if (!slot_cls_local) return false;
	g_equipment_slot_cls = (jclass)env->NewGlobalRef(slot_cls_local);
	env->DeleteLocalRef(slot_cls_local);
	if (!g_equipment_slot_cls) return false;

	auto resolve_slot = [&](const char* name) -> jobject {
		jfieldID fid = env->GetStaticFieldID(g_equipment_slot_cls, name, sdk::mappings::equipment_slot_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (!fid) return nullptr;
		jobject local = env->GetStaticObjectField(g_equipment_slot_cls, fid);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (!local) return nullptr;
		jobject g = env->NewGlobalRef(local);
		env->DeleteLocalRef(local);
		return g;
	};
	g_slot_head  = resolve_slot(sdk::mappings::equipment_slot_head_name);
	g_slot_chest = resolve_slot(sdk::mappings::equipment_slot_chest_name);
	g_slot_legs  = resolve_slot(sdk::mappings::equipment_slot_legs_name);
	g_slot_feet  = resolve_slot(sdk::mappings::equipment_slot_feet_name);
	if (!g_slot_head || !g_slot_chest || !g_slot_legs || !g_slot_feet) return false;

	jclass living_local = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
	if (!living_local) return false;
	g_get_equipped_stack_mid = env->GetMethodID(living_local,
		sdk::mappings::living_entity_get_equipped_stack_name,
		sdk::mappings::living_entity_get_equipped_stack_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_living_entity_cls = (jclass)env->NewGlobalRef(living_local);
	env->DeleteLocalRef(living_local);
	if (!g_get_equipped_stack_mid) return false;

	jclass stack_local = sdk::classloader::find_class(env, "net/minecraft/class_1799");
	if (!stack_local) return false;
	g_itemstack_is_empty_mid = env->GetMethodID(stack_local,
		sdk::mappings::itemstack_is_empty_name, sdk::mappings::itemstack_is_empty_sig);
	g_itemstack_get_item_mid = env->GetMethodID(stack_local,
		sdk::mappings::itemstack_get_item_name, sdk::mappings::itemstack_get_item_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_itemstack_cls = (jclass)env->NewGlobalRef(stack_local);
	env->DeleteLocalRef(stack_local);
	if (!g_itemstack_is_empty_mid || !g_itemstack_get_item_mid) return false;

	// Dye colour is a data component now: LeatherArmorItem and its
	// getColor(ItemStack) are both gone on 1.21.11, which is why colour
	// matching had silently degraded to "any dyed piece counts". The
	// replacement is a STATIC helper taking the stack plus a fallback.
	jclass dyed_local = sdk::classloader::find_class(env, sdk::mappings::dyed_color_component_class_sig);
	if (dyed_local)
	{
		g_dyed_color_cls = (jclass)env->NewGlobalRef(dyed_local);
		g_dyed_get_color_mid = env->GetStaticMethodID(dyed_local,
			sdk::mappings::dyed_color_get_color_name,
			sdk::mappings::dyed_color_get_color_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_dyed_get_color_mid = nullptr; }
		g_color_lookup_available = (g_dyed_get_color_mid != nullptr);
		env->DeleteLocalRef(dyed_local);
	}

	g_ready = true;
	return true;
}

// Color set + leather-presence flag for a single entity. -1 in any color
// slot means "no leather there". colors_count is how many leather pieces
// we found; useful as the fallback signal.
struct ArmorColors
{
	int   colors[4];
	int   count;
	ArmorColors() : count(0) { for (int i = 0; i < 4; ++i) colors[i] = -1; }
};

static ArmorColors read_armor(JNIEnv* env, jobject entity)
{
	ArmorColors out;
	if (!entity || !g_get_equipped_stack_mid) return out;

	jobject slots[4] = { g_slot_head, g_slot_chest, g_slot_legs, g_slot_feet };
	for (int i = 0; i < 4; ++i)
	{
		if (!slots[i]) continue;
		jobject stack = env->CallObjectMethod(entity, g_get_equipped_stack_mid, slots[i]);
		if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
		if (!stack) continue;

		jboolean empty = env->CallBooleanMethod(stack, g_itemstack_is_empty_mid);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (empty == JNI_TRUE) { env->DeleteLocalRef(stack); continue; }

		jobject item = env->CallObjectMethod(stack, g_itemstack_get_item_mid);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (!item) { env->DeleteLocalRef(stack); continue; }

		// There is no leather-armour class to test against any more. Ask for
		// the dye colour with a sentinel fallback: anything that comes back
		// different is a dyed piece, which is exactly the signal we wanted.
		int color = -1;
		if (g_color_lookup_available && g_dyed_get_color_mid)
		{
			constexpr jint k_undyed = -1;
			color = env->CallStaticIntMethod(g_dyed_color_cls, g_dyed_get_color_mid, stack, k_undyed);
			if (env->ExceptionCheck()) { env->ExceptionClear(); color = -1; }
		}

		if (color != -1)
		{
			out.count++;
			out.colors[i] = color;
		}
		env->DeleteLocalRef(item);
		env->DeleteLocalRef(stack);
	}
	return out;
}

static int rgb_distance(int a, int b)
{
	const int dr = ((a >> 16) & 0xff) - ((b >> 16) & 0xff);
	const int dg = ((a >>  8) & 0xff) - ((b >>  8) & 0xff);
	const int db = ( a        & 0xff) - ( b        & 0xff);
	return std::abs(dr) + std::abs(dg) + std::abs(db);
}

static bool colors_match(int a, int b, int tolerance)
{
	if (a < 0 || b < 0) return false;
	return rgb_distance(a, b) <= tolerance;
}

void run()
{
	if (!globals::teams_enabled) return;
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return;
	resolve_ids(env);
}

bool is_teammate(jobject local_player, jobject other)
{
	if (!globals::teams_enabled) return false;
	if (!local_player || !other) return false;
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env || !resolve_ids(env)) return false;

	const ArmorColors mine   = read_armor(env, local_player);
	const ArmorColors theirs = read_armor(env, other);

	// No leather on either side → can't tell, default to non-teammate so
	// the killaura still works against diamond/iron-armoured enemies.
	if (mine.count == 0 || theirs.count == 0) return false;

	// Color lookup unavailable → fall back to "both have leather" = team.
	if (!g_color_lookup_available) return true;

	const int tol = globals::teams_color_tolerance;
	for (int i = 0; i < 4; ++i)
	{
		if (mine.colors[i] < 0) continue;
		for (int j = 0; j < 4; ++j)
		{
			if (colors_match(mine.colors[i], theirs.colors[j], tol)) return true;
		}
	}
	return false;
}

}
