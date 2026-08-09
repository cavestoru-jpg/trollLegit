#include "auto_anchor.h"
#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../hooks/Hook.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/player/player.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <cstring>
#include <windows.h>

// ---------- inventory helpers (mirror anchor_macro) ----------

static bool aa_is_item(jobject item_stack, const char* item_name)
{
	if (!item_stack) return false;
	auto env = enhance::instance->get_env();
	if (!env) return false;

	jclass itemstack_class = env->GetObjectClass(item_stack);
	if (!itemstack_class) return false;

	jmethodID get_item_mid = env->GetMethodID(itemstack_class,
		sdk::mappings::itemstack_get_item_name, sdk::mappings::itemstack_get_item_sig);
	if (!get_item_mid) { env->DeleteLocalRef(itemstack_class); return false; }

	jobject item = env->CallObjectMethod(item_stack, get_item_mid);
	env->DeleteLocalRef(itemstack_class);
	if (!item) return false;

	jclass item_class = env->GetObjectClass(item);
	if (!item_class) { env->DeleteLocalRef(item); return false; }

	jmethodID get_key_mid = env->GetMethodID(item_class,
		sdk::mappings::item_get_translation_key_name, sdk::mappings::item_get_translation_key_sig);
	if (!get_key_mid) { env->DeleteLocalRef(item_class); env->DeleteLocalRef(item); return false; }

	jstring key = (jstring)env->CallObjectMethod(item, get_key_mid);
	env->DeleteLocalRef(item_class);
	env->DeleteLocalRef(item);
	if (!key) return false;

	const char* key_cstr = env->GetStringUTFChars(key, nullptr);
	if (!key_cstr) { env->DeleteLocalRef(key); return false; }

	bool matches = (strstr(key_cstr, item_name) != nullptr);
	env->ReleaseStringUTFChars(key, key_cstr);
	env->DeleteLocalRef(key);
	return matches;
}

static int aa_find_item_in_hotbar(jobject player, const char* item_name)
{
	auto env = enhance::instance->get_env();
	if (!env || !player) return -1;

	jclass player_class = env->GetObjectClass(player);
	if (!player_class) return -1;

	jfieldID inv_fid = env->GetFieldID(player_class,
		sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig);
	if (!inv_fid) { env->DeleteLocalRef(player_class); return -1; }

	jobject inventory = env->GetObjectField(player, inv_fid);
	env->DeleteLocalRef(player_class);
	if (!inventory) return -1;

	jclass inv_class = env->GetObjectClass(inventory);
	if (!inv_class) { env->DeleteLocalRef(inventory); return -1; }

	jmethodID get_stack_mid = env->GetMethodID(inv_class,
		sdk::mappings::inventory_get_stack_name, sdk::mappings::inventory_get_stack_sig);
	if (!get_stack_mid) { env->DeleteLocalRef(inv_class); env->DeleteLocalRef(inventory); return -1; }

	for (int slot = 0; slot < 9; slot++)
	{
		jobject stack = env->CallObjectMethod(inventory, get_stack_mid, slot);
		if (!stack) continue;

		jclass stack_class = env->GetObjectClass(stack);
		jmethodID is_empty_mid = stack_class ? env->GetMethodID(stack_class,
			sdk::mappings::itemstack_is_empty_name, sdk::mappings::itemstack_is_empty_sig) : nullptr;
		if (stack_class) env->DeleteLocalRef(stack_class);

		if (is_empty_mid)
		{
			jboolean empty = env->CallBooleanMethod(stack, is_empty_mid);
			if (empty == JNI_FALSE && aa_is_item(stack, item_name))
			{
				env->DeleteLocalRef(stack);
				env->DeleteLocalRef(inv_class);
				env->DeleteLocalRef(inventory);
				return slot;
			}
		}
		env->DeleteLocalRef(stack);
	}

	env->DeleteLocalRef(inv_class);
	env->DeleteLocalRef(inventory);
	return -1;
}

static int aa_get_current_slot(jobject player)
{
	auto env = enhance::instance->get_env();
	if (!env || !player) return -1;

	jclass player_class = env->GetObjectClass(player);
	if (!player_class) return -1;

	jfieldID inv_fid = env->GetFieldID(player_class,
		sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig);
	if (!inv_fid) { env->DeleteLocalRef(player_class); return -1; }

	jobject inventory = env->GetObjectField(player, inv_fid);
	env->DeleteLocalRef(player_class);
	if (!inventory) return -1;

	jclass inv_class = env->GetObjectClass(inventory);
	if (!inv_class) { env->DeleteLocalRef(inventory); return -1; }

	jfieldID slot_fid = env->GetFieldID(inv_class,
		sdk::mappings::inventory_selected_slot_name, sdk::mappings::inventory_selected_slot_sig);
	if (!slot_fid)
	{
		env->DeleteLocalRef(inv_class);
		env->DeleteLocalRef(inventory);
		return -1;
	}

	int slot = env->GetIntField(inventory, slot_fid);
	env->DeleteLocalRef(inv_class);
	env->DeleteLocalRef(inventory);
	return slot;
}

// ---------- click helpers ----------

static void aa_send_right_click()
{
	INPUT inputs[2] = {};
	inputs[0].type = INPUT_MOUSE; inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
	inputs[1].type = INPUT_MOUSE; inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
	SendInput(1, &inputs[0], sizeof(INPUT));
	Sleep(20);
	SendInput(1, &inputs[1], sizeof(INPUT));
}

// ---------- crosshair / anchor detection ----------

// Resolves the block position the player is currently looking at.
// Returns true and fills x/y/z if the crosshair is on a block; false otherwise.
static bool aa_crosshair_block_pos(int& x, int& y, int& z)
{
	auto env = enhance::instance->get_env();
	if (!env) return false;

	jobject hit = sdk::instance->get_crosshair_target();
	if (!hit) return false;

	jclass bhr_class = sdk::classloader::find_class(env, sdk::mappings::block_hit_result_class_sig);
	if (!bhr_class) { env->DeleteLocalRef(hit); return false; }

	jboolean is_block = env->IsInstanceOf(hit, bhr_class);
	if (is_block != JNI_TRUE)
	{
		env->DeleteLocalRef(bhr_class);
		env->DeleteLocalRef(hit);
		return false;
	}

	jmethodID get_pos_mid = env->GetMethodID(bhr_class,
		sdk::mappings::block_hit_result_get_block_pos_name,
		sdk::mappings::block_hit_result_get_block_pos_sig);
	env->DeleteLocalRef(bhr_class);
	if (!get_pos_mid) { env->DeleteLocalRef(hit); return false; }

	jobject pos = env->CallObjectMethod(hit, get_pos_mid);
	env->DeleteLocalRef(hit);
	if (!pos) return false;

	jclass pos_class = sdk::classloader::find_class(env, sdk::mappings::block_pos_class_sig);
	if (!pos_class) { env->DeleteLocalRef(pos); return false; }

	jmethodID gx = env->GetMethodID(pos_class, sdk::mappings::block_pos_get_x_name, sdk::mappings::block_pos_get_x_sig);
	jmethodID gy = env->GetMethodID(pos_class, sdk::mappings::block_pos_get_y_name, sdk::mappings::block_pos_get_y_sig);
	jmethodID gz = env->GetMethodID(pos_class, sdk::mappings::block_pos_get_z_name, sdk::mappings::block_pos_get_z_sig);
	env->DeleteLocalRef(pos_class);
	if (!gx || !gy || !gz) { env->DeleteLocalRef(pos); return false; }

	x = env->CallIntMethod(pos, gx);
	y = env->CallIntMethod(pos, gy);
	z = env->CallIntMethod(pos, gz);
	env->DeleteLocalRef(pos);
	return true;
}

// Returns true if the block at the given pos is a respawn anchor.
static bool aa_block_at_is_anchor(int x, int y, int z)
{
	auto env = enhance::instance->get_env();
	if (!env) return false;

	jobject world = sdk::instance->get_world();
	if (!world) return false;

	// new BlockPos(x, y, z) via class_2338 ctor (III)V
	jclass pos_class = sdk::classloader::find_class(env, sdk::mappings::block_pos_class_sig);
	if (!pos_class) { env->DeleteLocalRef(world); return false; }

	jmethodID pos_ctor = env->GetMethodID(pos_class, "<init>", "(III)V");
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!pos_ctor) { env->DeleteLocalRef(pos_class); env->DeleteLocalRef(world); return false; }

	jobject pos = env->NewObject(pos_class, pos_ctor, (jint)x, (jint)y, (jint)z);
	env->DeleteLocalRef(pos_class);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!pos) { env->DeleteLocalRef(world); return false; }

	jclass world_class = env->GetObjectClass(world);
	if (!world_class) { env->DeleteLocalRef(pos); env->DeleteLocalRef(world); return false; }

	jmethodID get_state_mid = env->GetMethodID(world_class,
		sdk::mappings::world_get_block_state_name, sdk::mappings::world_get_block_state_sig);
	env->DeleteLocalRef(world_class);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!get_state_mid) { env->DeleteLocalRef(pos); env->DeleteLocalRef(world); return false; }

	jobject state = env->CallObjectMethod(world, get_state_mid, pos);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(pos);
	env->DeleteLocalRef(world);
	if (!state) return false;

	jclass state_class = env->GetObjectClass(state);
	if (!state_class) { env->DeleteLocalRef(state); return false; }

	jmethodID get_block_mid = env->GetMethodID(state_class,
		sdk::mappings::block_state_get_block_name, sdk::mappings::block_state_get_block_sig);
	env->DeleteLocalRef(state_class);
	if (!get_block_mid) { env->DeleteLocalRef(state); return false; }

	jobject block = env->CallObjectMethod(state, get_block_mid);
	env->DeleteLocalRef(state);
	if (!block) return false;

	jclass anchor_class = sdk::classloader::find_class(env, sdk::mappings::respawn_anchor_block_class_sig);
	if (!anchor_class) { env->DeleteLocalRef(block); return false; }

	jboolean is_anchor = env->IsInstanceOf(block, anchor_class);
	env->DeleteLocalRef(anchor_class);
	env->DeleteLocalRef(block);
	return is_anchor == JNI_TRUE;
}

// True if the player is *right now* aiming at the same anchor we locked onto
// when starting the sequence. The position match is what protects us against
// the "player looked away mid-sequence" anti-cheat case.
static bool aa_still_looking_at_target_anchor()
{
	int x = 0, y = 0, z = 0;
	if (!aa_crosshair_block_pos(x, y, z)) return false;
	if (x != globals::auto_anchor_target_x ||
		y != globals::auto_anchor_target_y ||
		z != globals::auto_anchor_target_z) return false;
	return aa_block_at_is_anchor(x, y, z);
}

// ---------- keybind gating ----------

static bool aa_last_key_state = false;

static bool aa_is_keybind_active()
{
	if (!globals::auto_anchor_enabled) return false;
	if (globals::auto_anchor_keybind == 0) return true; // module enabled with no bind => always active

	bool key_now = (GetAsyncKeyState(globals::auto_anchor_keybind) & 0x8000) != 0;
	bool key_pressed = key_now && !aa_last_key_state;
	aa_last_key_state = key_now;

	switch (globals::auto_anchor_mode)
	{
		case 0: return key_now; // Hold
		case 1: // Toggle
			if (key_pressed) globals::auto_anchor_toggled = !globals::auto_anchor_toggled;
			return globals::auto_anchor_toggled;
		case 2: return true;    // Always (when enabled)
	}
	return false;
}

// ---------- sequence control ----------

static void aa_abort_and_restore(bool swap_back)
{
	if (swap_back && globals::auto_anchor_saved_slot != -1)
	{
		player_client::swap_selected_slot(globals::auto_anchor_saved_slot);
	}
	globals::auto_anchor_executing = false;
	globals::auto_anchor_step = 0;
	globals::auto_anchor_saved_slot = -1;
}

void enhance::modules::auto_anchor::run()
{
	if (!globals::auto_anchor_enabled)
	{
		// Module turned off mid-sequence: best-effort restore so we don't
		// leave the player stuck on glowstone.
		if (globals::auto_anchor_executing) aa_abort_and_restore(true);
		globals::auto_anchor_toggled = false;
		aa_last_key_state = false;
		return;
	}

	if (globals::show_gui)
	{
		aa_last_key_state = false;
		return;
	}

	HWND window = Hook::get_window();
	if (!window) return;
	if (GetForegroundWindow() != window)
	{
		// Player tabbed out — restore and stop so we don't fire blind clicks.
		if (globals::auto_anchor_executing) aa_abort_and_restore(true);
		aa_last_key_state = false;
		return;
	}

	auto env = enhance::instance->get_env();
	if (!env) return;

	ULONGLONG now = GetTickCount64();
	ULONGLONG elapsed = now - globals::auto_anchor_last_action;

	// --- Trigger phase (not currently executing) ---
	if (!globals::auto_anchor_executing)
	{
		if (!aa_is_keybind_active()) return;

		int x = 0, y = 0, z = 0;
		if (!aa_crosshair_block_pos(x, y, z)) return;
		if (!aa_block_at_is_anchor(x, y, z))  return;

		jobject player = sdk::instance->get_player();
		if (!player) return;

		int glow_slot = aa_find_item_in_hotbar(player, "glowstone");
		if (glow_slot == -1)
		{
			env->DeleteLocalRef(player);
			return;
		}

		globals::auto_anchor_saved_slot = aa_get_current_slot(player);
		env->DeleteLocalRef(player);

		// Lock onto this exact block — every subsequent step verifies the
		// crosshair is still on these coordinates before clicking.
		globals::auto_anchor_target_x = x;
		globals::auto_anchor_target_y = y;
		globals::auto_anchor_target_z = z;

		player_client::swap_selected_slot(glow_slot);

		globals::auto_anchor_executing  = true;
		globals::auto_anchor_step       = 1;
		globals::auto_anchor_last_action = now;
		return;
	}

	// --- Execution phase ---
	switch (globals::auto_anchor_step)
	{
		case 1:
		{
			// Wait for the slot-swap to register on the server, then verify
			// we're still aiming at the anchor before charging it.
			if (elapsed < (ULONGLONG)globals::auto_anchor_swap_delay_ms) return;

			if (!aa_still_looking_at_target_anchor())
			{
				// Player looked away — abort silently so we don't right-click
				// some unrelated block (anti-cheat would flag the use-item).
				aa_abort_and_restore(true);
				return;
			}

			aa_send_right_click();
			globals::auto_anchor_step = 2;
			globals::auto_anchor_last_action = now;
			break;
		}
		case 2:
		{
			// Right-click sent; let the charge interaction reach the server,
			// then swap back to the saved slot. The swap itself is a safe
			// non-interactive action so we don't need a gaze check here.
			if (elapsed < (ULONGLONG)globals::auto_anchor_charge_delay_ms) return;

			if (globals::auto_anchor_saved_slot != -1)
			{
				player_client::swap_selected_slot(globals::auto_anchor_saved_slot);
			}
			globals::auto_anchor_step = 3;
			globals::auto_anchor_last_action = now;
			break;
		}
		case 3:
		{
			// Wait for the swap-back to register, re-check gaze, then ignite
			// the anchor with a right-click — left-click would just break the
			// block without detonating it in the Overworld / End.
			if (elapsed < (ULONGLONG)globals::auto_anchor_swap_delay_ms) return;

			if (!aa_still_looking_at_target_anchor())
			{
				// Already swapped back — just stop without faking a click.
				aa_abort_and_restore(false);
				return;
			}

			aa_send_right_click();
			globals::auto_anchor_step = 4;
			globals::auto_anchor_last_action = now;
			break;
		}
		case 4:
		{
			if (elapsed < 100) return;
			globals::auto_anchor_executing = false;
			globals::auto_anchor_step = 0;
			globals::auto_anchor_saved_slot = -1;
			break;
		}
	}
}
