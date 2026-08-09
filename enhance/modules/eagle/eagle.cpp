#include "eagle.h"
#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../hooks/Hook.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <windows.h>
#include <cmath>

// Eagle — auto-sneak when standing near a block edge that opens into air.
//
// The Right Way Here(tm) is to flip MinecraftClient.options.sneakKey's
// `pressed` field directly via KeyBinding.setPressed(boolean). MC's Input
// tick reads this each game tick as the canonical sneak state, so writing
// it from the worker thread cleanly overrides the GLFW-driven key state
// for that tick. No SendInput, no hook, no KeyboardInput.tick race —
// vanilla just sees "user is holding Shift" and the edge clamp, packet,
// pose all run normally.
//
// We never read the user's physical Shift here: KeyBinding.pressed is also
// what GLFW writes when the user really presses Shift, so an OR is implicit
// at the source. When we no longer want sneak we only release if WE were
// the one who pressed, so we never steal a real hold.

namespace enhance::modules::eagle
{

// ---- Cached JNI ids (resolved lazily on the worker thread) -----------
static bool      g_jni_ready              = false;
static jclass    g_world_cls              = nullptr;
static jmethodID g_world_get_state_mid    = nullptr;
static jclass    g_block_state_cls        = nullptr;
static jmethodID g_block_state_is_air_mid = nullptr;
static jclass    g_block_pos_cls          = nullptr;
static jmethodID g_block_pos_ctor         = nullptr;
static jmethodID g_keybinding_set_pressed = nullptr;
static jfieldID  g_mc_options_fid         = nullptr;
static jfieldID  g_options_sneak_key_fid  = nullptr;

// "we pressed Shift" latch — only true while WE issued setPressed(true)
// without the user already holding it. Used to know when it's safe to
// setPressed(false).
static bool s_we_pressed = false;

// Live diagnostic published to the debug overlay (GUI.cpp reads via the
// status getters below). Updated at the end of every run().
struct DebugStatus
{
	bool  on_ground;
	double fall_distance;
	bool  probe_air[4];
	bool  want_sneak;
	bool  set_pressed_attempted;
	bool  jni_ok;
};
static DebugStatus s_dbg{};

const char* debug_status_string(char* out, size_t out_size)
{
	if (!out || out_size == 0) return "";
	snprintf(out, out_size,
		"Eagle: %s  ground=%d  fall=%.2f  probes(fwd,R,L)=%d%d%d  want=%d  jni=%d",
		globals::eagle_enabled ? "ON" : "off",
		s_dbg.on_ground ? 1 : 0,
		s_dbg.fall_distance,
		s_dbg.probe_air[0] ? 1 : 0,
		s_dbg.probe_air[1] ? 1 : 0,
		s_dbg.probe_air[2] ? 1 : 0,
		s_dbg.want_sneak ? 1 : 0,
		s_dbg.jni_ok ? 1 : 0);
	return out;
}

static bool resolve_ids(JNIEnv* env)
{
	if (g_jni_ready) return true;

	jclass world_cls = sdk::classloader::find_class(env, sdk::mappings::world_class_sig);
	jclass bs_cls    = sdk::classloader::find_class(env, sdk::mappings::block_state_class_sig);
	jclass bp_cls    = sdk::classloader::find_class(env, sdk::mappings::block_pos_class_sig);
	jclass mc_cls    = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
	jclass opts_cls  = sdk::classloader::find_class(env, sdk::mappings::gameoptions_class_sig);
	jclass kb_cls    = sdk::classloader::find_class(env, sdk::mappings::keybinding_class_sig);

	if (!world_cls || !bs_cls || !bp_cls || !mc_cls || !opts_cls || !kb_cls)
	{
		if (world_cls) env->DeleteLocalRef(world_cls);
		if (bs_cls)    env->DeleteLocalRef(bs_cls);
		if (bp_cls)    env->DeleteLocalRef(bp_cls);
		if (mc_cls)    env->DeleteLocalRef(mc_cls);
		if (opts_cls)  env->DeleteLocalRef(opts_cls);
		if (kb_cls)    env->DeleteLocalRef(kb_cls);
		return false;
	}

	g_world_get_state_mid    = env->GetMethodID(world_cls,
		sdk::mappings::world_get_block_state_name,
		sdk::mappings::world_get_block_state_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_block_state_is_air_mid = env->GetMethodID(bs_cls,
		sdk::mappings::block_state_is_air_name,
		sdk::mappings::block_state_is_air_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_block_pos_ctor         = env->GetMethodID(bp_cls, "<init>", "(III)V");
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_mc_options_fid         = env->GetFieldID(mc_cls,
		sdk::mappings::mc_options_field_name,
		sdk::mappings::mc_options_field_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_options_sneak_key_fid  = env->GetFieldID(opts_cls,
		sdk::mappings::gameoptions_sneak_key_name,
		sdk::mappings::gameoptions_sneak_key_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	g_keybinding_set_pressed = env->GetMethodID(kb_cls,
		sdk::mappings::keybinding_set_pressed_name,
		sdk::mappings::keybinding_set_pressed_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();

	g_world_cls       = (jclass)env->NewGlobalRef(world_cls);
	g_block_state_cls = (jclass)env->NewGlobalRef(bs_cls);
	g_block_pos_cls   = (jclass)env->NewGlobalRef(bp_cls);

	env->DeleteLocalRef(world_cls);
	env->DeleteLocalRef(bs_cls);
	env->DeleteLocalRef(bp_cls);
	env->DeleteLocalRef(mc_cls);
	env->DeleteLocalRef(opts_cls);
	env->DeleteLocalRef(kb_cls);

	g_jni_ready = g_world_cls && g_block_state_cls && g_block_pos_cls
	           && g_world_get_state_mid && g_block_state_is_air_mid && g_block_pos_ctor
	           && g_mc_options_fid && g_options_sneak_key_fid && g_keybinding_set_pressed;
	return g_jni_ready;
}

static bool block_is_air(JNIEnv* env, jobject world, int x, int y, int z)
{
	if (!world) return false;
	jobject pos = env->NewObject(g_block_pos_cls, g_block_pos_ctor, (jint)x, (jint)y, (jint)z);
	if (env->ExceptionCheck()) { env->ExceptionClear(); }
	if (!pos) return false;
	jobject state = env->CallObjectMethod(world, g_world_get_state_mid, pos);
	if (env->ExceptionCheck()) { env->ExceptionClear(); }
	env->DeleteLocalRef(pos);
	if (!state) return false;
	jboolean air = env->CallBooleanMethod(state, g_block_state_is_air_mid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); }
	env->DeleteLocalRef(state);
	return air == JNI_TRUE;
}

// Set the sneakKey KeyBinding's pressed state directly. This writes the
// same field GLFW's key callback writes when the user presses Shift, so
// Input.tick() reads the new value next game tick and the rest of MC's
// sneak pipeline triggers naturally.
static void set_sneak_key(JNIEnv* env, bool pressed)
{
	if (!g_mc_options_fid || !g_options_sneak_key_fid || !g_keybinding_set_pressed) return;

	jobject mc = sdk::instance ? sdk::instance->get_minecraft() : nullptr;
	if (!mc) return;
	jobject options = env->GetObjectField(mc, g_mc_options_fid);
	env->DeleteLocalRef(mc);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
	if (!options) return;
	jobject sneak_key = env->GetObjectField(options, g_options_sneak_key_fid);
	env->DeleteLocalRef(options);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
	if (!sneak_key) return;

	env->CallVoidMethod(sneak_key, g_keybinding_set_pressed, pressed ? JNI_TRUE : JNI_FALSE);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(sneak_key);
}

static bool user_holding_shift()
{
	return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0
	    || (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
}

static void release_if_ours(JNIEnv* env)
{
	if (s_we_pressed)
	{
		set_sneak_key(env, false);
		s_we_pressed = false;
	}
}

void run()
{
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return;

	if (!globals::eagle_enabled || globals::show_gui)
	{
		if (s_we_pressed) release_if_ours(env);
		return;
	}

	if (!sdk::instance) return;
	if (!resolve_ids(env)) return;

	jobject local_player = sdk::instance->get_player();
	if (!local_player) { release_if_ours(env); return; }
	jobject world = sdk::instance->get_world();
	if (!world) { env->DeleteLocalRef(local_player); release_if_ours(env); return; }

	sdk::entity_client le(local_player);
	const bool on_ground = le.is_on_ground();
	const double px = le.get_x();
	const double py = le.get_y();
	const double pz = le.get_z();
	const double fall = le.get_fall_distance();

	// Horizontal velocity — drives directional probing. The forward probe
	// (along velocity) gets full sensitivity; the two perpendicular probes
	// scale down by globals::eagle_side_sensitivity so strafing isn't
	// punished as eagerly. We DON'T probe behind us — you can't walk off
	// the back of where you came from.
	double vx = 0.0, vz = 0.0;
	{
		jobject vel = le.get_velocity();
		if (vel)
		{
			jclass vec_cls = env->GetObjectClass(vel);
			if (vec_cls)
			{
				jfieldID fx = env->GetFieldID(vec_cls, sdk::mappings::vec3d_x_name, sdk::mappings::vec3d_x_sig);
				jfieldID fz = env->GetFieldID(vec_cls, sdk::mappings::vec3d_z_name, sdk::mappings::vec3d_z_sig);
				if (fx) vx = env->GetDoubleField(vel, fx);
				if (fz) vz = env->GetDoubleField(vel, fz);
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(vec_cls);
			}
			env->DeleteLocalRef(vel);
		}
	}
	const double speed_h = std::sqrt(vx * vx + vz * vz);

	bool want_sneak = false;
	bool probes_air[4] = { false, false, false, false };
	const bool moving = speed_h > static_cast<double>(globals::eagle_min_speed);

	if (globals::eagle_always)
	{
		// Diagnostic: skip edge detector entirely.
		want_sneak = true;
	}
	else if (on_ground && fall <= 0.5 && moving)
	{
		// Unit vector along movement (forward).
		const double fwd_x = vx / speed_h;
		const double fwd_z = vz / speed_h;
		// Left-perpendicular (90° CCW in XZ): (-fz, +fx). Right is the
		// opposite. We probe along forward and ±perpendicular.
		const double per_x = -fwd_z;
		const double per_z =  fwd_x;

		const double half     = 0.3;
		const double thr_fwd  = static_cast<double>(globals::eagle_edge_distance);
		const double thr_side = thr_fwd * static_cast<double>(globals::eagle_side_sensitivity);
		const double off_fwd  = half + thr_fwd;
		const double off_side = half + thr_side;
		// Block coordinate of the surface the player is currently standing
		// on. player.y is at the top of that block, so floor(py - eps) gives
		// the block's integer Y. Previously we had a stray `- 1` here which
		// made every probe look at the block *below* the support, which is
		// usually air on bridges — so Eagle stayed engaged across the whole
		// bridge and was nearly impossible to release for ninja-bridging.
		const int support_y = static_cast<int>(std::floor(py - 0.05));

		struct Probe { double dx, dz; int idx; };
		const Probe probes[3] = {
			{  fwd_x * off_fwd,   fwd_z * off_fwd,  0 },    // forward
			{ -per_x * off_side, -per_z * off_side, 1 },    // right side
			{  per_x * off_side,  per_z * off_side, 2 },    // left side
		};

		for (const Probe& p : probes)
		{
			const int qx = static_cast<int>(std::floor(px + p.dx));
			const int qz = static_cast<int>(std::floor(pz + p.dz));
			const bool air = block_is_air(env, world, qx, support_y, qz);
			probes_air[p.idx] = air;
			if (air) want_sneak = true;
		}
	}

	// Publish diagnostic snapshot. probes_air slot order in the overlay:
	//   [0] forward (along movement)
	//   [1] right perpendicular
	//   [2] left perpendicular
	//   [3] unused — kept for ABI parity with the previous 4-cardinal layout
	s_dbg.on_ground               = on_ground;
	s_dbg.fall_distance           = fall;
	s_dbg.probe_air[0]            = probes_air[0];
	s_dbg.probe_air[1]            = probes_air[1];
	s_dbg.probe_air[2]            = probes_air[2];
	s_dbg.probe_air[3]            = probes_air[3];
	s_dbg.want_sneak              = want_sneak;
	s_dbg.set_pressed_attempted   = false;
	s_dbg.jni_ok                  = g_jni_ready;

	env->DeleteLocalRef(world);
	env->DeleteLocalRef(local_player);

	if (want_sneak)
	{
		// Re-assert every tick so the most recent write into KeyBinding.
		// pressed is ours; Input.tick reads it at the next game tick.
		if (!user_holding_shift())
		{
			set_sneak_key(env, true);
			s_we_pressed = true;
			s_dbg.set_pressed_attempted = true;
		}
		else
		{
			// User has their own hold — leave it alone, clear our latch.
			s_we_pressed = false;
		}
	}
	else
	{
		release_if_ours(env);
	}
}

}
