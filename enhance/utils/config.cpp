#include "config.h"

#include "../globals/globals.h"
#include "logger.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	// A registry of {key, type, pointer}. The settings are loose inline
	// globals rather than a struct, so this table is the one place that has to
	// know about all of them — and keeping it explicit means runtime state
	// (executing flags, saved slots, cached handles) can never be written out
	// by accident just because it happens to live in the same namespace.
	enum class kind { b, i, f, d, col };

	struct entry
	{
		const char* key;
		kind        type;
		void*       ptr;
	};

	std::atomic<bool>   g_dirty{ false };
	std::atomic<ULONGLONG> g_dirty_since{ 0 };
	bool g_loaded = false;

	// Debounce: dragging a slider dirties the config every frame, and writing
	// on each change would mean continuous disk traffic.
	constexpr ULONGLONG k_debounce_ms = 1500;

	const std::vector<entry>& table()
	{
		static const std::vector<entry> t = {
			{ "gui.key",                kind::i,   &globals::gui_keybind },
			{ "gui.key_mode",           kind::i,   &globals::gui_keybind_mode },
			{ "gui.theme",              kind::i,   &globals::gui_theme_id },
			{ "unload.key",             kind::i,   &globals::unload_keybind },
			{ "unload.level",           kind::i,   &globals::unload_level },

			{ "hitbox.enabled",         kind::b,   &globals::hitbox_enabled },
			{ "hitbox.width",           kind::d,   &globals::hitbox_expand_width },
			{ "hitbox.height",          kind::d,   &globals::hitbox_expand_height },
			{ "hitbox.key",             kind::i,   &globals::hitbox_keybind },
			{ "hitbox.mode",            kind::i,   &globals::hitbox_mode },

			{ "sprint.enabled",         kind::b,   &globals::sprint_enabled },
			{ "flight.enabled",         kind::b,   &globals::flight_enabled },

			{ "tbot.enabled",           kind::b,   &globals::triggerbot_enabled },
			{ "tbot.mode",              kind::i,   &globals::triggerbot_mode },
			{ "tbot.delay",             kind::i,   &globals::triggerbot_delay_ms },
			{ "tbot.min_delay",         kind::i,   &globals::triggerbot_min_delay_ms },
			{ "tbot.max_delay",         kind::i,   &globals::triggerbot_max_delay_ms },
			{ "tbot.crit_mode",         kind::i,   &globals::triggerbot_crit_mode },
			{ "tbot.hit_select",        kind::b,   &globals::triggerbot_hit_select },
			{ "tbot.key",               kind::i,   &globals::triggerbot_keybind },
			{ "tbot.key_mode",          kind::i,   &globals::triggerbot_keybind_mode },
			{ "tbot.weapon_only",       kind::b,   &globals::triggerbot_weapon_only },
			{ "tbot.through_walls",     kind::b,   &globals::triggerbot_through_walls },
			{ "tbot.through_walls_rng", kind::f,   &globals::triggerbot_through_walls_range },
			{ "tbot.through_walls_exp", kind::f,   &globals::triggerbot_through_walls_expand },
			{ "tbot.use_shield",        kind::b,   &globals::triggerbot_use_shield },
			{ "tbot.shield_ms",         kind::i,   &globals::triggerbot_shield_duration_ms },
			{ "tbot.check_shield",      kind::b,   &globals::triggerbot_check_shield },
			{ "tbot.shield_action",     kind::i,   &globals::triggerbot_shield_action },
			{ "tbot.sprint_reset",      kind::b,   &globals::triggerbot_sprint_reset },
			{ "tbot.sprint_ticks",      kind::i,   &globals::triggerbot_sprint_reset_ticks },

			{ "reach.enabled",          kind::b,   &globals::reach_enabled },
			{ "reach.distance",         kind::d,   &globals::reach_distance },
			{ "reach.key",              kind::i,   &globals::reach_keybind },
			{ "reach.mode",             kind::i,   &globals::reach_mode },

			{ "aim.enabled",            kind::b,   &globals::aimassist_enabled },
			{ "aim.smoothing",          kind::f,   &globals::aimassist_smoothing },
			{ "aim.max_distance",       kind::d,   &globals::aimassist_max_distance },
			{ "aim.key",                kind::i,   &globals::aimassist_keybind },
			{ "aim.mode",               kind::i,   &globals::aimassist_mode },
			{ "aim.horizontal",         kind::b,   &globals::aimassist_horizontal },
			{ "aim.vertical",           kind::b,   &globals::aimassist_vertical },
			{ "aim.mouse_input",        kind::b,   &globals::aimassist_use_mouse_input },
			{ "aim.sensitivity",        kind::f,   &globals::aimassist_mouse_sensitivity },

			{ "pearl.key",              kind::i,   &globals::pearl_catch_keybind },
			{ "pearl.mode",             kind::i,   &globals::pearl_catch_mode },
			{ "pearl.aim_mode",         kind::i,   &globals::pearl_catch_aim_mode },

			{ "esp.box",                kind::b,   &globals::box_enabled },
			{ "esp.key",                kind::i,   &globals::esp_keybind },
			{ "esp.mode",               kind::i,   &globals::esp_mode },
			{ "esp.health_bar",         kind::b,   &globals::esp_health_bar },
			{ "esp.style",              kind::i,   &globals::esp_box_style },
			{ "esp.filled",             kind::b,   &globals::esp_box_filled },
			{ "esp.tracers",            kind::b,   &globals::esp_tracers },
			{ "esp.fade",               kind::b,   &globals::esp_distance_fade },
			{ "esp.fade_start",         kind::f,   &globals::esp_fade_start },
			{ "esp.fade_end",           kind::f,   &globals::esp_fade_end },
			{ "esp.color",              kind::col, &globals::esp_color },
			{ "esp.world_render",       kind::b,   &globals::esp_world_render_enabled },
			{ "esp.world_through",      kind::b,   &globals::esp_world_through_walls },
			{ "esp.world_filled",       kind::b,   &globals::esp_world_filled },

			{ "nt.enabled",             kind::b,   &globals::nametags_enabled },
			{ "nt.in_world",            kind::b,   &globals::nametags_in_world },
			{ "nt.size",                kind::f,   &globals::nametags_size },
			{ "nt.alpha",               kind::f,   &globals::nametags_alpha },
			{ "nt.range",               kind::f,   &globals::nametags_range },
			{ "nt.static_size",         kind::b,   &globals::nametags_static_size },
			{ "nt.show_friends",        kind::b,   &globals::nametags_show_friends },
			{ "nt.show_health",         kind::b,   &globals::nametags_show_health },
			{ "nt.show_distance",       kind::b,   &globals::nametags_show_distance },
			{ "nt.health_mode",         kind::i,   &globals::nametags_health_mode },
			{ "nt.friend_color",        kind::col, &globals::nametags_friend_color },
			{ "nt.hide_vanilla",        kind::b,   &globals::nametags_hide_vanilla },

			{ "mace.enabled",           kind::b,   &globals::mace_enabled },
			{ "mace.look",              kind::b,   &globals::mace_look },
			{ "mace.switch_back",       kind::b,   &globals::mace_switch_back },
			{ "mace.remove_elytra",     kind::b,   &globals::mace_remove_elytra },
			{ "mace.min_fall",          kind::d,   &globals::mace_min_fall_distance },
			{ "mace.height_above",      kind::d,   &globals::mace_height_above_target },
			{ "mace.hitbox_w",          kind::d,   &globals::mace_fall_hitbox_width },
			{ "mace.hitbox_h",          kind::d,   &globals::mace_fall_hitbox_height },
			{ "mace.key",               kind::i,   &globals::mace_keybind },
			{ "mace.key_mode",          kind::i,   &globals::mace_keybind_mode },

			{ "sb.enabled",             kind::b,   &globals::shield_breaker_enabled },
			{ "sb.aim",                 kind::b,   &globals::shield_breaker_aim },
			{ "sb.switch_back",         kind::b,   &globals::shield_breaker_switch_back },
			{ "sb.delay",               kind::i,   &globals::shield_breaker_delay_ms },
			{ "sb.key",                 kind::i,   &globals::shield_breaker_keybind },
			{ "sb.key_mode",            kind::i,   &globals::shield_breaker_keybind_mode },

			{ "srot.enabled",           kind::b,   &globals::server_rotation_enabled },
			{ "srot.mode",              kind::i,   &globals::server_rotation_mode },
			{ "srot.yaw",               kind::f,   &globals::server_rotation_custom_yaw },
			{ "srot.pitch",             kind::f,   &globals::server_rotation_custom_pitch },

			{ "ss.enabled",             kind::b,   &globals::stun_slam_enabled },
			{ "ss.chance",              kind::f,   &globals::stun_slam_chance },
			{ "ss.swap_delay",          kind::i,   &globals::stun_slam_swap_delay_ms },
			{ "ss.axe_delay",           kind::i,   &globals::stun_slam_axe_delay_ms },
			{ "ss.mace_delay",          kind::i,   &globals::stun_slam_mace_delay_ms },
			{ "ss.min_fall",            kind::d,   &globals::stun_slam_min_fall },

			{ "stap.enabled",           kind::b,   &globals::stap_enabled },
			{ "stap.duration",          kind::i,   &globals::stap_duration_ms },
			{ "wtap.enabled",           kind::b,   &globals::wtap_enabled },
			{ "wtap.duration",          kind::i,   &globals::wtap_duration_ms },

			{ "aanchor.enabled",        kind::b,   &globals::auto_anchor_enabled },
			{ "aanchor.key",            kind::i,   &globals::auto_anchor_keybind },
			{ "aanchor.mode",           kind::i,   &globals::auto_anchor_mode },
			{ "aanchor.swap_delay",     kind::i,   &globals::auto_anchor_swap_delay_ms },
			{ "aanchor.charge_delay",   kind::i,   &globals::auto_anchor_charge_delay_ms },

			{ "amacro.enabled",         kind::b,   &globals::anchor_macro_enabled },
			{ "amacro.key",             kind::i,   &globals::anchor_macro_keybind },
			{ "amacro.mode",            kind::i,   &globals::anchor_macro_mode },
			{ "amacro.break",           kind::b,   &globals::anchor_macro_break_anchor },
			{ "amacro.swap_delay",      kind::i,   &globals::anchor_macro_swap_delay_ms },
			{ "amacro.charge_delay",    kind::i,   &globals::anchor_macro_charge_delay_ms },
			{ "amacro.break_delay",     kind::i,   &globals::anchor_macro_break_delay_ms },

			{ "sesp.enabled",           kind::b,   &globals::storage_esp_enabled },
			{ "sesp.key",               kind::i,   &globals::storage_esp_keybind },
			{ "sesp.mode",              kind::i,   &globals::storage_esp_mode },
			{ "sesp.chest",             kind::b,   &globals::storage_esp_chest },
			{ "sesp.ender",             kind::b,   &globals::storage_esp_ender_chest },
			{ "sesp.shulker",           kind::b,   &globals::storage_esp_shulker },

			{ "acr.enabled",            kind::b,   &globals::autocrystal_enabled },
			{ "acr.key",                kind::i,   &globals::autocrystal_keybind },
			{ "acr.mode",               kind::i,   &globals::autocrystal_mode },
			{ "acr.delay",              kind::i,   &globals::autocrystal_delay_ms },
			{ "acr.debug",              kind::b,   &globals::autocrystal_debug_enabled },

			{ "at.enabled",             kind::b,   &globals::autototem_enabled },
			{ "at.key",                 kind::i,   &globals::autototem_keybind },
			{ "at.mode",                kind::i,   &globals::autototem_mode },
			{ "at.rage",                kind::b,   &globals::autototem_rage_mode },

			{ "ajr.enabled",            kind::b,   &globals::autojumpreset_enabled },
			{ "ajr.key",                kind::i,   &globals::autojumpreset_keybind },
			{ "ajr.mode",               kind::i,   &globals::autojumpreset_mode },
			{ "ajr.cooldown",           kind::i,   &globals::autojumpreset_cooldown_ms },

			{ "ka.enabled",             kind::b,   &globals::killaura_enabled },
			{ "ka.key",                 kind::i,   &globals::killaura_keybind },
			{ "ka.key_mode",            kind::i,   &globals::killaura_keybind_mode },
			{ "ka.players",             kind::b,   &globals::killaura_target_players },
			{ "ka.mobs",                kind::b,   &globals::killaura_target_mobs },
			{ "ka.animals",             kind::b,   &globals::killaura_target_animals },
			{ "ka.friends",             kind::b,   &globals::killaura_target_friends },
			{ "ka.only_crit",           kind::b,   &globals::killaura_only_critical },
			{ "ka.smart_crit",          kind::b,   &globals::killaura_smart_critical },
			{ "ka.dyn_cooldown",        kind::b,   &globals::killaura_dynamic_cooldown },
			{ "ka.break_shield",        kind::b,   &globals::killaura_break_shield },
			{ "ka.unpress_shield",      kind::b,   &globals::killaura_unpress_shield },
			{ "ka.no_attack_eat",       kind::b,   &globals::killaura_no_attack_when_eat },
			{ "ka.ignore_walls",        kind::b,   &globals::killaura_ignore_walls },
			{ "ka.move_correction",     kind::b,   &globals::killaura_move_correction_enabled },
			{ "ka.correction_type",     kind::i,   &globals::killaura_correction_type },
			{ "aim.move_correction",    kind::i,   &globals::aiming_movement_correction },
			{ "silent.enabled",         kind::b,   &globals::silent_rotation_enabled },
			{ "silent.force_attach",    kind::b,   &globals::silent_rotation_force_attach },
			{ "silent.offset",          kind::f,   &globals::silent_rotation_offset },
			{ "silentaim.enabled",      kind::b,   &globals::silent_aim_enabled },
			{ "silentaim.range",        kind::f,   &globals::silent_aim_range },
			{ "silentaim.fov",          kind::f,   &globals::silent_aim_fov },
			{ "silentaim.multipoint",   kind::b,   &globals::silent_aim_multipoint },
			{ "silentaim.resolution",   kind::i,   &globals::silent_aim_resolution },
			{ "silentaim.pointmode",    kind::i,   &globals::silent_aim_point_mode },
			{ "silentaim.wallsrange",   kind::f,   &globals::silent_aim_walls_range },
			{ "silentaim.autoattack",   kind::b,   &globals::silent_aim_autoattack },
			{ "silentaim.min_cps",      kind::i,   &globals::silent_aim_min_cps },
			{ "silentaim.max_cps",      kind::i,   &globals::silent_aim_max_cps },
			{ "silentaim.require_cd",   kind::b,   &globals::silent_aim_require_cooldown },
			{ "aim.debug_log",          kind::b,   &globals::aiming_debug_log },
			{ "aim.smooth_mode",        kind::i,   &globals::aiming_smooth_mode },
			{ "aim.speed_min",          kind::f,   &globals::aiming_speed_min },
			{ "aim.speed_max",          kind::f,   &globals::aiming_speed_max },
			{ "aim.sig_steepness",      kind::f,   &globals::aiming_sigmoid_steepness },
			{ "aim.sig_midpoint",       kind::f,   &globals::aiming_sigmoid_midpoint },
			{ "aim.interp_h_min",       kind::i,   &globals::aiming_interp_h_min },
			{ "aim.interp_h_max",       kind::i,   &globals::aiming_interp_h_max },
			{ "aim.interp_v_min",       kind::i,   &globals::aiming_interp_v_min },
			{ "aim.interp_v_max",       kind::i,   &globals::aiming_interp_v_max },
			{ "aim.interp_dc_min",      kind::i,   &globals::aiming_interp_dirchange_min },
			{ "aim.interp_dc_max",      kind::i,   &globals::aiming_interp_dirchange_max },
			{ "aim.interp_midpoint",    kind::f,   &globals::aiming_interp_midpoint },
			{ "aim.accel_yaw_min",      kind::f,   &globals::aiming_accel_yaw_min },
			{ "aim.accel_yaw_max",      kind::f,   &globals::aiming_accel_yaw_max },
			{ "aim.accel_pitch_min",    kind::f,   &globals::aiming_accel_pitch_min },
			{ "aim.accel_pitch_max",    kind::f,   &globals::aiming_accel_pitch_max },
			{ "aim.accel_err",          kind::b,   &globals::aiming_accel_error_enabled },
			{ "aim.accel_err_yaw",      kind::f,   &globals::aiming_accel_yaw_error },
			{ "aim.accel_err_pitch",    kind::f,   &globals::aiming_accel_pitch_error },
			{ "aim.const_err",          kind::b,   &globals::aiming_const_error_enabled },
			{ "aim.const_err_yaw",      kind::f,   &globals::aiming_const_yaw_error },
			{ "aim.const_err_pitch",    kind::f,   &globals::aiming_const_pitch_error },
			{ "aim.sig_decel",          kind::b,   &globals::aiming_sigmoid_decel_enabled },
			{ "aim.decel_steepness",    kind::f,   &globals::aiming_decel_steepness },
			{ "aim.decel_midpoint",     kind::f,   &globals::aiming_decel_midpoint },
			{ "aim.ticks_until_reset",  kind::i,   &globals::aiming_ticks_until_reset },
			{ "aim.reset_threshold",    kind::f,   &globals::aiming_reset_threshold },
			{ "aim.normalize",          kind::b,   &globals::aiming_normalize_enabled },
			{ "aim.fail",               kind::b,   &globals::aiming_fail_enabled },
			{ "aim.fail_rate",          kind::i,   &globals::aiming_fail_rate },
			{ "aim.fail_factor",        kind::f,   &globals::aiming_fail_factor },
			{ "aim.fail_h_min",         kind::f,   &globals::aiming_fail_horiz_min },
			{ "aim.fail_h_max",         kind::f,   &globals::aiming_fail_horiz_max },
			{ "aim.fail_v_min",         kind::f,   &globals::aiming_fail_vert_min },
			{ "aim.fail_v_max",         kind::f,   &globals::aiming_fail_vert_max },
			{ "aim.fail_dur_min",       kind::i,   &globals::aiming_fail_dur_min },
			{ "aim.fail_dur_max",       kind::i,   &globals::aiming_fail_dur_max },
			{ "aim.jitter",             kind::b,   &globals::aiming_jitter_enabled },
			{ "aim.jitter_micro",       kind::b,   &globals::aiming_jitter_micro_enabled },
			{ "aim.jitter_micro_yaw",   kind::f,   &globals::aiming_jitter_micro_yaw },
			{ "aim.jitter_micro_pitch", kind::f,   &globals::aiming_jitter_micro_pitch },
			{ "aim.jitter_burst",       kind::b,   &globals::aiming_jitter_burst_enabled },
			{ "aim.jitter_burst_rate",  kind::i,   &globals::aiming_jitter_burst_rate },
			{ "aim.jitter_burst_dmin",  kind::i,   &globals::aiming_jitter_burst_dur_min },
			{ "aim.jitter_burst_dmax",  kind::i,   &globals::aiming_jitter_burst_dur_max },
			{ "aim.jitter_burst_yaw",   kind::f,   &globals::aiming_jitter_burst_yaw },
			{ "aim.jitter_burst_pitch", kind::f,   &globals::aiming_jitter_burst_pitch },
			{ "aim.jitter_drift",       kind::b,   &globals::aiming_jitter_drift_enabled },
			{ "aim.jitter_drift_yaw",   kind::f,   &globals::aiming_jitter_drift_max_yaw },
			{ "aim.jitter_drift_pitch", kind::f,   &globals::aiming_jitter_drift_max_pitch },
			{ "aim.jitter_drift_step",  kind::f,   &globals::aiming_jitter_drift_step },
			{ "aim.jitter_drift_rev",   kind::f,   &globals::aiming_jitter_drift_reversion },
			{ "aim.shortstop",          kind::b,   &globals::aiming_shortstop_enabled },
			{ "aim.shortstop_rate",     kind::i,   &globals::aiming_shortstop_rate },
			{ "aim.shortstop_dmin",     kind::i,   &globals::aiming_shortstop_dur_min },
			{ "aim.shortstop_dmax",     kind::i,   &globals::aiming_shortstop_dur_max },
			{ "ka.rotation_type",       kind::i,   &globals::killaura_rotation_type },
			{ "ka.sprint_bypass",       kind::i,   &globals::killaura_sprint_bypass },
			{ "ka.range",               kind::f,   &globals::killaura_range },
			{ "ka.fov",                 kind::f,   &globals::killaura_fov },
			{ "ka.autoattack",          kind::b,   &globals::killaura_autoattack },
			{ "ka.min_cps",             kind::i,   &globals::killaura_min_cps },
			{ "ka.max_cps",             kind::i,   &globals::killaura_max_cps },
			{ "ka.require_cd",          kind::b,   &globals::killaura_require_cooldown },
			{ "ka.esp",                 kind::b,   &globals::killaura_esp_enabled },
			{ "ka.esp_type",            kind::i,   &globals::killaura_esp_type },
			{ "ka.esp_ghost_speed",     kind::f,   &globals::killaura_esp_ghost_speed },
			{ "ka.esp_color",           kind::col, &globals::killaura_esp_color },
			{ "ka.debug",               kind::b,   &globals::killaura_debug_overlay },

			{ "ac.enabled",             kind::b,   &globals::autoclicker_enabled },
			{ "ac.lmb",                 kind::b,   &globals::autoclicker_lmb_enabled },
			{ "ac.lmb_min",             kind::i,   &globals::autoclicker_lmb_min_cps },
			{ "ac.lmb_max",             kind::i,   &globals::autoclicker_lmb_max_cps },
			{ "ac.lmb_hold",            kind::b,   &globals::autoclicker_lmb_hold_only },
			{ "ac.lmb_skip_block",      kind::b,   &globals::autoclicker_lmb_skip_on_block },
			{ "ac.rmb",                 kind::b,   &globals::autoclicker_rmb_enabled },
			{ "ac.rmb_min",             kind::i,   &globals::autoclicker_rmb_min_cps },
			{ "ac.rmb_max",             kind::i,   &globals::autoclicker_rmb_max_cps },
			{ "ac.rmb_hold",            kind::b,   &globals::autoclicker_rmb_hold_only },
			{ "ac.rmb_blocks",          kind::b,   &globals::autoclicker_rmb_blocks_only },
			{ "ac.key",                 kind::i,   &globals::autoclicker_keybind },
			{ "ac.key_mode",            kind::i,   &globals::autoclicker_keybind_mode },

			{ "eagle.enabled",          kind::b,   &globals::eagle_enabled },
			{ "eagle.edge",             kind::f,   &globals::eagle_edge_distance },
			{ "eagle.side",             kind::f,   &globals::eagle_side_sensitivity },
			{ "eagle.min_speed",        kind::f,   &globals::eagle_min_speed },
			{ "eagle.key",              kind::i,   &globals::eagle_keybind },
			{ "eagle.key_mode",         kind::i,   &globals::eagle_keybind_mode },
			{ "eagle.always",           kind::b,   &globals::eagle_always },
			{ "eagle.debug",            kind::b,   &globals::eagle_debug_overlay },

			{ "teams.enabled",          kind::b,   &globals::teams_enabled },
			{ "teams.tolerance",        kind::i,   &globals::teams_color_tolerance },

			{ "hud.enabled",            kind::b,   &globals::hud_enabled },
			{ "hud.watermark",          kind::b,   &globals::hud_watermark_enabled },
			{ "hud.potions",            kind::b,   &globals::hud_potion_list_enabled },
			{ "hud.keybinds",           kind::b,   &globals::hud_keybinds_enabled },
			{ "hud.target",             kind::b,   &globals::hud_target_hud_enabled },
			{ "hud.watermark_x",        kind::f,   &globals::hud_watermark_x },
			{ "hud.watermark_y",        kind::f,   &globals::hud_watermark_y },
			{ "hud.keybinds_x",         kind::f,   &globals::hud_keybinds_x },
			{ "hud.keybinds_y",         kind::f,   &globals::hud_keybinds_y },
			{ "hud.target_x",           kind::f,   &globals::hud_target_hud_x },
			{ "hud.target_y",           kind::f,   &globals::hud_target_hud_y },
			{ "hud.potions_x",          kind::f,   &globals::hud_potion_list_x },
			{ "hud.potions_y",          kind::f,   &globals::hud_potion_list_y },
		};
		return t;
	}

	std::string directory()
	{
		char appdata[MAX_PATH]{};
		if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
			return std::string(appdata) + "\\enhance";

		// Falling back next to the DLL would be useless — the injector drops it
		// into %TEMP% and deletes it — so use %TEMP% itself as a last resort.
		char tmp[MAX_PATH]{};
		if (GetTempPathA(MAX_PATH, tmp))
			return std::string(tmp) + "enhance";

		return ".";
	}

	void write_value(std::ostream& out, const entry& e)
	{
		switch (e.type)
		{
			case kind::b: out << (*static_cast<bool*>(e.ptr) ? 1 : 0); break;
			case kind::i: out << *static_cast<int*>(e.ptr); break;
			case kind::f: out << *static_cast<float*>(e.ptr); break;
			case kind::d: out << *static_cast<double*>(e.ptr); break;
			case kind::col:
			{
				const ImVec4& c = *static_cast<ImVec4*>(e.ptr);
				out << c.x << ' ' << c.y << ' ' << c.z << ' ' << c.w;
				break;
			}
		}
	}

	void read_value(const std::string& value, const entry& e)
	{
		std::istringstream in(value);
		switch (e.type)
		{
			case kind::b: { int v = 0; if (in >> v) *static_cast<bool*>(e.ptr) = v != 0; break; }
			case kind::i: { int v = 0; if (in >> v) *static_cast<int*>(e.ptr) = v; break; }
			case kind::f: { float v = 0; if (in >> v) *static_cast<float*>(e.ptr) = v; break; }
			case kind::d: { double v = 0; if (in >> v) *static_cast<double*>(e.ptr) = v; break; }
			case kind::col:
			{
				ImVec4 c{};
				if (in >> c.x >> c.y >> c.z >> c.w)
					*static_cast<ImVec4*>(e.ptr) = c;
				break;
			}
		}
	}
}

std::string enhance::config::path()
{
	return directory() + "\\config.cfg";
}

bool enhance::config::save()
{
	const std::string dir = directory();
	CreateDirectoryA(dir.c_str(), nullptr);

	// Write to a temporary file and move it into place, so a crash mid-write
	// cannot leave a truncated config behind.
	const std::string final_path = path();
	const std::string temp_path = final_path + ".tmp";

	{
		std::ofstream out(temp_path, std::ios::out | std::ios::trunc);
		if (!out.is_open())
		{
			logger::log_error("[config] could not open " + temp_path);
			return false;
		}

		out << "# enhance settings - rewritten automatically, edits are kept\n";
		for (const auto& e : table())
		{
			out << e.key << '=';
			write_value(out, e);
			out << '\n';
		}
	}

	if (!MoveFileExA(temp_path.c_str(), final_path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		logger::log_error("[config] could not replace " + final_path);
		return false;
	}

	g_dirty.store(false);
	return true;
}

bool enhance::config::load()
{
	std::ifstream in(path());
	if (!in.is_open())
	{
		g_loaded = true;
		return false;
	}

	std::string line;
	int applied = 0;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
			continue;

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string key = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);

		// Unknown keys are skipped rather than treated as an error: a config
		// written by an older or newer build must still load.
		for (const auto& e : table())
		{
			if (key == e.key)
			{
				read_value(value, e);
				++applied;
				break;
			}
		}
	}

	g_loaded = true;
	g_dirty.store(false);
	logger::log("[config] loaded " + std::to_string(applied) + " settings from " + path());
	return true;
}

void enhance::config::touch()
{
	if (!g_dirty.exchange(true))
		g_dirty_since.store(GetTickCount64());
}

void enhance::config::tick_autosave()
{
	if (!g_loaded || !g_dirty.load())
		return;

	if (GetTickCount64() - g_dirty_since.load() < k_debounce_ms)
		return;

	save();
}
