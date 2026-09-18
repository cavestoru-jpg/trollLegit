#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include "../../utils/imgui/imgui.h"
#include <jni.h>

namespace globals
{
	inline bool show_gui = false;
	inline bool flight_enabled = false;
	inline HWND mc_window = nullptr;

	// GUI hotkey (default Right Shift) and toggle mode (1=Toggle).
	inline int gui_keybind = VK_RSHIFT;
	inline int gui_keybind_mode = 1;

	// Unload hotkey (default End). Detaches every hook, restores what the
	// client changed and frees the DLL, so a new build can be injected without
	// restarting Minecraft.
	inline int unload_keybind = VK_END;

	// Superseded by unload_level (4 unmaps). Kept only so an existing config
	// file carrying this key still loads without complaint.
	inline bool unload_free_library = false;

	// How far the unload hotkey goes. 4 is the full, working teardown; the lower
	// levels are kept because bisecting this way is what found the crash that
	// four rounds of reasoning about it did not — a stage that "does nothing"
	// still failing is what pointed outside the staged code entirely.
	//
	//   0 - stop the worker only; every hook stays installed and live
	//   1 - + tear down ImGui and the client's own GL context (render thread)
	//   2 - + remove the MinHook trampoline and restore the window procedure
	//   3 - + detach the JNI hooks and release the JVMTI environments
	//   4 - + unmap the module
	inline int unload_level = 4;
	// 0=Dark, 1=Darker, 2=Void, 3=Light Blue Glass (default).
	inline int gui_theme_id = 3;
	
	inline bool hitbox_enabled = false;
	inline double hitbox_expand_width = 0.3;
	inline double hitbox_expand_height = 0.0;
	inline int hitbox_keybind = 0;
	inline int hitbox_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	
	inline bool sprint_enabled = false;
	inline bool triggerbot_enabled = false;
	inline int triggerbot_mode = 0; // 0 = Custom Delay, 1 = Weapon Cooldown, 2 = Combo
	inline int triggerbot_delay_ms = 50;
	inline int triggerbot_min_delay_ms = 40;
	inline int triggerbot_max_delay_ms = 80;
	inline int triggerbot_crit_mode = 0; // 0 = Off, 1 = Crit Only, 2 = Priority Crit
	inline bool triggerbot_hit_select = true; // Only hit when cooldown is ready
	inline int triggerbot_keybind = 0;
	inline int triggerbot_keybind_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline bool triggerbot_weapon_only = false; // Only trigger when holding weapon
	// MC's crosshair raycast stops at the first block, so a covered target never
	// shows up as an entity hit. With this on the module picks the target itself.
	inline bool triggerbot_through_walls = false;
	// Its own reach, not the Reach module's. Borrowing that one meant the
	// through-walls scan ran at 3 blocks by default and rejected every target
	// on distance alone, and it applied even with the Reach module switched
	// off.
	inline float triggerbot_through_walls_range = 5.0f;
	// How far each hitbox is grown before the ray is tested against it, in
	// blocks. Vanilla does the same when picking an entity; a little margin is
	// what makes aiming feel forgiving rather than pixel-exact.
	inline float triggerbot_through_walls_expand = 0.15f;

	inline bool reach_enabled = false;
	inline double reach_distance = 3.0;
	inline int reach_keybind = 0;
	inline int reach_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	
	inline bool aimassist_enabled = false;
	inline float aimassist_smoothing = 0.3f;
	inline double aimassist_max_distance = 10.0;
	inline int aimassist_keybind = 0;
	inline int aimassist_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline bool aimassist_horizontal = true; // Yaw aiming
	inline bool aimassist_vertical = true; // Pitch aiming
	
	inline int pearl_catch_keybind = 0;
	inline int pearl_catch_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline int pearl_catch_aim_mode = 0; // 0 = Silent (Detected), 1 = Visible
	
	inline bool box_enabled = false;
	inline int esp_keybind = 0;
	inline int esp_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline bool esp_health_bar = false;
	// Box style: 0 = 2D bounds, 1 = corner brackets, 2 = true 3D box (12 edges)
	inline int    esp_box_style = 0;
	inline bool   esp_box_filled = false;
	inline bool   esp_tracers = false;
	// Fades distant players out instead of drawing every box at full alpha.
	inline bool   esp_distance_fade = true;
	inline float  esp_fade_start = 24.0f;   // blocks — fully opaque up to here
	inline float  esp_fade_end   = 96.0f;   // blocks — invisible past here
	// ----- Name tags -------------------------------------------------------
	// Ported design: an anchor 0.55 blocks above the entity's bounding box,
	// carrying the name and a health readout. Sizes and the distance falloff
	// follow the reference formulas exactly.
	//
	// The reference's team prefix is NOT here either — it needs the scoreboard
	// team API, same as the vanilla hiding below.
	//
	// Item icons from the reference are NOT here: drawing real item textures
	// needs Minecraft's 1.21.9+ GUI item atlas (ItemModelResolver ->
	// GuiItemAtlas -> GpuTextureView -> raw GL id), which is a project of its
	// own and is entirely unmapped on this build.
	inline bool   nametags_enabled = false;
	inline float  nametags_size = 0.35f;       // reference default
	inline float  nametags_alpha = 1.0f;
	inline float  nametags_range = 64.0f;
	inline bool   nametags_static_size = false;
	inline bool   nametags_show_friends = true;
	inline bool   nametags_show_health = true;
	inline bool   nametags_show_distance = false;
	// 0 = Health (12.5), 1 = Hearts (6/10), 2 = Percent (62%)
	inline int    nametags_health_mode = 0;
	inline ImVec4 nametags_friend_color = ImVec4(0.25f, 0.85f, 0.45f, 1.0f);
	// Hide Minecraft's own floating names so ours is the only one on screen.
	inline bool   nametags_hide_vanilla = false;
	// Draw the tags as world geometry through the in-world renderer instead of
	// as a flat overlay. Costs the crispness of screen-aligned text and gains
	// depth: a tag behind terrain is occluded by it. Ignored unless the
	// in-world renderer actually attached — otherwise ticking this would just
	// make the tags disappear.
	inline bool   nametags_in_world = false;
	inline ImVec4 esp_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	// In-world 3D boxes: real geometry submitted from inside Minecraft's world
	// render pass by a Java renderer defined into the JVM at runtime. Because
	// it runs while the game's framebuffer is bound, its depth attachment is
	// live — so "through walls" off gives genuine occlusion, and the HUD draws
	// on top instead of underneath.
	inline bool esp_world_render_enabled = false;
	inline bool esp_world_through_walls = true;
	inline bool esp_world_filled = true;
	
	inline bool mace_enabled = false;
	inline bool mace_look = false;
	inline bool mace_switch_back = false;
	inline bool mace_remove_elytra = false;
	inline double mace_min_fall_distance = 3.0;
	inline double mace_height_above_target = 0.0;
	inline double mace_fall_hitbox_width = 0.3;
	inline double mace_fall_hitbox_height = 0.0;
	inline int mace_keybind = 0;
	inline int mace_keybind_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	
	// Mace state tracking
	inline int mace_saved_slot = -1;
	inline ULONGLONG mace_last_attack = 0;
	inline bool mace_elytra_swapped_this_fall = false;
	inline bool mace_was_in_fall_distance = false;
	
	// Shield breaker
	inline bool shield_breaker_enabled = false;
	inline bool shield_breaker_aim = false;
	inline bool shield_breaker_switch_back = false;
	inline int shield_breaker_saved_slot = -1;
	inline ULONGLONG shield_breaker_last_attack = 0;
	inline int shield_breaker_delay_ms = 0;
	inline int shield_breaker_keybind = 0;
	inline int shield_breaker_keybind_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	
	// Aimassist
	inline bool aimassist_use_mouse_input = false;
	inline float aimassist_mouse_sensitivity = 0.5f; // Mouse sensitivity multiplier
	
	// Triggerbot Shield Use (use shield after tbot hit)
	inline bool triggerbot_use_shield = false;
	inline int triggerbot_shield_duration_ms = 300; // How long to hold shield
	inline ULONGLONG triggerbot_shield_start_time = 0;
	inline bool triggerbot_shield_holding = false; // Are we currently holding shield
	
	// Triggerbot Shield Check (check if target is using shield)
	inline bool triggerbot_check_shield = false;
	inline int triggerbot_shield_action = 0; // 0 = Don't click, 1 = Spam click

	// Triggerbot built-in sprint reset (releases W for N ticks after each hit
	// to break sprint, allowing crits, then re-presses W to resume sprinting).
	inline bool triggerbot_sprint_reset = false;
	inline int  triggerbot_sprint_reset_ticks = 2;            // 1 tick = 50 ms
	inline bool triggerbot_sprint_reset_active = false;
	inline ULONGLONG triggerbot_sprint_reset_start_time = 0;
	
	// Server Rotation / Silent Aim
	inline bool server_rotation_enabled = false;
	inline int server_rotation_mode = 0; // 0 = Look Up, 1 = Look Down, 2 = Look Behind, 3 = Custom
	inline float server_rotation_custom_yaw = 0.0f;
	inline float server_rotation_custom_pitch = -90.0f;
	
	// Stun Slam (shield break + mace combo when falling)
	inline bool stun_slam_enabled = false;
	inline float stun_slam_chance = 100.0f; // Chance percentage
	inline int stun_slam_swap_delay_ms = 10; // Delay for swapping items (fast)
	inline int stun_slam_axe_delay_ms = 20; // Delay after axe hit
	inline int stun_slam_mace_delay_ms = 20; // Delay after mace hit
	inline double stun_slam_min_fall = 1.5; // Lower default for easier triggering
	
	// S-Tap (briefly press S to reset sprint) - uses actual key simulation
	inline bool stap_enabled = false;
	inline int stap_duration_ms = 50; // How long to press S (50-100ms typical)
	inline ULONGLONG stap_start_time = 0;
	inline bool stap_is_active = false;
	
	// W-Tap (briefly release W to reset sprint) - uses actual key simulation
	inline bool wtap_enabled = false;
	inline int wtap_duration_ms = 50; // How long to release W (50-100ms typical)
	inline ULONGLONG wtap_start_time = 0;
	inline bool wtap_is_active = false;
	
	// Auto Anchor — user places the anchor manually, we detect a respawn_anchor
	// at the crosshair, swap to glowstone, right-click once to charge by 1,
	// swap back, then left-click to break (explode). Aborts immediately if the
	// player looks away mid-sequence so we don't fake interactions with random
	// blocks (anti-cheat safety).
	inline bool auto_anchor_enabled = false;
	inline int  auto_anchor_keybind = 0;            // 0 = none, VK otherwise
	inline int  auto_anchor_mode    = 2;            // 0 = Hold, 1 = Toggle, 2 = Always-when-enabled
	inline int  auto_anchor_swap_delay_ms   = 80;   // pause between slot swap and click
	inline int  auto_anchor_charge_delay_ms = 120;  // pause after right-click before swapping back
	inline bool auto_anchor_executing   = false;
	inline bool auto_anchor_toggled     = false;
	inline int  auto_anchor_step        = 0;
	inline ULONGLONG auto_anchor_last_action = 0;
	inline int  auto_anchor_saved_slot  = -1;
	// Locked target block coordinates from the moment we detected the anchor.
	// Re-checked on every interaction step; if the live crosshair no longer
	// matches, we abort to avoid clicking a different block.
	inline int  auto_anchor_target_x = 0;
	inline int  auto_anchor_target_y = 0;
	inline int  auto_anchor_target_z = 0;

	// Anchor Macro (keybind to place, charge, and explode anchor)
	inline bool anchor_macro_enabled = false;
	inline int anchor_macro_keybind = 0; // 0 = none, VK code otherwise
	inline int anchor_macro_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline bool anchor_macro_break_anchor = true; // Whether to break the anchor at the end
	inline int anchor_macro_swap_delay_ms = 80; // Delay for swapping items
	inline int anchor_macro_charge_delay_ms = 120; // Delay before charging
	inline int anchor_macro_break_delay_ms = 80; // Delay before breaking
	inline bool anchor_macro_executing = false;
	inline bool anchor_macro_toggled = false; // For toggle mode
	inline int anchor_macro_step = 0;
	inline ULONGLONG anchor_macro_last_action = 0;
	inline int anchor_macro_saved_slot = -1;
	
	// Storage ESP
	inline bool storage_esp_enabled = false;
	inline int storage_esp_keybind = 0;
	inline int storage_esp_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline bool storage_esp_chest = true;
	inline bool storage_esp_ender_chest = true;
	inline bool storage_esp_shulker = true;
	
	// AutoCrystal
	inline bool autocrystal_enabled = false;
	inline int autocrystal_keybind = 0;
	inline int autocrystal_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline int autocrystal_delay_ms = 50; // Delay between place and break
	inline bool autocrystal_debug_enabled = false; // Debug logging
	
	// Auto Totem
	inline bool autototem_enabled = false;
	inline int autototem_keybind = 0;
	inline int autototem_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline bool autototem_rage_mode = false; // Rage mode (uses different packet sequence)
	
	// Auto Jump Reset
	inline bool autojumpreset_enabled = false;
	inline int autojumpreset_keybind = 0;
	inline int autojumpreset_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline int autojumpreset_cooldown_ms = 100; // Cooldown between jumps
	inline ULONGLONG autojumpreset_last_jump = 0;
	inline float autojumpreset_last_health = 0.0f;
	
	// Backtrack
	inline bool backtrack_enabled = false;
	inline int backtrack_keybind = 0;
	inline int backtrack_mode = 2; // 0 = Hold, 1 = Toggle, 2 = Always
	inline double backtrack_min_distance = 1.0;
	inline double backtrack_max_distance = 4.0;
	inline int backtrack_max_delay_ms = 200; // Maximum delay in milliseconds
	inline float backtrack_cooldown_seconds = 0.0f; // Cooldown before reactivation
	inline int backtrack_max_hurt_time_ms = 0; // Maximum hurt time (i-frames) to activate
	inline bool backtrack_disable_on_hit = false; // Disable when taking knockback
	inline bool backtrack_visualization_enabled = true;
	inline ImVec4 backtrack_visualization_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red by default
	inline float backtrack_visualization_line_width = 1.0f;
	inline bool backtrack_visualization_filled = false; // Outline by default
	inline bool backtrack_visualization_show_head_rotation = false;

	// ----- Killaura (BlazeDLC port) ----------------------------------------
	inline bool killaura_enabled = false;
	inline int  killaura_keybind = 0;
	inline int  killaura_keybind_mode = 2;     // 0=Hold, 1=Toggle, 2=Always

	// Target filter
	inline bool killaura_target_players = true;
	inline bool killaura_target_mobs    = false;
	inline bool killaura_target_animals = false;
	inline bool killaura_target_friends = false;   // include friends in scan?

	// Attack modifiers
	inline bool killaura_only_critical      = false;
	inline bool killaura_smart_critical     = false;
	inline bool killaura_dynamic_cooldown   = false;
	inline bool killaura_break_shield       = false;
	inline bool killaura_unpress_shield     = false;
	inline bool killaura_no_attack_when_eat = true;
	inline bool killaura_ignore_walls       = false;

	// Clicker: killaura fires attacks on its own instead of only rotating.
	// The attack is queued on the JVM tick thread right after the look packet
	// (silent_rotation_hook::queue_attack), keeping the order Look(fake) ->
	// Attack that Grim's PacketOrder check expects. CPS is drawn fresh from
	// [min,max] after every hit so the interval is not a constant.
	inline bool killaura_autoattack       = true;   // swing by itself
	inline int  killaura_min_cps          = 8;
	inline int  killaura_max_cps          = 12;
	inline bool killaura_require_cooldown = true;    // only hit at full attack charge (1.9+)

	// Move correction
	// Disabled by default: with the current silent-rotation architecture
	// (yaw only swapped inside sendMovementPackets, not throughout the
	// tick), travel() runs against real yaw — rotating the input here
	// just makes the player move in an unintended direction without
	// fixing the server-side mismatch. The proper fix would be to extend
	// the silent hook to wrap tickMovement, but for now correction is
	// off by default and the user can toggle it on if they understand
	// the trade-off.
	inline bool killaura_move_correction_enabled = false;
	inline int  killaura_correction_type = 0;   // 0 = Free, 1 = Focused

	// --- Rotation rework, ported from LiquidBounce -----------------------
	// Replaces the correction above. The old one rotated the input vector
	// after KeyboardInput.tick, which 1.21.11 removed (Input now holds an
	// immutable PlayerInput record). This one wraps tickMovement and swaps
	// the yaw itself, so the whole movement chain underneath
	// (travel -> updateVelocity -> getYaw) computes against the rotation we
	// are showing the server.
	//
	// 0 = Off, 1 = Strict, 2 = Silent
	//
	// Off    the body keeps the player's real angle; the look packet still carries
	//        the silent one, so the server computes movement from an angle the
	//        body is not using -- which is the mismatch a rotation check looks for.
	// Strict the whole tick runs on the silent angle, so client and server agree
	//        and the body visibly turns with it.
	// Silent the same agreement, but the movement input is rotated back by the
	//        same delta inside ClientInput.tick, so the direction the player asked
	//        for is the direction they get. Needs writable input -- see
	//        sdk::caps::feature::input_write; the menu falls back to Strict where
	//        it is unavailable.
	inline int aiming_movement_correction = 1;

	// --- Silent rotation, rebuilt from scratch --------------------------
	// Hold a fixed offset from the player's real yaw for the duration of
	// ClientPlayerEntity.tick. Everything the server is told comes from inside
	// that call, so the offset reaches the server while the camera does not
	// move. This is the base the rest is meant to grow from -- a fixed offset
	// is the one form that can be checked without any other machinery being
	// right first.
	// Attach even when JVMTI will not grant can_suspend. That capability is
	// solo per JVM and is not handed back promptly when an environment is
	// disposed, so after a single unload it can stay unavailable until the game
	// restarts -- which otherwise makes every hook impossible to install for
	// the rest of the session. The cost is a microseconds-wide window during
	// the attach in which the hooked method is native with no implementation;
	// a thread calling it right then throws UnsatisfiedLinkError and the game
	// stops. Off by default because that is a crash, not a glitch.
	inline bool  silent_rotation_force_attach = false;

	inline bool  silent_rotation_enabled = false;
	inline float silent_rotation_offset = 90.0f;

	// Silent aim. When a target is found, the angles the server is told point
	// at it while the camera keeps whatever the mouse says. Takes precedence
	// over the fixed offset above, which stays as the test mode: a constant 90
	// degrees is trivial to confirm by eye and by packet, so it remains the way
	// to answer "is the swap reaching the server at all" without involving
	// target selection.
	inline bool  silent_aim_enabled = false;
	// Separate from killaura_range on purpose: aiming at something is not the
	// same as being able to hit it, and wanting to track further than you can
	// reach is the normal case rather than a mistake.
	inline float silent_aim_range = 4.5f;
	inline float silent_aim_fov   = 180.0f;

	// Auto-attack for silent aim. Silent aim only ROTATES onto a target; this
	// makes it swing on its own too, so it works as a full aura without needing
	// killaura enabled. Same queued-on-tick-thread attack path killaura uses,
	// so the order stays Look(fake) -> Attack. Gated by a randomized CPS and
	// (optionally) the vanilla attack cooldown, and only inside `range`.
	inline bool  silent_aim_autoattack       = true;
	inline int   silent_aim_min_cps          = 8;
	inline int   silent_aim_max_cps          = 12;
	inline bool  silent_aim_require_cooldown = true;

	// Multipoint. Scans the whole target hitbox instead of the eleven points up
	// its middle, and keeps the candidate needing the smallest turn. Resolution
	// is the grid size per axis, so it costs resolution^3 candidates a tick --
	// 5 is 125, which is nothing; 9 is 729 and still cheap next to a JNI call.
	inline bool  silent_aim_multipoint = true;
	inline int   silent_aim_resolution = 5;
	// Points that fail the visibility test are held to this shorter range
	// instead of the full one, matching LiquidBounce's wallsRange.
	inline float silent_aim_walls_range = 3.0f;

	// How multipoint picks among the candidates it accepted.
	//
	// 0 Center: prefer the point closest to the middle of the hitbox, falling
	//   back outward only when the middle is out of reach. The aim genuinely
	//   points at the target.
	// 1 Least turn: LiquidBounce's default -- the candidate needing the
	//   smallest rotation change. Looks more legitimate, but because a hitbox
	//   is three times taller than it is wide, the vertical choice collapses
	//   into "whatever height I am already looking at" and the pitch stops
	//   tracking the target at all.
	inline int silent_aim_point_mode = 0;


	// Log what the tick hook actually applies, once a second. Off by default:
	// the hook runs 20 times a second and an ungated log here is how a 9.7 MB
	// log file happened once already.
	inline bool aiming_debug_log = false;

	// --- AngleSmooth: 0 Linear, 1 Sigmoid, 2 Interpolation, 3 Acceleration
	// LiquidBounce's set minus Multipoint and Clone, which pull in a whole
	// machinery of their own and are deliberately out of scope.
	inline int aiming_smooth_mode = 0;

	// Degrees per tick. Ranges rather than constants because a fixed turn rate
	// is its own signature. Also the speed ceiling for Sigmoid.
	// 180 deg/tick is not smoothing -- delta_to already wraps to +/-180, so a
	// cap of 180 never binds and the aim arrives in a single tick. Kept well
	// below that so the limiter actually limits.
	inline float aiming_speed_min = 25.0f;
	inline float aiming_speed_max = 45.0f;

	// Sigmoid
	inline float aiming_sigmoid_steepness = 10.0f;
	inline float aiming_sigmoid_midpoint  = 0.3f;

	// Interpolation — percentages of the remaining distance, not degrees
	inline int   aiming_interp_h_min = 80,  aiming_interp_h_max = 85;
	inline int   aiming_interp_v_min = 20,  aiming_interp_v_max = 25;
	inline int   aiming_interp_dirchange_min = 95, aiming_interp_dirchange_max = 100;
	inline float aiming_interp_midpoint = 0.35f;

	// Acceleration
	inline float aiming_accel_yaw_min = 20.0f,   aiming_accel_yaw_max = 25.0f;
	inline float aiming_accel_pitch_min = 20.0f, aiming_accel_pitch_max = 25.0f;
	inline bool  aiming_accel_error_enabled = true;
	inline float aiming_accel_yaw_error = 0.1f, aiming_accel_pitch_error = 0.1f;
	inline bool  aiming_const_error_enabled = true;
	inline float aiming_const_yaw_error = 0.1f, aiming_const_pitch_error = 0.1f;
	inline bool  aiming_sigmoid_decel_enabled = false;
	inline float aiming_decel_steepness = 10.0f, aiming_decel_midpoint = 0.3f;

	// --- Humanising processors (LiquidBounce's combat-only group) ---------
	// Off by default: each trades accuracy for plausibility, and that is a
	// choice to make deliberately rather than inherit.
	inline bool  aiming_fail_enabled = false;
	inline int   aiming_fail_rate = 3;             // % chance per tick
	inline float aiming_fail_factor = 0.04f;
	inline float aiming_fail_horiz_min = 5.0f,  aiming_fail_horiz_max = 10.0f;
	inline float aiming_fail_vert_min  = 0.0f,  aiming_fail_vert_max  = 2.0f;
	inline int   aiming_fail_dur_min = 1, aiming_fail_dur_max = 4;   // ticks

	inline bool  aiming_jitter_enabled = false;
	inline bool  aiming_jitter_micro_enabled = true;
	inline float aiming_jitter_micro_yaw = 0.6f;
	inline float aiming_jitter_micro_pitch = 0.3f;
	inline bool  aiming_jitter_burst_enabled = true;
	inline int   aiming_jitter_burst_rate = 25;    // % chance per tick
	inline int   aiming_jitter_burst_dur_min = 2, aiming_jitter_burst_dur_max = 6;
	inline float aiming_jitter_burst_yaw = 2.5f, aiming_jitter_burst_pitch = 1.0f;
	inline bool  aiming_jitter_drift_enabled = true;
	inline float aiming_jitter_drift_max_yaw = 2.0f, aiming_jitter_drift_max_pitch = 1.0f;
	inline float aiming_jitter_drift_step = 0.15f, aiming_jitter_drift_reversion = 0.05f;

	inline bool  aiming_shortstop_enabled = false;
	inline int   aiming_shortstop_rate = 3;        // % chance per tick
	inline int   aiming_shortstop_dur_min = 1, aiming_shortstop_dur_max = 2;

	// Request lifetime and how close the walk-back must get before the
	// override is released. LiquidBounce's TicksUntilReset / ResetThreshold.
	inline int   aiming_ticks_until_reset = 5;
	inline float aiming_reset_threshold = 2.0f;

	// Quantise outgoing angles onto the mouse grid before sending. The single
	// most useful of these: a real mouse can only produce whole multiples of
	// a sensitivity-derived step, and an angle computed straight to a hitbox
	// does not land on one.
	inline bool aiming_normalize_enabled = false;

	// Rotation profile: 0 = Snap, 1 = Matrix, 2 = Polar (default), 3 = Linear
	inline int  killaura_rotation_type = 2;

	// Sprint bypass: 0 = None (off), anything else resets sprint before each hit
	// (setSprinting(false) + an explicit STOP_SPRINTING packet ahead of the
	// attack) so a falling hit lands as a crit instead of a sprint attack.
	// Default Grim; the mode names are cosmetic for now (all non-None behave the
	// same), a placeholder for per-anticheat timing later.
	inline int  killaura_sprint_bypass = 2;

	// Reach + FOV
	inline float killaura_range = 4.5f;
	inline float killaura_fov   = 360.0f;

	// Target ESP
	inline bool   killaura_esp_enabled = true;
	inline int    killaura_esp_type = 1;        // 0 = Cube, 1 = Circle, 2 = Ghosts
	inline float  killaura_esp_ghost_speed = 1.0f;
	inline ImVec4 killaura_esp_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

	// Debug overlay — small top-left text showing target acquisition status.
	// Useful when ESP isn't showing to confirm whether the module is finding
	// targets or stuck somewhere upstream.
	// 0 distance, 1 health, 2 fov, 3 hurt time
	inline int  killaura_sort_mode = 0;
	inline bool killaura_debug_overlay = true;

	// Friends list (in-memory only, per session)
	inline std::vector<std::string> killaura_friends;

	// Runtime state
	inline uint64_t killaura_last_attack_ms = 0;
	inline int      killaura_attack_count = 0;

	// ----- Autoclicker -----------------------------------------------------
	// Master toggle gates both sides. LMB and RMB are independent schedulers
	// inside, each with its own random CPS range. "Hold only" gates fire
	// only while the corresponding physical mouse button is held. RMB
	// "blocks only" restricts to the case where the main-hand item is a
	// BlockItem (placeable block) — so the autoclicker won't right-spam
	// food / pearls / buckets.
	inline bool autoclicker_enabled        = false;

	inline bool autoclicker_lmb_enabled    = true;
	inline int  autoclicker_lmb_min_cps    = 8;
	inline int  autoclicker_lmb_max_cps    = 14;
	inline bool autoclicker_lmb_hold_only  = true;
	inline bool autoclicker_lmb_skip_on_block = true;  // don't click while crosshair on a block (so mining works)

	inline bool autoclicker_rmb_enabled    = false;
	inline int  autoclicker_rmb_min_cps    = 6;
	inline int  autoclicker_rmb_max_cps    = 10;
	inline bool autoclicker_rmb_hold_only  = true;
	inline bool autoclicker_rmb_blocks_only = true;

	inline int  autoclicker_keybind        = 0;
	inline int  autoclicker_keybind_mode   = 2;   // 0=Hold, 1=Toggle, 2=Always

	// ----- Eagle -----------------------------------------------------------
	// Auto-sneak when standing near a block edge that opens into air. The
	// threshold is the distance (in blocks) from the player's hitbox edge
	// to the air face — below this we engage sneak. Vanilla MC's sneak edge
	// clamp uses ~0.05 internally; sensible user values are 0.05..0.35.
	inline bool  eagle_enabled       = false;
	inline float eagle_edge_distance = 0.05f;
	// Side-probe sensitivity multiplier — how much of the forward edge
	// distance is used for the two probes perpendicular to the player's
	// movement direction. 1.0 = same sensitivity all around, 0.0 = only
	// look in the direction we're walking. Default 0.3 means strafing
	// near an edge engages sneak, but only 30% as eagerly as walking
	// straight off it would.
	inline float eagle_side_sensitivity = 0.3f;
	// Minimum horizontal speed (blocks per tick) below which we consider
	// the player "idle" and skip detection entirely — standing on an
	// edge isn't dangerous.
	inline float eagle_min_speed     = 0.04f;
	inline int   eagle_keybind       = 0;
	inline int   eagle_keybind_mode  = 2;
	inline bool  eagle_always        = false;
	inline bool  eagle_debug_overlay = true;

	// ----- Teams -----------------------------------------------------------
	// Detect teammates by leather armor color so killaura skips them. The
	// tolerance is the maximum allowed L1 RGB distance for a color match
	// (0..765). Default ~24 is a gentle "same dye allowing for lighting".
	inline bool teams_enabled         = false;
	inline int  teams_color_tolerance = 24;

	// ----- HUD -------------------------------------------------------------
	// In-game overlay: watermark (top-right), potion list (top-left under
	// watermark — currently a stub), active keybinds (bottom-left), target
	// HUD (top-right under watermark — depends on killaura target state).
	inline bool hud_enabled             = true;
	inline bool hud_watermark_enabled   = true;
	inline bool hud_potion_list_enabled = true;
	inline bool hud_keybinds_enabled    = true;
	inline bool hud_target_hud_enabled  = true;

	// HUD element positions (screen pixels from top-left). Each element is
	// drag-positionable while the menu is open — the user clicks and drags
	// the element to wherever they want it. Initial values are sensible
	// defaults for a 1280x720-ish window; users with larger resolutions
	// just drag them where they want.
	inline float hud_watermark_x   = 1080.0f;
	inline float hud_watermark_y   =   10.0f;
	inline float hud_keybinds_x    =   10.0f;
	inline float hud_keybinds_y    =  500.0f;
	inline float hud_target_hud_x  = 1080.0f;
	inline float hud_target_hud_y  =   50.0f;
	inline float hud_potion_list_x =   10.0f;
	inline float hud_potion_list_y =   10.0f;
}
