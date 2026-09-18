#include "triggerbot.h"
#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../hooks/Hook.h"
#include "../../utils/logger.h"
#include "../stap/stap.h"
#include "../wtap/wtap.h"
#include "../autojumpreset/autojumpreset.h"
#include "../killaura/target_selector.h"
#include "../killaura/silent_rotation_hook.h"
#include "../killaura/sprint.h"
#include "../../utils/client_thread.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/player/player.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>
#include <sdk/minecraft/world/world.h>
#include <cmath>
#include <vector>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <cstring>
#include <random>

jobject enhance::modules::triggerbot::last_targeted_entity = nullptr;
ULONGLONG enhance::modules::triggerbot::last_attack_time = 0;
int enhance::modules::triggerbot::current_delay = 0;

// Last raycast result, for the diagnostic line.
static int    g_last_scan_total = 0;
static int    g_last_scan_alive = 0;
static double g_last_hit_dist   = -1.0;

static std::random_device rd;
static std::mt19937 gen(rd());

// ---------- Built-in sprint reset ----------
// Sends a W key-up to break sprint right after a hit so the next attack lands
// as a crit if the player is falling, then re-presses W after N ticks. Tick
// granularity matches MC (50 ms / tick).
static void sr_release_w()
{
	INPUT input = {};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = 'W';
	input.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(INPUT));
}
static void sr_repress_w()
{
	INPUT input = {};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = 'W';
	input.ki.dwFlags = 0;
	SendInput(1, &input, sizeof(INPUT));
}
static bool sr_is_sprinting()
{
	jobject player = sdk::instance->get_player();
	if (!player) return false;
	auto env = enhance::instance->get_env();
	if (!env) return false;
	jclass entity_class = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
	if (!entity_class) { env->DeleteLocalRef(player); return false; }
	jmethodID mid = env->GetMethodID(entity_class, sdk::mappings::is_sprinting_name, sdk::mappings::is_sprinting_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(entity_class);
	if (!mid) { env->DeleteLocalRef(player); return false; }
	jboolean s = env->CallBooleanMethod(player, mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(player);
	return s == JNI_TRUE;
}

// Force-flush the STOP_SPRINTING action packet to the server right now.
// setSprinting(false) alone only flips the data tracker; the packet that
// actually tells the server "I'm not sprinting anymore" lives in
// ClientPlayerEntity.sendSprintingPacket() and is normally deferred to the
// next tickMovement(). For a crit we need it on the wire BEFORE the attack.
static void sr_send_sprinting_packet()
{
	jobject player = sdk::instance->get_player();
	if (!player) return;
	auto env = enhance::instance->get_env();
	if (!env) { return; }

	jclass client_player_class = sdk::classloader::find_class(env, sdk::mappings::clientplayerentity_class_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!client_player_class) { env->DeleteLocalRef(player); return; }

	jmethodID mid = env->GetMethodID(client_player_class,
		sdk::mappings::send_sprinting_packet_name,
		sdk::mappings::send_sprinting_packet_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(client_player_class);

	if (mid)
	{
		env->CallVoidMethod(player, mid);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	env->DeleteLocalRef(player);
}

static void sprint_reset_on_hit()
{
	if (!globals::triggerbot_sprint_reset) return;
	if (globals::triggerbot_sprint_reset_active) return;
	if (!(GetAsyncKeyState('W') & 0x8000)) return;   // only if user is actually holding W
	if (!sr_is_sprinting()) return;

	globals::triggerbot_sprint_reset_active = true;
	globals::triggerbot_sprint_reset_start_time = GetTickCount64();

	// 1+2. Clear the sprint flag and push the STOP_SPRINTING action packet, so
	//      the server sees no-sprint before our attack arrives — left to
	//      itself the packet waits for the next tickMovement and the attack
	//      lands non-crit.
	//
	//      Both happen on the JVM tick thread, not here. setSprinting mutates
	//      the entity's attribute-modifier map, which the tick thread mutates
	//      too and which is not thread-safe; calling it from this thread
	//      crashed the game with ArrayIndexOutOfBoundsException(-1) inside
	//      Object2ObjectArrayMap.remove. The request is applied at the top of
	//      aiming::tick_movement_hook, which is also early enough in the tick
	//      to keep the packet ahead of the attack.
	enhance::modules::killaura::request_stop_sprint();

	// 3. Inject a W key-up so MC's input handler can't immediately re-start
	//    sprint on the next tick while the user is still physically holding W.
	sr_release_w();
}

static void sprint_reset_tick()
{
	if (!globals::triggerbot_sprint_reset_active) return;

	// Force-restore if the feature was disabled mid-cycle or GUI opened.
	if (!globals::triggerbot_sprint_reset || globals::show_gui) {
		sr_repress_w();
		globals::triggerbot_sprint_reset_active = false;
		return;
	}

	ULONGLONG now = GetTickCount64();
	int ticks = globals::triggerbot_sprint_reset_ticks;
	if (ticks < 1) ticks = 1;
	ULONGLONG duration_ms = (ULONGLONG)ticks * 50ULL;
	if (now - globals::triggerbot_sprint_reset_start_time >= duration_ms) {
		sr_repress_w();
		globals::triggerbot_sprint_reset_active = false;
	}
}

static int get_random_delay(int min_delay, int max_delay)
{
	if (min_delay >= max_delay)
		return min_delay;
	std::uniform_int_distribution<> dis(min_delay, max_delay);
	return dis(gen);
}

static void send_click()
{
	HWND window = Hook::get_window();
	if (!window)
		return;

	HWND foreground_window = GetForegroundWindow();
	if (foreground_window != window)
		return;

	POINT cursorPos{};
	GetCursorPos(&cursorPos);
	ScreenToClient(window, &cursorPos);
	PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cursorPos.x, cursorPos.y));
	PostMessageA(window, WM_LBUTTONUP, MK_LBUTTON, MAKELPARAM(cursorPos.x, cursorPos.y));
}

static void hold_right_click_down()
{
	HWND window = Hook::get_window();
	if (!window) return;

	POINT cursorPos{};
	GetCursorPos(&cursorPos);
	ScreenToClient(window, &cursorPos);
	PostMessageA(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(cursorPos.x, cursorPos.y));
}

static void release_right_click()
{
	HWND window = Hook::get_window();
	if (!window) return;

	POINT cursorPos{};
	GetCursorPos(&cursorPos);
	ScreenToClient(window, &cursorPos);
	PostMessageA(window, WM_RBUTTONUP, MK_RBUTTON, MAKELPARAM(cursorPos.x, cursorPos.y));
}

static bool has_shield_in_offhand()
{
	jobject player = sdk::instance->get_player();
	if (!player) return false;

	auto env = enhance::instance->get_env();
	if (!env)
	{
		return false;
	}

	jclass player_class = env->GetObjectClass(player);
	if (!player_class)
	{
		env->DeleteLocalRef(player);
		return false;
	}

	jfieldID inventory_fid = env->GetFieldID(player_class, sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig);
	if (!inventory_fid)
	{
		env->DeleteLocalRef(player_class);
		env->DeleteLocalRef(player);
		return false;
	}

	jobject inventory = env->GetObjectField(player, inventory_fid);
	env->DeleteLocalRef(player_class);
	if (!inventory)
	{
		env->DeleteLocalRef(player);
		return false;
	}

	jclass inventory_class = env->GetObjectClass(inventory);
	if (!inventory_class)
	{
		env->DeleteLocalRef(inventory);
		env->DeleteLocalRef(player);
		return false;
	}

	jmethodID get_stack_mid = env->GetMethodID(inventory_class, sdk::mappings::inventory_get_stack_name, sdk::mappings::inventory_get_stack_sig);
	if (!get_stack_mid)
	{
		env->DeleteLocalRef(inventory_class);
		env->DeleteLocalRef(inventory);
		env->DeleteLocalRef(player);
		return false;
	}

	jobject offhand_stack = env->CallObjectMethod(inventory, get_stack_mid, 40);
	env->DeleteLocalRef(inventory_class);
	env->DeleteLocalRef(inventory);
	env->DeleteLocalRef(player);

	if (!offhand_stack) return false;

	jclass itemstack_class = env->GetObjectClass(offhand_stack);
	if (!itemstack_class)
	{
		env->DeleteLocalRef(offhand_stack);
		return false;
	}

	jmethodID is_empty_mid = env->GetMethodID(itemstack_class, sdk::mappings::itemstack_is_empty_name, sdk::mappings::itemstack_is_empty_sig);
	if (!is_empty_mid)
	{
		env->DeleteLocalRef(itemstack_class);
		env->DeleteLocalRef(offhand_stack);
		return false;
	}

	jboolean is_empty = env->CallBooleanMethod(offhand_stack, is_empty_mid);
	if (is_empty == JNI_TRUE)
	{
		env->DeleteLocalRef(itemstack_class);
		env->DeleteLocalRef(offhand_stack);
		return false;
	}

	jmethodID get_item_mid = env->GetMethodID(itemstack_class, sdk::mappings::itemstack_get_item_name, sdk::mappings::itemstack_get_item_sig);
	env->DeleteLocalRef(itemstack_class);
	if (!get_item_mid)
	{
		env->DeleteLocalRef(offhand_stack);
		return false;
	}

	jobject item = env->CallObjectMethod(offhand_stack, get_item_mid);
	env->DeleteLocalRef(offhand_stack);
	if (!item) return false;

	jclass item_class = env->GetObjectClass(item);
	if (!item_class)
	{
		env->DeleteLocalRef(item);
		return false;
	}

	jmethodID get_translation_key_mid = env->GetMethodID(item_class, sdk::mappings::item_get_translation_key_name, sdk::mappings::item_get_translation_key_sig);
	if (!get_translation_key_mid)
	{
		env->DeleteLocalRef(item_class);
		env->DeleteLocalRef(item);
		return false;
	}

	jstring translation_key = (jstring)env->CallObjectMethod(item, get_translation_key_mid);
	env->DeleteLocalRef(item_class);
	env->DeleteLocalRef(item);
	if (!translation_key) return false;

	const char* key_cstr = env->GetStringUTFChars(translation_key, nullptr);
	if (!key_cstr)
	{
		env->DeleteLocalRef(translation_key);
		return false;
	}

	bool is_shield = (strstr(key_cstr, "shield") != nullptr);
	env->ReleaseStringUTFChars(translation_key, key_cstr);
	env->DeleteLocalRef(translation_key);

	return is_shield;
}

static bool is_entity_blocking(jobject target_entity)
{
	if (!target_entity) return false;
	auto env = enhance::instance->get_env();
	if (!env) return false;

	jclass player_class_check = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
	if (!player_class_check) return false;
	
	jboolean is_player = env->IsInstanceOf(target_entity, player_class_check);
	env->DeleteLocalRef(player_class_check);
	
	if (!is_player) return false;

	jclass living_entity_class = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
	if (!living_entity_class) return false;
	
	jmethodID is_blocking_mid = env->GetMethodID(living_entity_class, sdk::mappings::living_entity_is_blocking_name, sdk::mappings::living_entity_is_blocking_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(living_entity_class);
	
	if (!is_blocking_mid) return false;
	
	jboolean blocking = env->CallBooleanMethod(target_entity, is_blocking_mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return blocking == JNI_TRUE;
}

// Check if player is holding a weapon (sword, axe, or mace)
static bool is_holding_weapon()
{
	jobject player = sdk::instance->get_player();
	if (!player) return false;

	auto env = enhance::instance->get_env();
	if (!env)
	{
		return false;
	}

	jclass player_class = env->GetObjectClass(player);
	if (!player_class)
	{
		env->DeleteLocalRef(player);
		return false;
	}

	jfieldID inventory_fid = env->GetFieldID(player_class, sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig);
	if (!inventory_fid)
	{
		env->DeleteLocalRef(player_class);
		env->DeleteLocalRef(player);
		return false;
	}

	jobject inventory = env->GetObjectField(player, inventory_fid);
	env->DeleteLocalRef(player_class);
	if (!inventory)
	{
		env->DeleteLocalRef(player);
		return false;
	}

	jclass inventory_class = env->GetObjectClass(inventory);
	if (!inventory_class)
	{
		env->DeleteLocalRef(inventory);
		env->DeleteLocalRef(player);
		return false;
	}

	// Get selected slot
	jfieldID selected_slot_fid = env->GetFieldID(inventory_class, sdk::mappings::inventory_selected_slot_name, sdk::mappings::inventory_selected_slot_sig);
	if (!selected_slot_fid)
	{
		env->DeleteLocalRef(inventory_class);
		env->DeleteLocalRef(inventory);
		env->DeleteLocalRef(player);
		return false;
	}

	int selected_slot = env->GetIntField(inventory, selected_slot_fid);

	jmethodID get_stack_mid = env->GetMethodID(inventory_class, sdk::mappings::inventory_get_stack_name, sdk::mappings::inventory_get_stack_sig);
	env->DeleteLocalRef(inventory_class);
	if (!get_stack_mid)
	{
		env->DeleteLocalRef(inventory);
		env->DeleteLocalRef(player);
		return false;
	}

	jobject stack = env->CallObjectMethod(inventory, get_stack_mid, selected_slot);
	env->DeleteLocalRef(inventory);
	env->DeleteLocalRef(player);

	if (!stack) return false;

	jclass itemstack_class = env->GetObjectClass(stack);
	if (!itemstack_class)
	{
		env->DeleteLocalRef(stack);
		return false;
	}

	jmethodID is_empty_mid = env->GetMethodID(itemstack_class, sdk::mappings::itemstack_is_empty_name, sdk::mappings::itemstack_is_empty_sig);
	if (!is_empty_mid)
	{
		env->DeleteLocalRef(itemstack_class);
		env->DeleteLocalRef(stack);
		return false;
	}

	jboolean is_empty = env->CallBooleanMethod(stack, is_empty_mid);
	if (is_empty == JNI_TRUE)
	{
		env->DeleteLocalRef(itemstack_class);
		env->DeleteLocalRef(stack);
		return false;
	}

	jmethodID get_item_mid = env->GetMethodID(itemstack_class, sdk::mappings::itemstack_get_item_name, sdk::mappings::itemstack_get_item_sig);
	env->DeleteLocalRef(itemstack_class);
	if (!get_item_mid)
	{
		env->DeleteLocalRef(stack);
		return false;
	}

	jobject item = env->CallObjectMethod(stack, get_item_mid);
	env->DeleteLocalRef(stack);
	if (!item) return false;

	jclass item_class = env->GetObjectClass(item);
	if (!item_class)
	{
		env->DeleteLocalRef(item);
		return false;
	}

	jmethodID get_translation_key_mid = env->GetMethodID(item_class, sdk::mappings::item_get_translation_key_name, sdk::mappings::item_get_translation_key_sig);
	if (!get_translation_key_mid)
	{
		env->DeleteLocalRef(item_class);
		env->DeleteLocalRef(item);
		return false;
	}

	jstring translation_key = (jstring)env->CallObjectMethod(item, get_translation_key_mid);
	env->DeleteLocalRef(item_class);
	env->DeleteLocalRef(item);
	if (!translation_key) return false;

	const char* key_cstr = env->GetStringUTFChars(translation_key, nullptr);
	if (!key_cstr)
	{
		env->DeleteLocalRef(translation_key);
		return false;
	}

	// Check for weapon types: sword, axe, mace
	bool is_weapon = (strstr(key_cstr, "sword") != nullptr ||
	                  strstr(key_cstr, "axe") != nullptr ||
	                  strstr(key_cstr, "mace") != nullptr ||
	                  strstr(key_cstr, "trident") != nullptr);
	env->ReleaseStringUTFChars(translation_key, key_cstr);
	env->DeleteLocalRef(translation_key);

	return is_weapon;
}

static void handle_shield_use()
{
	if (!globals::triggerbot_use_shield) return;
	
	if (!has_shield_in_offhand()) return;

	if (!globals::triggerbot_shield_holding)
	{
		globals::triggerbot_shield_holding = true;
		globals::triggerbot_shield_start_time = GetTickCount64();
		hold_right_click_down();
	}
	else
	{
		globals::triggerbot_shield_start_time = GetTickCount64();
	}
}

static void update_shield_state()
{
	if (!globals::triggerbot_use_shield)
	{
		if (globals::triggerbot_shield_holding)
		{
			release_right_click();
			globals::triggerbot_shield_holding = false;
		}
		return;
	}

	if (!globals::triggerbot_shield_holding) return;

	ULONGLONG current_time = GetTickCount64();
	if (current_time - globals::triggerbot_shield_start_time >= (ULONGLONG)globals::triggerbot_shield_duration_ms)
	{
		release_right_click();
		globals::triggerbot_shield_holding = false;
	}
}

// Without the crosshair we have no raycast at all, so the cone is what stands in
// for one: only an entity within a couple of degrees of the look vector counts
// as "the thing the player is pointing at". Widening this turns the triggerbot
// into an aimbot that fires at anything vaguely ahead, walls or not.

// Target selection for the through-walls path: our own raycast.
//
// MinecraftClient's crosshairTarget stops at the first block, so a covered
// player comes back as a block hit and never reaches us as an entity. This is
// the same thing vanilla does -- march the eye ray through the world and take
// the first entity hitbox it crosses -- with the block test simply left out.
//
// It deliberately does NOT reuse killaura's pick_best_target. That selector is
// built for "who should I be fighting": an FOV cone, friends, teams, and a
// distance measured from our eyes to the target's FEET. All of that is wrong
// here, where the question is only "what is under my crosshair, ignoring
// walls", and every one of those filters cost a round of debugging before this
// was rewritten.
//
// Returns a local ref the caller owns, or nullptr.

// Slab method. Returns the distance along the ray to the near face, or a
// negative value when the ray misses.
static double ray_box_distance(double ox, double oy, double oz,
                               double dx, double dy, double dz,
                               double min_x, double min_y, double min_z,
                               double max_x, double max_y, double max_z,
                               double max_dist)
{
	double t_near = 0.0;
	double t_far  = max_dist;

	const double o[3] = { ox, oy, oz };
	const double d[3] = { dx, dy, dz };
	const double lo[3] = { min_x, min_y, min_z };
	const double hi[3] = { max_x, max_y, max_z };

	for (int axis = 0; axis < 3; ++axis)
	{
		if (std::fabs(d[axis]) < 1e-8)
		{
			// Ray is parallel to this pair of planes: it can only hit if it
			// already lies between them.
			if (o[axis] < lo[axis] || o[axis] > hi[axis])
				return -1.0;
			continue;
		}

		double t1 = (lo[axis] - o[axis]) / d[axis];
		double t2 = (hi[axis] - o[axis]) / d[axis];
		if (t1 > t2) { const double tmp = t1; t1 = t2; t2 = tmp; }

		if (t1 > t_near) t_near = t1;
		if (t2 < t_far)  t_far  = t2;
		if (t_near > t_far) return -1.0;
	}

	return t_near;
}

static jobject pick_through_walls_target(JNIEnv* env)
{
	jobject world = sdk::instance->get_world();
	if (!world)
		return nullptr;

	jobject local_player = sdk::instance->get_player();
	if (!local_player)
	{
		env->DeleteLocalRef(world);
		return nullptr;
	}

	sdk::entity_client local(local_player);
	const double ox = local.get_x();
	const double oy = local.get_y() + 1.62;   // eye height, standing
	const double oz = local.get_z();

	const float yaw = local.get_yaw();
	const float pitch = local.get_pitch();

	constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
	const double cy = std::cos(yaw * kDeg2Rad), sy = std::sin(yaw * kDeg2Rad);
	const double cp = std::cos(pitch * kDeg2Rad), sp = std::sin(pitch * kDeg2Rad);

	// Minecraft's look vector.
	const double dx = -sy * cp;
	const double dy = -sp;
	const double dz =  cy * cp;

	const double reach = globals::triggerbot_through_walls_range;
	const double grow  = globals::triggerbot_through_walls_expand;

	sdk::world_client wc(world);
	std::vector<jobject> entities = wc.get_entities();

	jobject best = nullptr;
	double  best_dist = reach + 1.0;
	int     considered = 0;

	for (jobject ent : entities)
	{
		if (!ent) continue;

		sdk::entity_client e(ent);

		// is_alive() is an Entity method and safe on anything the world hands
		// us. get_health() is not -- it is a LivingEntity call, and this loop
		// sees items, arrows and boats too. It is guarded inside entity_client
		// now, but the health test is skipped for non-living entities anyway:
		// a boat or a minecart is a legitimate thing to hit and has no health.
		if (e.is_same_object(local_player) || !e.is_alive())
		{
			env->DeleteLocalRef(ent);
			continue;
		}

		const bool living = e.is_player_class() || e.is_mob_class() || e.is_animal_class();
		if (living && e.get_health() <= 0.0f)
		{
			env->DeleteLocalRef(ent);
			continue;
		}

		jobject box = e.get_bounding_box();
		if (!box)
		{
			env->DeleteLocalRef(ent);
			continue;
		}

		sdk::box_client b(box);
		const double t = ray_box_distance(ox, oy, oz, dx, dy, dz,
			b.get_min_x() - grow, b.get_min_y() - grow, b.get_min_z() - grow,
			b.get_max_x() + grow, b.get_max_y() + grow, b.get_max_z() + grow,
			reach);
		env->DeleteLocalRef(box);

		++considered;

		if (t < 0.0 || t > reach)
		{
			env->DeleteLocalRef(ent);
			continue;
		}

		if (t < best_dist)
		{
			if (best) env->DeleteLocalRef(best);
			best = ent;
			best_dist = t;
		}
		else
		{
			env->DeleteLocalRef(ent);
		}
	}

	g_last_scan_total = static_cast<int>(entities.size());
	g_last_scan_alive = considered;
	g_last_hit_dist   = best ? best_dist : -1.0;

	env->DeleteLocalRef(local_player);
	env->DeleteLocalRef(world);
	return best;
}

// The through-walls attack rides on killaura's sendMovementPackets hook, so the
// hook has to be up before the first such attack. Attached lazily and at most
// once per process, the way killaura does it: JNIHook needs a live JVM and a
// loaded ClientPlayerEntity, neither of which exists at DLL attach. Killaura may
// have attached it already — init() is idempotent and returns true once the hook
// is in place, and the vendored JNIHook now lets several hooks on one class
// coexist.
static bool ensure_attack_hook()
{
	static bool s_ok = false;
	if (s_ok)
		return true;

	// Retried rather than latched after the first failure. The usual reason to
	// fail is that JNIHook could not get can_suspend, which is a transient
	// condition after a re-injection -- latching would write the feature off
	// for the rest of the session over a few seconds of bad timing. Throttled
	// so a permanent failure costs one attach attempt every two seconds
	// instead of one per worker pass.
	static ULONGLONG s_next_try = 0;
	const ULONGLONG now = GetTickCount64();
	if (now < s_next_try)
		return false;
	s_next_try = now + 2000;

	// Posted to the client thread rather than attached here. See
	// utils/client_thread.h: attaching from the worker needs can_suspend,
	// which after a re-injection usually cannot be had.
	enhance::client_thread::post([]() {
		try { s_ok = enhance::modules::silent_rotation_hook::init(); } catch (...) {}
	});
	return s_ok;
}

void enhance::modules::triggerbot::run()
{
	update_shield_state();

	// Always tick the sprint reset so a pending re-press resolves even when
	// triggerbot itself early-returns this frame (GUI open, no target, etc.).
	sprint_reset_tick();

	if (!globals::triggerbot_enabled)
	{
		cleanup();
		return;
	}

	if (globals::show_gui)
		return;

	HWND window = Hook::get_window();
	if (!window)
		return;

	HWND foreground_window = GetForegroundWindow();
	if (foreground_window != window)
		return;

	jobject minecraft = sdk::instance->get_minecraft();
	if (!minecraft)
		return;

	auto env = enhance::instance->get_env();
	if (!env)
		return;

	jobject crosshair_target = sdk::instance->get_crosshair_target();

	jobject target_entity = nullptr;
	if (crosshair_target && is_entity_hit_result(crosshair_target))
		target_entity = get_entity_from_hit_result(crosshair_target);

	// A non-entity crosshair result only means a block got in the way first, so
	// with through-walls on we look for a target ourselves instead of giving up.
	// Everything below this point treats it exactly like a crosshair target —
	// same cooldown, crit, shield and weapon gates. Only the attack itself
	// differs, which is what this flag is for.
	bool target_from_fallback = false;
	if (!target_entity && globals::triggerbot_through_walls)
	{
		target_entity = pick_through_walls_target(env);
		target_from_fallback = (target_entity != nullptr);

		// Rate-limited: this runs every worker pass, and an ungated log here
		// would bury everything else.
		static ULONGLONG s_last_log = 0;
		const ULONGLONG now = GetTickCount64();
		if (now - s_last_log > 1000)
		{
			s_last_log = now;
			logger::log(std::string("[tbot] through-walls: target=") +
			            (target_entity ? "found" : "none") +
			            " | entities=" + std::to_string(g_last_scan_total) +
			            " alive=" + std::to_string(g_last_scan_alive) +
			            " hit_dist=" + std::to_string(g_last_hit_dist) +
			            " | reach=" + std::to_string(globals::triggerbot_through_walls_range) +
			            " expand=" + std::to_string(globals::triggerbot_through_walls_expand) +
			            " hook=" + (ensure_attack_hook() ? "up" : "DOWN"));
		}
	}

	if (!target_entity)
	{
		cleanup_refs(env, minecraft, crosshair_target, nullptr);
		return;
	}

	jobject local_player = sdk::instance->get_player();
	if (local_player)
	{
		sdk::entity_client entity_client(target_entity);
		if (entity_client.is_same_object(local_player))
		{
			env->DeleteLocalRef(local_player);
			cleanup_refs(env, minecraft, crosshair_target, target_entity);
			return;
		}
		env->DeleteLocalRef(local_player);
	}

	if (globals::triggerbot_check_shield)
	{
		bool target_blocking = is_entity_blocking(target_entity);
		if (target_blocking)
		{
			if (globals::triggerbot_shield_action == 0)
			{
				cleanup_refs(env, minecraft, crosshair_target, target_entity);
				return;
			}
		}
	}

	// Check if weapon only mode is enabled
	if (globals::triggerbot_weapon_only && !is_holding_weapon())
	{
		cleanup_refs(env, minecraft, crosshair_target, target_entity);
		return;
	}

	// CRIT MODE CHECK - Different crit behaviors
	if (globals::triggerbot_crit_mode > 0)
	{
		jobject local_player_crit = sdk::instance->get_player();
		if (local_player_crit)
		{
			sdk::entity_client local_entity(local_player_crit);
			bool is_on_ground = local_entity.is_on_ground();
			
			// Get Y velocity to determine if falling
			double y_velocity = 0.0;
			bool has_velocity = false;
			jobject velocity = local_entity.get_velocity();
			if (velocity)
			{
				jclass vec3d_class = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
				if (vec3d_class)
				{
					jfieldID y_fid = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
					if (y_fid)
					{
						y_velocity = env->GetDoubleField(velocity, y_fid);
						if (env->ExceptionCheck()) env->ExceptionClear();
						has_velocity = true;
					}
					env->DeleteLocalRef(vec3d_class);
				}
				env->DeleteLocalRef(velocity);
			}
			
			bool is_falling = false;
			if (has_velocity)
			{
				// Falling = not on ground AND negative Y velocity
				is_falling = !is_on_ground && (y_velocity < -0.1);
			}
			else
			{
				// Fallback: just check if not on ground
				is_falling = !is_on_ground;
			}
			
			bool is_in_air = !is_on_ground;
			bool is_going_up = has_velocity && (y_velocity > 0.1);
			
			env->DeleteLocalRef(local_player_crit);
			
			// Mode 1 = Crit Only: Only attack when falling
			if (globals::triggerbot_crit_mode == 1)
			{
				if (!is_falling)
				{
					cleanup_refs(env, minecraft, crosshair_target, target_entity);
					return;
				}
			}
			// Mode 2 = Priority Crit: Wait if in air but not falling, only attack when falling
			else if (globals::triggerbot_crit_mode == 2)
			{
				// If in air but going up (knocked into air), wait until falling
				if (is_in_air && is_going_up)
				{
					cleanup_refs(env, minecraft, crosshair_target, target_entity);
					return;
				}
				// Only attack when actually falling (negative Y velocity)
				if (!is_falling)
				{
					cleanup_refs(env, minecraft, crosshair_target, target_entity);
					return;
				}
			}
		}
		else
		{
			cleanup_refs(env, minecraft, crosshair_target, target_entity);
			return;
		}
	}

	player_client player;
	float cooldown_progress = player.get_attack_cooldown_progress(0.5f);
	bool cooldown_ready = (cooldown_progress >= 1.0f);

	if (globals::triggerbot_hit_select && !cooldown_ready)
	{
		cleanup_refs(env, minecraft, crosshair_target, target_entity);
		return;
	}

	ULONGLONG current_time = GetTickCount64();
	ULONGLONG time_since_last_attack = current_time - last_attack_time;

	bool should_attack = false;
	int required_delay = 0;

	switch (globals::triggerbot_mode)
	{
		case 0:
			required_delay = globals::triggerbot_delay_ms;
			should_attack = (time_since_last_attack >= static_cast<ULONGLONG>(required_delay));
			break;

		case 1:
			if (cooldown_ready && time_since_last_attack >= 50)
			{
				should_attack = true;
			}
			break;

		case 2:
			{
				if (current_delay == 0)
				{
					current_delay = get_random_delay(globals::triggerbot_min_delay_ms, globals::triggerbot_max_delay_ms);
				}
				
				if (time_since_last_attack >= static_cast<ULONGLONG>(current_delay))
				{
					if (!globals::triggerbot_hit_select || cooldown_ready)
					{
						should_attack = true;
						current_delay = get_random_delay(globals::triggerbot_min_delay_ms, globals::triggerbot_max_delay_ms);
					}
				}
			}
			break;

		default:
			required_delay = globals::triggerbot_delay_ms;
			should_attack = (time_since_last_attack >= static_cast<ULONGLONG>(required_delay));
			break;
	}

	// Says why a through-walls target did or did not turn into an attack. If
	// this line never appears while target=found does, the attack was stopped
	// by one of the earlier returns above (crit mode, shield, weapon-only)
	// rather than by the timing gate here.
	if (target_from_fallback)
	{
		static ULONGLONG s_last_gate_log = 0;
		if (current_time - s_last_gate_log > 1000)
		{
			s_last_gate_log = current_time;
			logger::log(std::string("[tbot] gate: attack=") + (should_attack ? "yes" : "no") +
			            " mode=" + std::to_string(globals::triggerbot_mode) +
			            " cooldown=" + std::to_string(cooldown_progress) +
			            " hit_select=" + (globals::triggerbot_hit_select ? "on" : "off") +
			            " since_last=" + std::to_string(time_since_last_attack) + "ms");
		}
	}

	if (should_attack)
	{
		// Sprint reset MUST run before the attack — otherwise the attack packet
		// reaches the server while it still thinks we're sprinting (no crit).
		sprint_reset_on_hit();

		// A synthetic left click is resolved by the game against its own
		// crosshairTarget, and on the through-walls path that is the block in
		// the way — the click would start mining it and never touch the target.
		// So that path attacks the entity directly instead, queued for the JVM
		// tick thread right after the look packet goes out. If the hook could
		// not be attached there is no such path, and a click is still better
		// than doing nothing at all: it degrades to today's behaviour, which
		// only lands when nothing is actually covering the target.
		if (target_from_fallback && ensure_attack_hook())
		{
			enhance::modules::silent_rotation_hook::queue_attack(
				target_entity, enhance::modules::silent_rotation_hook::attack_owner::triggerbot);
			logger::log("[tbot] fired through-walls attack (queued for tick thread)");
		}
		else if (target_from_fallback)
		{
			// Deliberately no click. Vanilla resolves a left click against
			// crosshairTarget, which on this path is the block in the way, so
			// clicking would start mining it -- worse than doing nothing, and
			// not what anyone asked for. Skip the attack and say why.
			static ULONGLONG s_last_warn = 0;
			const ULONGLONG now_warn = GetTickCount64();
			if (now_warn - s_last_warn > 5000)
			{
				s_last_warn = now_warn;
				logger::log_error("[tbot] through-walls target found but the attack hook is down "
				                  "-- skipping, a click would mine the block instead");
			}
		}
		else
		{
			send_click();
		}

		last_attack_time = current_time;

		enhance::modules::stap::on_hit();

		enhance::modules::wtap::on_hit();

		enhance::modules::autojumpreset::on_hit();

		handle_shield_use();
		
		if (last_targeted_entity)
			env->DeleteGlobalRef(last_targeted_entity);
		last_targeted_entity = env->NewGlobalRef(target_entity);
	}

	cleanup_refs(env, minecraft, crosshair_target, target_entity);
}

void enhance::modules::triggerbot::cleanup()
{
	auto env = enhance::instance->get_env();
	if (env && last_targeted_entity)
	{
		env->DeleteGlobalRef(last_targeted_entity);
		last_targeted_entity = nullptr;
	}
	last_attack_time = 0;
	current_delay = 0;
}

void enhance::modules::triggerbot::cleanup_refs(JNIEnv* env, jobject minecraft, jobject crosshair, jobject entity)
{
	if (env)
	{
		if (entity) env->DeleteLocalRef(entity);
		if (crosshair) env->DeleteLocalRef(crosshair);
		if (minecraft) env->DeleteLocalRef(minecraft);
	}
}

bool enhance::modules::triggerbot::is_entity_hit_result(jobject hit_result)
{
	if (!hit_result) 
		return false;

	auto env = enhance::instance->get_env();
	if (!env) 
		return false;

	// Use mappings from mappings.hpp
	if (sdk::mappings::have(sdk::mappings::entity_hit_result_class_sig))
	{
		jclass entity_hit_result_class = sdk::classloader::find_class(env, sdk::mappings::entity_hit_result_class_sig);
		if (entity_hit_result_class)
		{
			jboolean is_entity = env->IsInstanceOf(hit_result, entity_hit_result_class);
			env->DeleteLocalRef(entity_hit_result_class);
			return is_entity == JNI_TRUE;
		}
	}
	
	// Fallback if mappings not available
		return false;
}

jobject enhance::modules::triggerbot::get_entity_from_hit_result(jobject hit_result)
{
	if (!hit_result) 
		return nullptr;

	auto env = enhance::instance->get_env();
	if (!env) 
		return nullptr;

	// Use mappings from mappings.hpp
	if (sdk::mappings::have(sdk::mappings::entity_hit_result_class_sig) && sdk::mappings::have(sdk::mappings::entity_hit_result_get_entity_name) && sdk::mappings::have(sdk::mappings::entity_hit_result_get_entity_sig))
	{
		jclass hit_result_class = sdk::classloader::find_class(env, sdk::mappings::entity_hit_result_class_sig);
	if (!hit_result_class) 
		return nullptr;

		// Try as method first
		jmethodID mid = env->GetMethodID(hit_result_class, sdk::mappings::entity_hit_result_get_entity_name, sdk::mappings::entity_hit_result_get_entity_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		
		if (mid)
	{
			jobject entity = env->CallObjectMethod(hit_result, mid);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(hit_result_class);
			return entity;
		}
		
		// Try as field if method failed
		jfieldID fid = env->GetFieldID(hit_result_class, sdk::mappings::entity_hit_result_get_entity_name, sdk::mappings::entity_hit_result_get_entity_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		
		if (fid)
		{
			jobject entity = env->GetObjectField(hit_result, fid);
			if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(hit_result_class);
		return entity;
	}

	env->DeleteLocalRef(hit_result_class);
	}

	// Fallback if mappings not available
	return nullptr;
}
