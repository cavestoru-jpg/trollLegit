#include "autoclicker.h"
#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../hooks/Hook.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <windows.h>
#include <random>
#include <chrono>

// Auto-clicker: independent CPS schedulers for LMB and RMB. Each side has
// its own [min..max] CPS range; the next click delay is sampled fresh after
// every click so the rhythm doesn't look perfectly periodic. RMB has an
// optional "blocks only" gate — clicks are suppressed unless the crosshair
// is currently on a block (avoids spamming right-click on the air / on
// entities, which would also fire BadPacket-style flags). Each side can
// also be set to fire only while the corresponding mouse button is
// physically held by the user (hold-passthrough).

namespace enhance::modules::autoclicker
{

static std::mt19937 g_rng{ std::random_device{}() };

// Per-side scheduler state. Stored as the absolute tick-count at which the
// next click is allowed (in milliseconds, from GetTickCount64). Sampled
// fresh after each click.
static uint64_t s_next_lmb_ms = 0;
static uint64_t s_next_rmb_ms = 0;
static bool     s_rmb_hold_active = false;   // are we currently holding RMB down?

// Sample next click delay in milliseconds given a [min..max] CPS range.
// Adds a small ±20% jitter on top of the random CPS pick so back-to-back
// clicks aren't identically spaced even if min==max.
static uint64_t sample_delay_ms(int min_cps, int max_cps)
{
	if (min_cps < 1) min_cps = 1;
	if (max_cps < min_cps) max_cps = min_cps;
	std::uniform_int_distribution<int> pick(min_cps, max_cps);
	const int cps = pick(g_rng);
	const double base = 1000.0 / static_cast<double>(cps);
	// ±20% jitter
	std::uniform_real_distribution<double> jit(0.80, 1.20);
	const double ms = base * jit(g_rng);
	return static_cast<uint64_t>(ms < 1.0 ? 1.0 : ms);
}

static bool window_focused(HWND& out_window)
{
	HWND wnd = Hook::get_window();
	if (!wnd) return false;
	if (GetForegroundWindow() != wnd) return false;
	out_window = wnd;
	return true;
}

static void send_left_click(HWND window)
{
	POINT p{}; GetCursorPos(&p); ScreenToClient(window, &p);
	const LPARAM lp = MAKELPARAM(p.x, p.y);
	PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, lp);
	PostMessageA(window, WM_LBUTTONUP,   MK_LBUTTON, lp);
}

static void send_right_click(HWND window)
{
	POINT p{}; GetCursorPos(&p); ScreenToClient(window, &p);
	const LPARAM lp = MAKELPARAM(p.x, p.y);
	PostMessageA(window, WM_RBUTTONDOWN, MK_RBUTTON, lp);
	PostMessageA(window, WM_RBUTTONUP,   MK_RBUTTON, lp);
}

// True if the item currently in the player's main hand is a BlockItem
// (a placeable block — uses class_1747). Tools, weapons, food, buckets,
// etc. all fail this check. Used as the RMB "blocks only" gate so we
// don't right-click-spam things like ender pearls or food.
static bool main_hand_is_block()
{
	if (!sdk::instance) return false;
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return false;

	jobject player = sdk::instance->get_player();
	if (!player) return false;

	bool ok = false;
	do
	{
		jclass player_cls = env->GetObjectClass(player);
		if (!player_cls) break;

		jfieldID inv_fid = env->GetFieldID(player_cls,
			sdk::mappings::player_inventory_name,
			sdk::mappings::player_inventory_sig);
		jobject inventory = inv_fid ? env->GetObjectField(player, inv_fid) : nullptr;
		env->DeleteLocalRef(player_cls);
		if (!inventory) break;

		jclass inv_cls = env->GetObjectClass(inventory);
		if (!inv_cls) { env->DeleteLocalRef(inventory); break; }

		jfieldID slot_fid = env->GetFieldID(inv_cls,
			sdk::mappings::inventory_selected_slot_name,
			sdk::mappings::inventory_selected_slot_sig);
		jmethodID get_stack_mid = env->GetMethodID(inv_cls,
			sdk::mappings::inventory_get_stack_name,
			sdk::mappings::inventory_get_stack_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();

		if (!slot_fid || !get_stack_mid)
		{
			env->DeleteLocalRef(inv_cls);
			env->DeleteLocalRef(inventory);
			break;
		}

		jint slot = env->GetIntField(inventory, slot_fid);
		jobject stack = env->CallObjectMethod(inventory, get_stack_mid, slot);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(inv_cls);
		env->DeleteLocalRef(inventory);
		if (!stack) break;

		jclass stack_cls = env->GetObjectClass(stack);
		if (!stack_cls) { env->DeleteLocalRef(stack); break; }

		jmethodID is_empty_mid = env->GetMethodID(stack_cls,
			sdk::mappings::itemstack_is_empty_name,
			sdk::mappings::itemstack_is_empty_sig);
		jmethodID get_item_mid  = env->GetMethodID(stack_cls,
			sdk::mappings::itemstack_get_item_name,
			sdk::mappings::itemstack_get_item_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();

		if (is_empty_mid)
		{
			jboolean empty = env->CallBooleanMethod(stack, is_empty_mid);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (empty)
			{
				env->DeleteLocalRef(stack_cls);
				env->DeleteLocalRef(stack);
				break;
			}
		}

		if (!get_item_mid)
		{
			env->DeleteLocalRef(stack_cls);
			env->DeleteLocalRef(stack);
			break;
		}

		jobject item = env->CallObjectMethod(stack, get_item_mid);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(stack_cls);
		env->DeleteLocalRef(stack);
		if (!item) break;

		jclass block_item_cls = sdk::classloader::find_class(env,
			sdk::mappings::block_item_class_sig);
		if (block_item_cls)
		{
			ok = env->IsInstanceOf(item, block_item_cls) == JNI_TRUE;
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(block_item_cls);
		}
		env->DeleteLocalRef(item);
	} while (false);

	env->DeleteLocalRef(player);
	return ok;
}

static bool key_held(int vk)
{
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// True if the player's crosshair currently lands on a block (not air, not
// entity). Used to suppress LMB clicks while mining so the user's natural
// hold can break the block — each synthetic click would reset block-break
// progress otherwise.
static bool crosshair_on_block()
{
	if (!sdk::instance) return false;
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return false;

	jobject hit = sdk::instance->get_crosshair_target();
	if (!hit) return false;

	bool is_block = false;
	jclass bhr_cls = sdk::classloader::find_class(env, sdk::mappings::block_hit_result_class_sig);
	if (bhr_cls)
	{
		is_block = env->IsInstanceOf(hit, bhr_cls) == JNI_TRUE;
		if (env->ExceptionCheck()) env->ExceptionClear();

		// BlockHitResult can also represent MISS (air) in some cases — read
		// the enum ordinal to confirm type == BLOCK (1) and not MISS (0).
		if (is_block)
		{
			jmethodID type_mid = env->GetMethodID(bhr_cls,
				sdk::mappings::block_hit_result_get_type_name,
				sdk::mappings::block_hit_result_get_type_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (type_mid)
			{
				jobject type_obj = env->CallObjectMethod(hit, type_mid);
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (type_obj)
				{
					jclass enum_cls = env->GetObjectClass(type_obj);
					if (enum_cls)
					{
						jmethodID ord_mid = env->GetMethodID(enum_cls, "ordinal", "()I");
						if (ord_mid)
						{
							jint ord = env->CallIntMethod(type_obj, ord_mid);
							if (env->ExceptionCheck()) env->ExceptionClear();
							if (ord != 1) is_block = false;
						}
						env->DeleteLocalRef(enum_cls);
					}
					env->DeleteLocalRef(type_obj);
				}
				else
				{
					is_block = false;
				}
			}
		}
		env->DeleteLocalRef(bhr_cls);
	}
	env->DeleteLocalRef(hit);
	return is_block;
}

void run()
{
	if (!globals::autoclicker_enabled) return;

	const bool lmb_on = globals::autoclicker_lmb_enabled;
	const bool rmb_on = globals::autoclicker_rmb_enabled;
	if (!lmb_on && !rmb_on)
	{
		// Make sure we drop any held RMB when the user disables the module
		// mid-hold.
		if (s_rmb_hold_active)
		{
			HWND wnd = Hook::get_window();
			if (wnd)
			{
				POINT p{}; GetCursorPos(&p); ScreenToClient(wnd, &p);
				PostMessageA(wnd, WM_RBUTTONUP, MK_RBUTTON, MAKELPARAM(p.x, p.y));
			}
			s_rmb_hold_active = false;
		}
		return;
	}

	// Module enable check (combine into a single keybind that toggles both
	// sides as a unit — matches enhance's per-module enable model). When
	// neither side is enabled, the keybind is irrelevant.
	if (lmb_on || rmb_on)
	{
		// If a keybind is set, gate the whole module on it using the same
		// Hold/Toggle/Always semantics as the rest of enhance.
		if (globals::autoclicker_keybind != 0)
		{
			static bool s_last_key = false;
			static bool s_toggled  = false;
			const bool key_now = (GetAsyncKeyState(globals::autoclicker_keybind) & 0x8000) != 0;
			const bool pressed = key_now && !s_last_key;
			s_last_key = key_now;

			bool active = false;
			switch (globals::autoclicker_keybind_mode)
			{
				case 0: active = key_now; break;                        // Hold
				case 1: if (pressed) s_toggled = !s_toggled; active = s_toggled; break;
				case 2:
				default: active = true; break;                          // Always
			}
			if (!active) return;
		}
	}

	if (globals::show_gui) return;

	HWND window;
	if (!window_focused(window)) return;

	const uint64_t now = GetTickCount64();

	// ----- LMB ---------------------------------------------------------
	if (lmb_on)
	{
		// "Only while held" gate. If enabled, we only fire while the user
		// is physically holding LMB. We DON'T re-send the down event each
		// click — we just shape the natural click stream the user is
		// already producing into the configured CPS range.
		const bool gate_ok = !globals::autoclicker_lmb_hold_only
		                  || key_held(VK_LBUTTON);
		if (gate_ok && now >= s_next_lmb_ms)
		{
			// Skip the synthetic click if the crosshair is on a block: the
			// user is presumably mining, and each click would reset block-
			// break progress. We still advance the next-tick timer so we
			// don't busy-loop the JNI check.
			const bool block_skip = globals::autoclicker_lmb_skip_on_block
			                     && crosshair_on_block();
			if (!block_skip) send_left_click(window);
			s_next_lmb_ms = now + sample_delay_ms(
				globals::autoclicker_lmb_min_cps,
				globals::autoclicker_lmb_max_cps);
		}
	}

	// ----- RMB ---------------------------------------------------------
	if (rmb_on)
	{
		const bool hold_gate = !globals::autoclicker_rmb_hold_only
		                    || key_held(VK_RBUTTON);
		const bool blocks_ok = !globals::autoclicker_rmb_blocks_only
		                    || main_hand_is_block();

		if (hold_gate && blocks_ok && now >= s_next_rmb_ms)
		{
			send_right_click(window);
			s_next_rmb_ms = now + sample_delay_ms(
				globals::autoclicker_rmb_min_cps,
				globals::autoclicker_rmb_max_cps);
		}
	}
}

}
