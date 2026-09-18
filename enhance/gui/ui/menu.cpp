#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <sdk/version/version.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/caps/caps.h>
#include "../../modules/aiming/silent_aim.h"
#include "../../modules/killaura/killaura.h"
#include <imgui_internal.h>
#include <animations.hpp>
#include <xorstr.hpp>
#include <unicodes.hpp>
#include <fonts.hpp>

#include <string>
#include <vector>
#include <unordered_map>

#include "menu.hpp"
#include "font_manager.hpp"
#include "comp_builder.hpp"
#include "widgets_manager.hpp"
#include "style_manager.hpp"
#include "tabs_manager.hpp"
#include "child_manager.hpp"
#include "popup_manager.hpp"
#include "lang_manager.hpp"
#include "search_manager.hpp"

#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include "../../modules/world_render/world_render_hook.h"

using namespace ImGui;

namespace {

float child_half_width( ) {
	return ( GetWindowWidth( ) - GImGui->Style.WindowPadding.x * 2.f - GImGui->Style.ItemSpacing.x ) / 2.f;
}

// Several globals are doubles / ints but the slider widget hands its value
// pointer to the search index, which replays the widget on a later frame. A
// frame-local mirror would dangle, so mirrors are keyed by the global's
// address and live for the process.
float& mirror_for( const void* key ) {
	static std::unordered_map< const void*, float > m;
	return m[key];
}

bool slider_d( const char* label, double* v, float mn, float mx, const char* fmt ) {
	float& f = mirror_for( v );
	f = static_cast< float >( *v );
	if ( widgets_manager::get( ).slider_float( label, &f, mn, mx, fmt ) ) {
		*v = static_cast< double >( f );
		return true;
	}
	return false;
}

// Accent colour lives in the style, not in globals, so the picker needs its
// own storage that it can write back into ImGuiCol_Scheme every frame.
float g_accent[4] = { 142.f / 255.f, 132.f / 255.f, 255.f / 255.f, 1.f };

// Draws a control the running Minecraft version cannot support as disabled, with
// the reason underneath. A control that is present and silently inert is the one
// thing AGENT.md asks this menu never to show: "it is off" and "this version
// cannot do it" have to look different.
static bool begin_gate( sdk::caps::feature f )
{
	if ( sdk::caps::available( f ) )
		return false;
	ImGui::BeginDisabled( true );
	return true;
}

static void end_gate( bool gated, sdk::caps::feature f )
{
	if ( !gated )
		return;
	ImGui::EndDisabled( );
	ImGui::TextDisabled( "%s", sdk::caps::why_not( f ) );
}

// A module's enable checkbox with its key binding, plus the Hold/Toggle choice
// tucked into the checkbox's options popup.
//
// The mode was the whole reason binds appeared broken: every module's mode
// global initialises to 2 ("Always" — ignore the key), and the only code that
// promoted it once a key was assigned lived in the old menu, which is dead.
// The bind processor now treats 2 as Toggle, so this control only has to
// expose the Hold alternative.
void bind_checkbox( const char* label, bool* enabled, int* key, int* mode )
{
	widgets_manager::get( ).checkbox( label, enabled, key, nullptr, [mode]( ) {
		// Stored as 0 = Hold, 1 = Toggle; the legacy 2 presents as Toggle,
		// which is exactly how the bind processor now treats it.
		if ( *mode == 2 ) *mode = 1;
		widgets_manager::get( ).combo( xorstr_( "Bind mode" ), mode,
			{ xorstr_( "Hold" ), xorstr_( "Toggle" ) } );
	} );
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------


// Everything killaura, in one place.
//
// It used to be several separate blocks with no diagnostics, so a module that
// was gated off and a module that could not find a target looked identical from
// the menu. The readout at the bottom is the difference.
void draw_killaura( float width ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );

	cm.begin_child( xorstr_( "Killaura" ), { width, 0 } );
	{
		bind_checkbox( xorstr_( "Enabled##ka" ), &globals::killaura_enabled, &globals::killaura_keybind, &globals::killaura_keybind_mode );

		w.checkbox( xorstr_( "Players##ka" ), &globals::killaura_target_players );
		w.checkbox( xorstr_( "Mobs##ka" ), &globals::killaura_target_mobs );
		w.checkbox( xorstr_( "Animals##ka" ), &globals::killaura_target_animals );
		w.checkbox( xorstr_( "Friends##ka" ), &globals::killaura_target_friends );

		w.slider_float( xorstr_( "Range##ka" ), &globals::killaura_range, 1.f, 6.f, "%.2f" );
		w.slider_float( xorstr_( "FOV##ka" ), &globals::killaura_fov, 60.f, 360.f, "%.0f" );
		w.combo( xorstr_( "Sort##ka" ), &globals::killaura_sort_mode,
		         { xorstr_( "Distance" ), xorstr_( "Health" ), xorstr_( "FOV" ), xorstr_( "Hurt time" ) } );

		// Clicker
		w.checkbox( xorstr_( "Auto attack##ka" ), &globals::killaura_autoattack );
		if ( globals::killaura_autoattack )
		{
			w.slider_int( xorstr_( "Min CPS##ka" ), &globals::killaura_min_cps, 1, 20, "%d" );
			w.slider_int( xorstr_( "Max CPS##ka" ), &globals::killaura_max_cps, 1, 20, "%d" );
			w.checkbox( xorstr_( "Require cooldown##ka" ), &globals::killaura_require_cooldown );
		}

		// Sprint reset for crits: None = off, anything else stops sprint (with an
		// explicit STOP_SPRINTING packet) before each hit.
		w.combo( xorstr_( "Sprint bypass##ka" ), &globals::killaura_sprint_bypass,
		         { xorstr_( "None" ), xorstr_( "MineBlaze" ), xorstr_( "Grim" ), xorstr_( "Legit" ) } );

		const auto d = enhance::modules::killaura::debug_state( );
		if ( !d.active )
			TextColored( ImVec4{ 0.65f, 0.65f, 0.65f, 1.f }, "idle (%s)", d.blocked_by );
		else if ( d.has_target )
			TextColored( ImVec4{ 0.4f, 1.f, 0.5f, 1.f },
			             "target%s: yaw %.1f pitch %.1f", d.locked ? " (locked)" : "", d.yaw, d.pitch );
		else
			TextColored( ImVec4{ 1.f, 0.55f, 0.55f, 1.f },
			             "no target (scanned %d, nearest %.1f)", d.candidates, d.distance );
	}
	cm.end_child( );
}

void page_combat_aim( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "AimAssist" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##aimassist" ), &globals::aimassist_enabled, &globals::aimassist_keybind, &globals::aimassist_mode );
			w.checkbox( xorstr_( "Horizontal (yaw)##aimassist" ), &globals::aimassist_horizontal );
			w.checkbox( xorstr_( "Vertical (pitch)##aimassist" ), &globals::aimassist_vertical );
			w.checkbox( xorstr_( "Use mouse input##aimassist" ), &globals::aimassist_use_mouse_input, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_float( xorstr_( "MC sensitivity##aimassist" ), &globals::aimassist_mouse_sensitivity, 0.01f, 2.f, "%.2f" );
			} );
			w.slider_float( xorstr_( "Smoothing##aimassist" ), &globals::aimassist_smoothing, 0.f, 1.f, "%.2f" );
			slider_d( xorstr_( "Max distance##aimassist" ), &globals::aimassist_max_distance, 1.f, 50.f, "%.1f" );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Triggerbot" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##tbot" ), &globals::triggerbot_enabled, &globals::triggerbot_keybind, &globals::triggerbot_keybind_mode );
			w.combo( xorstr_( "Mode##tbot" ), &globals::triggerbot_mode, { xorstr_( "Custom delay" ), xorstr_( "Weapon cooldown" ), xorstr_( "Combo" ) } );
			if ( globals::triggerbot_mode == 0 ) {
				w.slider_int( xorstr_( "Delay##tbot" ), &globals::triggerbot_delay_ms, 0, 500, "%d ms" );
			} else if ( globals::triggerbot_mode == 2 ) {
				w.slider_int( xorstr_( "Min delay##tbot" ), &globals::triggerbot_min_delay_ms, 0, 1500, "%d ms" );
				w.slider_int( xorstr_( "Max delay##tbot" ), &globals::triggerbot_max_delay_ms, 0, 1500, "%d ms" );
				if ( globals::triggerbot_min_delay_ms > globals::triggerbot_max_delay_ms )
					globals::triggerbot_max_delay_ms = globals::triggerbot_min_delay_ms;
			}
			w.checkbox( xorstr_( "Weapon only##tbot" ), &globals::triggerbot_weapon_only );
			w.checkbox( xorstr_( "Through walls##tbot" ), &globals::triggerbot_through_walls, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_float( xorstr_( "Range##tbotwall" ), &globals::triggerbot_through_walls_range, 1.f, 8.f, "%.1f" );
				widgets_manager::get( ).slider_float( xorstr_( "Hitbox expand##tbotwall" ), &globals::triggerbot_through_walls_expand, 0.f, 1.f, "%.2f" );
			} );
			w.checkbox( xorstr_( "Hit select##tbot" ), &globals::triggerbot_hit_select );
			w.combo( xorstr_( "Crit mode##tbot" ), &globals::triggerbot_crit_mode, { xorstr_( "Off" ), xorstr_( "Crit only" ), xorstr_( "Priority crit" ) } );
			w.checkbox( xorstr_( "Use shield##tbot" ), &globals::triggerbot_use_shield, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_int( xorstr_( "Shield time##tbot" ), &globals::triggerbot_shield_duration_ms, 10, 1000, "%d ms" );
			} );
			w.checkbox( xorstr_( "Check shield##tbot" ), &globals::triggerbot_check_shield );
			if ( globals::triggerbot_check_shield )
				w.combo( xorstr_( "Shield action##tbot" ), &globals::triggerbot_shield_action, { xorstr_( "Don't click" ), xorstr_( "Spam click" ) } );
			w.checkbox( xorstr_( "Sprint reset##tbot" ), &globals::triggerbot_sprint_reset, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_int( xorstr_( "Reset ticks##tbot" ), &globals::triggerbot_sprint_reset_ticks, 1, 10, "%d" );
			} );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		draw_killaura( half );

		// Rotations — LiquidBounce's aiming set, minus Multipoint and Clone.
		// The old Snap/Matrix/Polar/Linear profiles and the v1 move
		// correction are gone: both were dead code after the rework, the
		// profiles because RotationController was retired and the correction
		// because 1.21.11 removed the Input fields it wrote to.
		cm.begin_child( xorstr_( "Rotations" ), { half, 0 } );
		{
			// Silent rotation, rebuilt from the ground up. A fixed offset held
			// across the client tick: the server is told your yaw plus this,
			// your camera does not move. Everything else is meant to be built
			// on top of it once this is known good.
			w.checkbox( xorstr_( "Silent rotation##silent" ), &globals::silent_rotation_enabled, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_float( xorstr_( "Offset##silentoff" ), &globals::silent_rotation_offset, -180.f, 180.f, "%.0f" );
				widgets_manager::get( ).checkbox( xorstr_( "Force attach##silentforce" ), &globals::silent_rotation_force_attach );
			} );
			if ( globals::silent_rotation_enabled )
				TextColored( ImVec4{ 1.f, 0.75f, 0.35f, 1.f }, xorstr_( "server sees your yaw + offset" ) );

			// Silent aim rides the same machinery: it only replaces the fixed
			// offset with angles pointing at a target, so if the offset test
			// works and this does not, the fault is in target selection.
			w.checkbox( xorstr_( "Silent aim##saim" ), &globals::silent_aim_enabled, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_float( xorstr_( "Range##saimr" ), &globals::silent_aim_range, 1.f, 8.f, "%.1f" );
				widgets_manager::get( ).slider_float( xorstr_( "FOV##saimf" ), &globals::silent_aim_fov, 10.f, 360.f, "%.0f" );
				widgets_manager::get( ).checkbox( xorstr_( "Multipoint##saimmp" ), &globals::silent_aim_multipoint );
				if ( globals::silent_aim_multipoint )
				{
					widgets_manager::get( ).slider_int( xorstr_( "Resolution##saimres" ), &globals::silent_aim_resolution, 2, 9, "%d" );
					widgets_manager::get( ).combo( xorstr_( "Point##saimpm" ), &globals::silent_aim_point_mode,
					                               { xorstr_( "Center" ), xorstr_( "Least turn" ) } );
				}
				widgets_manager::get( ).checkbox( xorstr_( "Auto attack##saimaa" ), &globals::silent_aim_autoattack );
				if ( globals::silent_aim_autoattack )
				{
					widgets_manager::get( ).slider_int( xorstr_( "Min CPS##saimmincps" ), &globals::silent_aim_min_cps, 1, 20, "%d" );
					widgets_manager::get( ).slider_int( xorstr_( "Max CPS##saimmaxcps" ), &globals::silent_aim_max_cps, 1, 20, "%d" );
					widgets_manager::get( ).checkbox( xorstr_( "Require cooldown##saimcd" ), &globals::silent_aim_require_cooldown );
				}
			} );
			if ( globals::silent_aim_enabled )
			{
				const auto d = enhance::modules::aiming::silent_aim::debug_state( );
				if ( d.has_target )
					TextColored( ImVec4{ 0.4f, 1.f, 0.5f, 1.f },
					             "target: yaw %.1f pitch %.1f (pts %d)", d.yaw, d.pitch, d.points );
				else
					TextColored( ImVec4{ 1.f, 0.55f, 0.55f, 1.f },
					             "no target (scanned %d, nearest %.1f)", d.scanned, d.distance );
			}

			w.combo( xorstr_( "Smooth##aim" ), &globals::aiming_smooth_mode,
			         { xorstr_( "Linear" ), xorstr_( "Sigmoid" ), xorstr_( "Interpolation" ), xorstr_( "Acceleration" ) } );

			w.slider_float( xorstr_( "Turn speed min##aim" ), &globals::aiming_speed_min, 0.f, 180.f, "%.0f" );
			w.slider_float( xorstr_( "Turn speed max##aim" ), &globals::aiming_speed_max, 0.f, 180.f, "%.0f" );

			if ( globals::aiming_smooth_mode == 1 )
			{
				w.slider_float( xorstr_( "Steepness##aimsig" ), &globals::aiming_sigmoid_steepness, 0.f, 20.f, "%.1f" );
				w.slider_float( xorstr_( "Midpoint##aimsig" ), &globals::aiming_sigmoid_midpoint, 0.f, 1.f, "%.2f" );
			}
			else if ( globals::aiming_smooth_mode == 2 )
			{
				w.slider_int( xorstr_( "Horizontal min##aimint" ), &globals::aiming_interp_h_min, 1, 100, "%d%%" );
				w.slider_int( xorstr_( "Horizontal max##aimint" ), &globals::aiming_interp_h_max, 1, 100, "%d%%" );
				w.slider_int( xorstr_( "Vertical min##aimint" ), &globals::aiming_interp_v_min, 1, 100, "%d%%" );
				w.slider_int( xorstr_( "Vertical max##aimint" ), &globals::aiming_interp_v_max, 1, 100, "%d%%" );
				w.slider_int( xorstr_( "Dir change min##aimint" ), &globals::aiming_interp_dirchange_min, 0, 100, "%d%%" );
				w.slider_int( xorstr_( "Dir change max##aimint" ), &globals::aiming_interp_dirchange_max, 0, 100, "%d%%" );
				w.slider_float( xorstr_( "Midpoint##aimint" ), &globals::aiming_interp_midpoint, 0.f, 1.f, "%.2f" );
			}
			else if ( globals::aiming_smooth_mode == 3 )
			{
				w.slider_float( xorstr_( "Yaw accel min##aimacc" ), &globals::aiming_accel_yaw_min, 1.f, 180.f, "%.0f" );
				w.slider_float( xorstr_( "Yaw accel max##aimacc" ), &globals::aiming_accel_yaw_max, 1.f, 180.f, "%.0f" );
				w.slider_float( xorstr_( "Pitch accel min##aimacc" ), &globals::aiming_accel_pitch_min, 1.f, 180.f, "%.0f" );
				w.slider_float( xorstr_( "Pitch accel max##aimacc" ), &globals::aiming_accel_pitch_max, 1.f, 180.f, "%.0f" );
				w.checkbox( xorstr_( "Accel error##aimacc" ), &globals::aiming_accel_error_enabled, nullptr, nullptr, [&]( ) {
					widgets_manager::get( ).slider_float( xorstr_( "Yaw##aimaccerr" ), &globals::aiming_accel_yaw_error, 0.01f, 1.f, "%.2f" );
					widgets_manager::get( ).slider_float( xorstr_( "Pitch##aimaccerr" ), &globals::aiming_accel_pitch_error, 0.01f, 1.f, "%.2f" );
				} );
				w.checkbox( xorstr_( "Constant error##aimacc" ), &globals::aiming_const_error_enabled, nullptr, nullptr, [&]( ) {
					widgets_manager::get( ).slider_float( xorstr_( "Yaw##aimcerr" ), &globals::aiming_const_yaw_error, 0.01f, 1.f, "%.2f" );
					widgets_manager::get( ).slider_float( xorstr_( "Pitch##aimcerr" ), &globals::aiming_const_pitch_error, 0.01f, 1.f, "%.2f" );
				} );
				w.checkbox( xorstr_( "Sigmoid decel##aimacc" ), &globals::aiming_sigmoid_decel_enabled, nullptr, nullptr, [&]( ) {
					widgets_manager::get( ).slider_float( xorstr_( "Steepness##aimdec" ), &globals::aiming_decel_steepness, 0.f, 20.f, "%.1f" );
					widgets_manager::get( ).slider_float( xorstr_( "Midpoint##aimdec" ), &globals::aiming_decel_midpoint, 0.f, 1.f, "%.2f" );
				} );
			}

			{
				// Silent needs writable input; every other mode works anywhere.
				const bool silent_ok = sdk::caps::available( sdk::caps::feature::input_write );
				if ( !silent_ok && globals::aiming_movement_correction == 2 )
					globals::aiming_movement_correction = 1;
				w.combo( xorstr_( "Move correction##aim" ), &globals::aiming_movement_correction,
				         { xorstr_( "Off" ), xorstr_( "Strict" ), xorstr_( "Silent" ) } );
				if ( globals::aiming_movement_correction == 0 )
					TextDisabled( xorstr_( "body moves along your real angle" ) );
				else if ( globals::aiming_movement_correction == 2 )
					TextDisabled( xorstr_( "input rotated back, so the body keeps its direction" ) );
				if ( !silent_ok )
					TextDisabled( xorstr_( "Silent: %s" ), sdk::caps::why_not( sdk::caps::feature::input_write ) );
			}
			w.slider_int( xorstr_( "Ticks until reset##aim" ), &globals::aiming_ticks_until_reset, 1, 30, "%d" );
			w.slider_float( xorstr_( "Reset threshold##aim" ), &globals::aiming_reset_threshold, 1.f, 180.f, "%.0f" );

			w.checkbox( xorstr_( "Mouse grid##aim" ), &globals::aiming_normalize_enabled );

			w.checkbox( xorstr_( "Fail##aim" ), &globals::aiming_fail_enabled, nullptr, nullptr, [&]( ) {
				auto& wm = widgets_manager::get( );
				wm.slider_int( xorstr_( "Rate##aimfail" ), &globals::aiming_fail_rate, 1, 100, "%d%%" );
				wm.slider_float( xorstr_( "Factor##aimfail" ), &globals::aiming_fail_factor, 0.01f, 0.99f, "%.2f" );
				wm.slider_float( xorstr_( "Horiz min##aimfail" ), &globals::aiming_fail_horiz_min, 1.f, 90.f, "%.1f" );
				wm.slider_float( xorstr_( "Horiz max##aimfail" ), &globals::aiming_fail_horiz_max, 1.f, 90.f, "%.1f" );
				wm.slider_float( xorstr_( "Vert min##aimfail" ), &globals::aiming_fail_vert_min, 0.f, 90.f, "%.1f" );
				wm.slider_float( xorstr_( "Vert max##aimfail" ), &globals::aiming_fail_vert_max, 0.f, 90.f, "%.1f" );
				wm.slider_int( xorstr_( "Dur min##aimfail" ), &globals::aiming_fail_dur_min, 0, 20, "%d" );
				wm.slider_int( xorstr_( "Dur max##aimfail" ), &globals::aiming_fail_dur_max, 0, 20, "%d" );
			} );

			w.checkbox( xorstr_( "Jitter##aim" ), &globals::aiming_jitter_enabled, nullptr, nullptr, [&]( ) {
				auto& wm = widgets_manager::get( );
				wm.checkbox( xorstr_( "Micro noise##aimjit" ), &globals::aiming_jitter_micro_enabled );
				wm.slider_float( xorstr_( "Micro yaw##aimjit" ), &globals::aiming_jitter_micro_yaw, 0.f, 15.f, "%.2f" );
				wm.slider_float( xorstr_( "Micro pitch##aimjit" ), &globals::aiming_jitter_micro_pitch, 0.f, 15.f, "%.2f" );
				wm.checkbox( xorstr_( "Burst##aimjit" ), &globals::aiming_jitter_burst_enabled );
				wm.slider_int( xorstr_( "Burst rate##aimjit" ), &globals::aiming_jitter_burst_rate, 0, 100, "%d%%" );
				wm.slider_int( xorstr_( "Burst dur min##aimjit" ), &globals::aiming_jitter_burst_dur_min, 1, 40, "%d" );
				wm.slider_int( xorstr_( "Burst dur max##aimjit" ), &globals::aiming_jitter_burst_dur_max, 1, 40, "%d" );
				wm.slider_float( xorstr_( "Burst yaw##aimjit" ), &globals::aiming_jitter_burst_yaw, 0.f, 25.f, "%.2f" );
				wm.slider_float( xorstr_( "Burst pitch##aimjit" ), &globals::aiming_jitter_burst_pitch, 0.f, 25.f, "%.2f" );
				wm.checkbox( xorstr_( "Drift##aimjit" ), &globals::aiming_jitter_drift_enabled );
				wm.slider_float( xorstr_( "Drift max yaw##aimjit" ), &globals::aiming_jitter_drift_max_yaw, 0.f, 20.f, "%.2f" );
				wm.slider_float( xorstr_( "Drift max pitch##aimjit" ), &globals::aiming_jitter_drift_max_pitch, 0.f, 20.f, "%.2f" );
				wm.slider_float( xorstr_( "Drift step##aimjit" ), &globals::aiming_jitter_drift_step, 0.01f, 2.f, "%.2f" );
				wm.slider_float( xorstr_( "Drift reversion##aimjit" ), &globals::aiming_jitter_drift_reversion, 0.f, 1.f, "%.2f" );
			} );

			w.checkbox( xorstr_( "Short stop##aim" ), &globals::aiming_shortstop_enabled, nullptr, nullptr, [&]( ) {
				auto& wm = widgets_manager::get( );
				wm.slider_int( xorstr_( "Rate##aimstop" ), &globals::aiming_shortstop_rate, 1, 25, "%d%%" );
				wm.slider_int( xorstr_( "Dur min##aimstop" ), &globals::aiming_shortstop_dur_min, 1, 5, "%d" );
				wm.slider_int( xorstr_( "Dur max##aimstop" ), &globals::aiming_shortstop_dur_max, 1, 5, "%d" );
			} );


			w.checkbox( xorstr_( "Log rotation##aim" ), &globals::aiming_debug_log );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Attack modifiers" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Only critical##ka" ), &globals::killaura_only_critical );
			w.checkbox( xorstr_( "Smart critical##ka" ), &globals::killaura_smart_critical );
			w.checkbox( xorstr_( "Dynamic cooldown##ka" ), &globals::killaura_dynamic_cooldown );
			w.checkbox( xorstr_( "Break shield##ka" ), &globals::killaura_break_shield );
			w.checkbox( xorstr_( "Unpress shield##ka" ), &globals::killaura_unpress_shield );
			w.checkbox( xorstr_( "No attack when eating##ka" ), &globals::killaura_no_attack_when_eat );
			w.checkbox( xorstr_( "Ignore walls##ka" ), &globals::killaura_ignore_walls );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Target ESP" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Enabled##kaesp" ), &globals::killaura_esp_enabled );
			w.combo( xorstr_( "Style##kaesp" ), &globals::killaura_esp_type, { xorstr_( "Cube" ), xorstr_( "Circle" ), xorstr_( "Ghosts" ) } );
			if ( globals::killaura_esp_type == 2 )
				w.slider_float( xorstr_( "Ghost speed##kaesp" ), &globals::killaura_esp_ghost_speed, 0.1f, 5.f, "%.2f" );
			w.color_edit( xorstr_( "Colour##kaesp" ), &globals::killaura_esp_color.x );
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_combat_melee( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "AutoClicker" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##ac" ), &globals::autoclicker_enabled, &globals::autoclicker_keybind, &globals::autoclicker_keybind_mode );

			w.checkbox( xorstr_( "Left mouse##ac" ), &globals::autoclicker_lmb_enabled );
			if ( globals::autoclicker_lmb_enabled ) {
				w.slider_int( xorstr_( "LMB min CPS##ac" ), &globals::autoclicker_lmb_min_cps, 1, 30, "%d" );
				w.slider_int( xorstr_( "LMB max CPS##ac" ), &globals::autoclicker_lmb_max_cps, 1, 30, "%d" );
				if ( globals::autoclicker_lmb_max_cps < globals::autoclicker_lmb_min_cps )
					globals::autoclicker_lmb_max_cps = globals::autoclicker_lmb_min_cps;
				w.checkbox( xorstr_( "LMB hold only##ac" ), &globals::autoclicker_lmb_hold_only );
				w.checkbox( xorstr_( "LMB skip on block##ac" ), &globals::autoclicker_lmb_skip_on_block );
			}

			w.separator( );

			w.checkbox( xorstr_( "Right mouse##ac" ), &globals::autoclicker_rmb_enabled );
			if ( globals::autoclicker_rmb_enabled ) {
				w.slider_int( xorstr_( "RMB min CPS##ac" ), &globals::autoclicker_rmb_min_cps, 1, 30, "%d" );
				w.slider_int( xorstr_( "RMB max CPS##ac" ), &globals::autoclicker_rmb_max_cps, 1, 30, "%d" );
				if ( globals::autoclicker_rmb_max_cps < globals::autoclicker_rmb_min_cps )
					globals::autoclicker_rmb_max_cps = globals::autoclicker_rmb_min_cps;
				w.checkbox( xorstr_( "RMB hold only##ac" ), &globals::autoclicker_rmb_hold_only );
				w.checkbox( xorstr_( "RMB blocks only##ac" ), &globals::autoclicker_rmb_blocks_only );
			}
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Reach" ), { half, 0 } );
		{
			{
				const bool gated = begin_gate( sdk::caps::feature::reach );
				bind_checkbox( xorstr_( "Enabled##reach" ), &globals::reach_enabled, &globals::reach_keybind, &globals::reach_mode );
				end_gate( gated, sdk::caps::feature::reach );
			}
			slider_d( xorstr_( "Distance##reach" ), &globals::reach_distance, 3.f, 6.f, "%.2f" );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Hitbox" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##hitbox" ), &globals::hitbox_enabled, &globals::hitbox_keybind, &globals::hitbox_mode );
			slider_d( xorstr_( "X/Z expand##hitbox" ), &globals::hitbox_expand_width, 0.f, 1.f, "%.2f" );
			slider_d( xorstr_( "Y expand##hitbox" ), &globals::hitbox_expand_height, 0.f, 1.f, "%.2f" );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Shield Breaker" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##sb" ), &globals::shield_breaker_enabled, &globals::shield_breaker_keybind, &globals::shield_breaker_keybind_mode );
			w.checkbox( xorstr_( "Aim at target##sb" ), &globals::shield_breaker_aim );
			w.checkbox( xorstr_( "Switch back##sb" ), &globals::shield_breaker_switch_back );
			w.slider_int( xorstr_( "Delay##sb" ), &globals::shield_breaker_delay_ms, 0, 200, "%d ms" );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Stun Slam" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Enabled##ss" ), &globals::stun_slam_enabled );
			w.slider_float( xorstr_( "Chance##ss" ), &globals::stun_slam_chance, 0.f, 100.f, "%.0f%%" );
			w.slider_int( xorstr_( "Swap delay##ss" ), &globals::stun_slam_swap_delay_ms, 0, 50, "%d ms" );
			w.slider_int( xorstr_( "Axe delay##ss" ), &globals::stun_slam_axe_delay_ms, 0, 100, "%d ms" );
			w.slider_int( xorstr_( "Mace delay##ss" ), &globals::stun_slam_mace_delay_ms, 0, 100, "%d ms" );
			slider_d( xorstr_( "Min fall##ss" ), &globals::stun_slam_min_fall, 0.5f, 10.f, "%.1f" );
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_combat_auto( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "AutoCrystal" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##acr" ), &globals::autocrystal_enabled, &globals::autocrystal_keybind, &globals::autocrystal_mode );
			w.slider_int( xorstr_( "Delay##acr" ), &globals::autocrystal_delay_ms, 0, 500, "%d ms" );
			w.checkbox( xorstr_( "Debug logging##acr" ), &globals::autocrystal_debug_enabled );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "AutoTotem" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##at" ), &globals::autototem_enabled, &globals::autototem_keybind, &globals::autototem_mode );
			w.checkbox( xorstr_( "Rage mode##at" ), &globals::autototem_rage_mode );
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_movement( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Eagle" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##eagle" ), &globals::eagle_enabled, &globals::eagle_keybind, &globals::eagle_keybind_mode );
			w.slider_float( xorstr_( "Edge distance##eagle" ), &globals::eagle_edge_distance, 0.f, 0.5f, "%.2f" );
			w.slider_float( xorstr_( "Side sensitivity##eagle" ), &globals::eagle_side_sensitivity, 0.f, 1.f, "%.2f" );
			w.slider_float( xorstr_( "Min speed##eagle" ), &globals::eagle_min_speed, 0.f, 0.2f, "%.3f" );
			w.checkbox( xorstr_( "Always sneak##eagle" ), &globals::eagle_always );
			w.checkbox( xorstr_( "Debug overlay##eagle" ), &globals::eagle_debug_overlay );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Tapping" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "S-Tap##stap" ), &globals::stap_enabled );
			w.slider_int( xorstr_( "S duration##stap" ), &globals::stap_duration_ms, 20, 1500, "%d ms" );
			w.separator( );
			w.checkbox( xorstr_( "W-Tap##wtap" ), &globals::wtap_enabled );
			w.slider_int( xorstr_( "W release##wtap" ), &globals::wtap_duration_ms, 20, 1500, "%d ms" );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "General" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Sprint##move" ), &globals::sprint_enabled );
			w.checkbox( xorstr_( "Flight##move" ), &globals::flight_enabled );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Auto Mace" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##mace" ), &globals::mace_enabled, &globals::mace_keybind, &globals::mace_keybind_mode );
			w.checkbox( xorstr_( "Look at target##mace" ), &globals::mace_look );
			w.checkbox( xorstr_( "Switch back##mace" ), &globals::mace_switch_back );
			w.checkbox( xorstr_( "Remove elytra##mace" ), &globals::mace_remove_elytra );
			slider_d( xorstr_( "Min fall##mace" ), &globals::mace_min_fall_distance, 0.f, 20.f, "%.1f" );
			slider_d( xorstr_( "Height above##mace" ), &globals::mace_height_above_target, 0.f, 50.f, "%.1f" );
			slider_d( xorstr_( "Hitbox X##mace" ), &globals::mace_fall_hitbox_width, 0.f, 1.f, "%.2f" );
			slider_d( xorstr_( "Hitbox Y##mace" ), &globals::mace_fall_hitbox_height, 0.f, 1.f, "%.2f" );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Auto Jump Reset" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##ajr" ), &globals::autojumpreset_enabled, &globals::autojumpreset_keybind, &globals::autojumpreset_mode );
			w.slider_int( xorstr_( "Cooldown##ajr" ), &globals::autojumpreset_cooldown_ms, 0, 1000, "%d ms" );
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_visuals( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Box ESP" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##esp" ), &globals::box_enabled, &globals::esp_keybind, &globals::esp_mode );
			w.combo( xorstr_( "Style##esp" ), &globals::esp_box_style, { xorstr_( "2D box" ), xorstr_( "Corners" ), xorstr_( "3D box" ) } );
			w.color_edit( xorstr_( "Colour##esp" ), &globals::esp_color.x );
			w.checkbox( xorstr_( "Filled##esp" ), &globals::esp_box_filled );
			w.checkbox( xorstr_( "Health bar##esp" ), &globals::esp_health_bar );
			w.checkbox( xorstr_( "Tracers##esp" ), &globals::esp_tracers );
			w.separator( );
			w.checkbox( xorstr_( "In-world 3D (Java)##esp" ), &globals::esp_world_render_enabled, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).checkbox( xorstr_( "Through walls##espworld" ), &globals::esp_world_through_walls );
				widgets_manager::get( ).checkbox( xorstr_( "Filled##espworld" ), &globals::esp_world_filled );
			} );
			// The Iris/Sodium disclaimer that used to sit here was both
			// unconditional — it showed even when nothing was wrong — and no
			// longer true: nothing is hooked at all now, the renderer rides
			// Fabric's own world render event.
			if ( globals::esp_world_render_enabled && !enhance::modules::world_render_hook::is_attached( ) ) {
				TextDisabled( xorstr_( "not attached - see log" ) );
			}
			w.separator( );
			w.checkbox( xorstr_( "Distance fade##esp" ), &globals::esp_distance_fade, nullptr, nullptr, [&]( ) {
				widgets_manager::get( ).slider_float( xorstr_( "Fade start##esp" ), &globals::esp_fade_start, 0.f, 128.f, "%.0f m" );
				widgets_manager::get( ).slider_float( xorstr_( "Fade end##esp" ), &globals::esp_fade_end, 8.f, 256.f, "%.0f m" );
			} );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Storage ESP" ), { half, 0 } );
		{
			{
				const bool gated = begin_gate( sdk::caps::feature::storage_esp );
				bind_checkbox( xorstr_( "Enabled##sesp" ), &globals::storage_esp_enabled, &globals::storage_esp_keybind, &globals::storage_esp_mode );
				end_gate( gated, sdk::caps::feature::storage_esp );
			}
			w.checkbox( xorstr_( "Chests##sesp" ), &globals::storage_esp_chest );
			w.checkbox( xorstr_( "Ender chests##sesp" ), &globals::storage_esp_ender_chest );
			w.checkbox( xorstr_( "Shulkers##sesp" ), &globals::storage_esp_shulker );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Name Tags" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Enabled##nt" ), &globals::nametags_enabled );
			w.slider_float( xorstr_( "Size##nt" ), &globals::nametags_size, 0.01f, 1.f, "%.2f" );
			w.slider_float( xorstr_( "Alpha##nt" ), &globals::nametags_alpha, 0.f, 1.f, "%.2f" );
			w.slider_float( xorstr_( "Range##nt" ), &globals::nametags_range, 8.f, 128.f, "%.0f m" );
			w.checkbox( xorstr_( "Static size##nt" ), &globals::nametags_static_size );
			w.checkbox( xorstr_( "Health##nt" ), &globals::nametags_show_health );
			w.combo( xorstr_( "HP mode##nt" ), &globals::nametags_health_mode,
				{ xorstr_( "Health" ), xorstr_( "Hearts" ), xorstr_( "Percent" ) } );
			w.checkbox( xorstr_( "Distance##nt" ), &globals::nametags_show_distance );
			w.checkbox( xorstr_( "Show friends##nt" ), &globals::nametags_show_friends );
			w.color_edit( xorstr_( "Friend colour##nt" ), &globals::nametags_friend_color.x );
			w.checkbox( xorstr_( "Hide vanilla tags##nt" ), &globals::nametags_hide_vanilla );
			w.checkbox( xorstr_( "In-world (Java)##nt" ), &globals::nametags_in_world );
			if ( globals::nametags_in_world && !globals::esp_world_render_enabled ) {
				TextDisabled( xorstr_( "needs Visuals > In-world 3D (Java)" ) );
			}
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "HUD" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Enabled##hud" ), &globals::hud_enabled );
			w.checkbox( xorstr_( "Watermark##hud" ), &globals::hud_watermark_enabled );
			w.checkbox( xorstr_( "Active keybinds##hud" ), &globals::hud_keybinds_enabled );
			w.checkbox( xorstr_( "Target HUD##hud" ), &globals::hud_target_hud_enabled );
			w.checkbox( xorstr_( "Potion list##hud" ), &globals::hud_potion_list_enabled );
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_utility( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Teams" ), { half, 0 } );
		{
			{
				const bool gated = begin_gate( sdk::caps::feature::team_colours );
				w.checkbox( xorstr_( "Enabled##teams" ), &globals::teams_enabled );
				end_gate( gated, sdk::caps::feature::team_colours );
			}
			w.slider_int( xorstr_( "Colour tolerance##teams" ), &globals::teams_color_tolerance, 0, 200, "%d" );
		}
		cm.end_child( );

		cm.begin_child( xorstr_( "Pearl Catch" ), { half, 0 } );
		{
			w.binder( xorstr_( "Key##pearl" ), &globals::pearl_catch_keybind );
			w.combo( xorstr_( "Aim mode##pearl" ), &globals::pearl_catch_aim_mode, { xorstr_( "Silent (detected)" ), xorstr_( "Visible" ) } );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Server Rotation" ), { half, 0 } );
		{
			w.checkbox( xorstr_( "Enabled##srot" ), &globals::server_rotation_enabled );
			w.combo( xorstr_( "Mode##srot" ), &globals::server_rotation_mode, { xorstr_( "Look up" ), xorstr_( "Look down" ), xorstr_( "Look behind" ), xorstr_( "Custom" ) } );
			if ( globals::server_rotation_mode == 3 ) {
				w.slider_float( xorstr_( "Yaw##srot" ), &globals::server_rotation_custom_yaw, -180.f, 180.f, "%.0f" );
				w.slider_float( xorstr_( "Pitch##srot" ), &globals::server_rotation_custom_pitch, -90.f, 90.f, "%.0f" );
			}
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_macros( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Anchor Macro" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##amacro" ), &globals::anchor_macro_enabled, &globals::anchor_macro_keybind, &globals::anchor_macro_mode );
			w.checkbox( xorstr_( "Explode at end##amacro" ), &globals::anchor_macro_break_anchor );
			w.slider_int( xorstr_( "Swap delay##amacro" ), &globals::anchor_macro_swap_delay_ms, 40, 200, "%d ms" );
			w.slider_int( xorstr_( "Charge delay##amacro" ), &globals::anchor_macro_charge_delay_ms, 80, 300, "%d ms" );
			w.slider_int( xorstr_( "Explode delay##amacro" ), &globals::anchor_macro_break_delay_ms, 40, 200, "%d ms" );
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Auto Anchor" ), { half, 0 } );
		{
			bind_checkbox( xorstr_( "Enabled##aanchor" ), &globals::auto_anchor_enabled, &globals::auto_anchor_keybind, &globals::auto_anchor_mode );
			w.slider_int( xorstr_( "Swap delay##aanchor" ), &globals::auto_anchor_swap_delay_ms, 40, 200, "%d ms" );
			w.slider_int( xorstr_( "Charge delay##aanchor" ), &globals::auto_anchor_charge_delay_ms, 80, 300, "%d ms" );
		}
		cm.end_child( );
	}
	EndGroup( );
}

void page_settings( ) {
	auto& cm = child_manager::get( );
	auto& w = widgets_manager::get( );
	const float half = child_half_width( );

	BeginGroup( );
	{
		cm.begin_child( xorstr_( "Menu" ), { half, 0 } );
		{
			w.binder( xorstr_( "Open key##gui" ), &globals::gui_keybind );
			w.binder( xorstr_( "Unload key##gui" ), &globals::unload_keybind );
			w.slider_int( xorstr_( "Unload level##gui" ), &globals::unload_level, 0, 4, "%d" );
			TextDisabled( xorstr_( "0 worker  1 +imgui  2 +hooks" ) );
			TextDisabled( xorstr_( "3 +jni    4 +unmap" ) );
			TextDisabled( xorstr_( "raise until it crashes to find the culprit" ) );
			if ( w.color_edit( xorstr_( "Accent##gui" ), g_accent ) ) {
				GImGui->Style.Colors[ImGuiCol_Scheme] = ImVec4{ g_accent[0], g_accent[1], g_accent[2], 1.f };
			}
		}
		cm.end_child( );
	}
	EndGroup( );
	SameLine( );
	BeginGroup( );
	{
		cm.begin_child( xorstr_( "About" ), { half, 0 } );
		{
			TextDisabled( xorstr_( "enhance client" ) );
			TextDisabled( xorstr_( "Right Shift toggles this menu by default." ) );

			// What the client thinks it is attached to. "It does nothing" and "it
			// bound to the wrong table" look identical from the outside, so the
			// detection result belongs on screen, not only in the log.
			Spacing( );
			const int missing = ( int )sdk::mappings::unresolved( ).size( );
			const int total = sdk::mappings::symbol_count( );
			if ( !sdk::mappings::bound( ) )
			{
				TextColored( ImVec4( 1.f, .35f, .35f, 1.f ),
					xorstr_( "mappings not bound - nothing will work" ) );
			}
			else
			{
				Text( xorstr_( "Minecraft %s" ), sdk::version::name( ) );
				TextDisabled( xorstr_( "%s names, %d/%d symbols resolved" ),
					sdk::version::ns_name( ), total - missing, total );
				if ( !sdk::version::exact( ) )
				{
					TextColored( ImVec4( 1.f, .75f, .3f, 1.f ),
						xorstr_( "reported %s - using the %s table" ),
						sdk::version::reported( ), sdk::version::name( ) );
				}
				if ( missing > 0 && IsItemHovered( ) )
					SetTooltip( xorstr_( "%d symbols this version does not have; see the log" ), missing );
			}
		}
		cm.end_child( );
	}
	EndGroup( );
}

} // namespace

// ---------------------------------------------------------------------------

void menu::init( ) {
	if ( m_init )
		return;

	style_manager::get( ).styles( );
	style_manager::get( ).colors( );

	fonts.resize( fonts_size );

	fonts.at( font ).setup( b_font, sizeof( b_font ),
		{ 12, 14 },
		GetIO( ).Fonts->GetGlyphRangesCyrillic( ) );

	const static ImWchar icons_ranges[] = { 0x1 + 16000, 0x1 + 17170, 0 };
	fonts.at( icons ).setup( glyphter, sizeof( glyphter ),
		{ 12, 14 },
		icons_ranges );

	GetIO( ).FontDefault = fonts[font].get( 14.f );

	// The embedded icon font does carry glyphs for the requested block
	// (verified against its cmap: U+3E80..U+4311), so if icons are missing the
	// fault is on this side — a font that failed to load, or a lookup that
	// falls back to the text font, which has nothing in that block and draws
	// nothing at all.
	{
		ImFont* icon_14 = fonts[icons].get( 14.f );
		ImFont* icon_12 = fonts[icons].get( 12.f );
		logger::log( "[fonts] atlas=" + std::to_string( GetIO( ).Fonts->Fonts.Size ) +
		             " text_sizes=" + std::to_string( (int)fonts[font].fonts.size( ) ) +
		             " icon_sizes=" + std::to_string( (int)fonts[icons].fonts.size( ) ) +
		             " icon14=" + ( icon_14 ? "ok" : "NULL" ) +
		             " icon12=" + ( icon_12 ? "ok" : "NULL" ) );

		if ( icon_14 )
		{
			// Ask the font itself whether it baked a glyph we actually use.
			const ImFontGlyph* g = icon_14->FindGlyphNoFallback( (ImWchar)0x4033 ); // i_eye
			logger::log( std::string( "[fonts] i_eye glyph: " ) + ( g ? "present" : "MISSING" ) );
		}
	}

	// A single language keeps lang_manager's translate() a pass-through while
	// still driving the navbar selector.
	lang_manager::get( ).add_language( xorstr_( "English" ), font, { } );

	const ImVec4 scheme = GImGui->Style.Colors[ImGuiCol_Scheme];
	g_accent[0] = scheme.x; g_accent[1] = scheme.y; g_accent[2] = scheme.z; g_accent[3] = 1.f;

	tabs_manager::get( ).add_page( 0, page_combat_aim );
	tabs_manager::get( ).add_page( 0, page_combat_melee );
	tabs_manager::get( ).add_page( 0, page_combat_auto );
	tabs_manager::get( ).add_page( 1, page_movement );
	tabs_manager::get( ).add_page( 2, page_visuals );
	tabs_manager::get( ).add_page( 3, page_utility );
	tabs_manager::get( ).add_page( 4, page_macros );
	tabs_manager::get( ).add_page( 5, page_settings );

	m_init = true;
}

void menu::draw( ) {
	if ( !m_init )
		init( );

	SetNextWindowSize( m_size, ImGuiCond_Once );
	PushStyleColor( ImGuiCol_WindowBg, ImVec4{ 0.f, 0.f, 0.f, 0.f } );
	Begin( xorstr_( "enhance" ), 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus );
	{
		// D3D11 backdrop blur from the source client has no OpenGL counterpart
		// yet, so the window paints a flat near-opaque panel instead.
		GetWindowDrawList( )->AddRectFilled( GetWindowPos( ), GetWindowPos( ) + GetWindowSize( ), IM_COL32( 12, 13, 18, ( int )( 245.f * GImGui->Style.Alpha ) ), 4.f );

		BeginChild( xorstr_( "navbar" ), { 180, 0 }, 0, ImGuiWindowFlags_NoBackground );
		{
			SetCursorPos( ImVec2{ 14, 14 } );
			PushStyleColor( ImGuiCol_FrameBg, GetColorU32( ImGuiCol_FrameBg, 0.f ) );
			PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1 );
			PushItemFlag( ImGuiItemFlags_NoNav, true );
			if ( widgets_manager::get( ).text_field( xorstr_( "##search" ), search_manager::get( ).search_buf, sizeof( search_manager::get( ).search_buf ), { GetWindowWidth( ) - 28, 0 }, xorstr_( "Search..." ), i_search_md ) ) {
				search_manager::get( ).update( );
			}
			PopItemFlag( );
			PopStyleVar( );
			PopStyleColor( );

			widgets_manager::get( ).separator( );
			SetCursorPosX( 14 );
			tabs_manager::get( ).render_tabs( 8.f );

			GetWindowDrawList( )->AddRectFilled( GetWindowPos( ) + ImVec2{ GetWindowWidth( ) - 1, 0 }, GetWindowPos( ) + GetWindowSize( ), GetColorU32( ImGuiCol_Border ) );
		}
		EndChild( );

		SameLine( 0, 0 );

		const bool searching = strlen( search_manager::get( ).search_buf ) > 0 || search_manager::get( ).get_anim( ) > 0.05f;

		if ( searching ) PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 14, 14 } );
		BeginChild( xorstr_( "main" ), { 0, 0 }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
		{
			if ( searching ) PopStyleVar( );

			SetCursorPos( { 0, 0 } );

			child_manager::get( ).smooth_scroll( );

			if ( !searching ) {
				tabs_manager::get( ).render_subtabs( 20.f );

				auto window = GetCurrentWindow( );

				PushStyleVar( ImGuiStyleVar_Alpha, GImGui->Style.Alpha * tabs_manager::get( ).get_anim( ) );
				PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 14, 14 } );
				BeginChild( xorstr_( "content" ), { 0, 0 }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
				{
					tabs_manager::get( ).draw_page( window );
				}
				EndChild( );
				PopStyleVar( 2 );
			} else {
				search_manager::get( ).draw( );
			}
		}
		EndChild( );

		popup_manager::get( ).handle( );
	}
	End( );
	PopStyleColor( );
}
