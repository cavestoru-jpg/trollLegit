#include "world_render_hook.h"

#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include "../../utils/client_thread.h"
#include "../../java/enhance_renderer_class.hpp"
#include "../killaura/friends.h"

#include <jnihook.h>

#include <imgui.h>

#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/render/render_view.h>
#include <sdk/java/runtime_class.h>
#include <sdk/java/jvmti_dump.h>
#include <sdk/java/jvmti_env.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Draws the ESP as real geometry into Minecraft's own frame, while the depth
// buffer from the world pass is still bound, rather than as a flat overlay
// after the frame is finished.
//
// Depth is the whole point: testing against it gives real occlusion by
// terrain, and drawing before the HUD leaves the HUD on top instead of
// underneath. It also runs once per frame rather than once per tick, so the
// boxes interpolate with the entity models instead of stepping at 20 Hz.
//
// HOW IT ATTACHES: it subscribes to Fabric's WorldRenderEvents through a
// java.lang.reflect.Proxy. No Minecraft class is hooked, redefined or touched.
//
// That is the fourth approach tried here, and the three dead ends are worth
// recording because each looked correct until it ran:
//
//   - JNIHook on WorldRenderer.render blanked the world. NOT because
//     redefining drops Mixin's work — JNIHook_ProbeClassMixins measured the
//     opposite, the bytes handed to us for redefinition already contain
//     Mixin's members (see tick_movement_hook.cpp, which dropped its own guard
//     over exactly this). The method is the problem: Sodium and Iris rewrite
//     the body of render, and JNIHook replaces the body it hooks with a native
//     stub, so their injected code goes with it.
//
//   - A JVMTI breakpoint redefines nothing and would have been ideal, but
//     can_generate_breakpoint_events is onload-solo in HotSpot:
//     AddCapabilities returns JVMTI_ERROR_NOT_AVAILABLE (98) to anything that
//     arrives after the VM is up, which a DLL injection always does. Debuggers
//     get it because the VM starts with -agentlib:jdwp.
//
//   - JNIHook on InGameHud.render, picked as a method mods leave alone, broke
//     the HUD: fabric-rendering-v1 wraps operations inside it
//     (wrapOperation$..$wrapStatusEffectOverlay). On a Fabric install there is
//     no reliable "clean" method in the render path — any mod may claim any of
//     them — so the whole strategy of hooking one was abandoned.
//
// Subscribing to the event has none of those failure modes, and it is the
// mechanism Iris and Sodium are themselves built to cooperate with. The cost
// is a hard dependency on Fabric API being present, which is checked at
// registration and reported rather than assumed.
//
// THREADING: the callback runs on Minecraft's render thread. A JNIEnv is
// thread-local, so every JNI call below MUST go through the `env` handed to
// the callback, never enhance::instance->get_env() (bound to the worker
// thread). Method and field IDs are thread-safe once resolved, so they are
// cached at init() and only used here.

namespace
{
	constexpr const char* k_renderer_binary_name = "enhance/EnhanceRenderer";
	constexpr int k_floats_per_vertex = 7;   // x, y, z, r, g, b, a

	bool g_attached = false;
	bool g_natives_registered = false;

	// Note the `world` package: Fabric moved WorldRenderEvents there, and the
	// first attempt at this looked exactly like a missing Fabric API until the
	// JVM was asked what it actually had loaded.
	constexpr const char* k_events_class =
		"net/fabricmc/fabric/api/client/rendering/v1/world/WorldRenderEvents";

	// Which event to subscribe to is NOT hardcoded — it is read off the class
	// at runtime. The same version bump that moved the package also renamed the
	// events (LAST and AFTER_TRANSLUCENT are gone, END_MAIN and
	// BEFORE_TRANSLUCENT exist instead), and a hardcoded list would break again
	// on the next one. The fields are enumerated over JVMTI and matched against
	// the order below; anything unlisted is still tried, just last.
	//
	// Ordered by where in the frame the boxes want to land: as late in the
	// world pass as possible, while the depth buffer is still bound.
	constexpr const char* k_event_preference[] = {
		"END_MAIN", "LAST", "END", "AFTER_TRANSLUCENT",
		"BEFORE_TRANSLUCENT", "AFTER_ENTITIES", "DEBUG_RENDER",
	};

	// Fabric pairs each event field with a single-method interface whose name
	// is the field in CamelCase: END_MAIN -> WorldRenderEvents$EndMain.
	std::string field_to_interface_name(const std::string& field)
	{
		std::string out;
		bool upper = true;

		for (char c : field)
		{
			if (c == '_')
			{
				upper = true;
				continue;
			}

			out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
			             : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			upper = false;
		}

		return out;
	}

	// Java renderer
	jclass    g_renderer_class = nullptr;    // GlobalRef
	jmethodID g_renderer_render = nullptr;

	// Vertex staging. Native owns the memory; Java wraps it once with a direct
	// ByteBuffer so nothing is copied or allocated per frame.
	std::vector<float> g_tris;
	std::vector<float> g_lines;
	std::vector<float> g_packed;
	jobject   g_packed_buffer = nullptr;     // GlobalRef to the direct ByteBuffer
	size_t    g_packed_buffer_floats = 0;    // capacity the buffer was built for

	// Minecraft accessors, all resolved once at init().
	jclass    g_mc_class = nullptr;           // GlobalRef
	jfieldID  g_fid_mc_instance = nullptr;
	jfieldID  g_fid_mc_world = nullptr;
	jfieldID  g_fid_mc_player = nullptr;
	jfieldID  g_fid_mc_tick_counter = nullptr;   // optional

	jclass    g_world_class = nullptr;        // GlobalRef
	jfieldID  g_fid_world_players = nullptr;

	jclass    g_list_class = nullptr;         // GlobalRef
	jmethodID g_mid_list_size = nullptr;
	jmethodID g_mid_list_get = nullptr;

	jclass    g_entity_class = nullptr;       // GlobalRef
	jmethodID g_mid_entity_get_x = nullptr;
	jmethodID g_mid_entity_get_y = nullptr;
	jmethodID g_mid_entity_get_z = nullptr;
	jfieldID  g_fid_entity_box = nullptr;
	jfieldID  g_fid_last_render_x = nullptr;  // optional
	jfieldID  g_fid_last_render_y = nullptr;  // optional
	jfieldID  g_fid_last_render_z = nullptr;  // optional

	// Name tags. All optional: a missing id costs the tag, never the boxes.
	jmethodID g_mid_scoreboard_name = nullptr;
	jclass    g_living_class = nullptr;       // GlobalRef
	jmethodID g_mid_get_health = nullptr;
	jmethodID g_mid_get_max_health = nullptr;

	// --- the submit-node backend, 1.21.9 and later -----------------------
	//
	// From 1.21.9 the frame queues geometry into LevelRenderer's own storage and
	// drains it by phase, and submitCustomGeometry is the game's declared way in
	// for anyone else's. Everything here sits above the backend split, so the
	// game draws our boxes on OpenGL or Vulkan without this client knowing which.
	//
	// It also needs no Fabric API, which the event route does, and no mod is
	// disturbed: Sodium and Iris both mix into LevelRenderer, but into
	// renderLevel -- the method the abandoned JNIHook took, and why the world
	// went blank. Neither names submitEntities.
	jclass    g_ordered_cls = nullptr;          // GlobalRef
	jmethodID g_mid_order = nullptr;
	jmethodID g_mid_submit_custom = nullptr;
	jmethodID g_mid_stage_geometry = nullptr;
	jobject   g_rt_lines = nullptr;             // GlobalRef
	jobject   g_rt_tris = nullptr;              // GlobalRef
	jobject   g_proxy_tris = nullptr;           // GlobalRef
	jobject   g_proxy_lines = nullptr;          // GlobalRef
	bool      g_submit_ready = false;

	// Set only for the length of the submitEntities hook, which is the one moment
	// the collector exists and the frame is taking entity geometry.
	jobject   g_frame_pose = nullptr;
	jobject   g_frame_collector = nullptr;
	jobject   g_frame_state = nullptr;          // LevelRenderState, for the frame's camera

	// Name tags, all optional: a missing id costs the tags, never the boxes.
	jmethodID g_mid_submit_name_tag = nullptr;
	jfieldID  g_fid_lrs_camera = nullptr;
	jobject   g_vec3_zero = nullptr;            // GlobalRef
	jclass    g_component_cls = nullptr;        // GlobalRef
	jmethodID g_mid_component_of = nullptr;
	jmethodID g_mid_pose_push = nullptr;
	jmethodID g_mid_pose_pop = nullptr;
	jmethodID g_mid_pose_translate = nullptr;
	bool      g_tags_ready = false;

	// Our own tags, drawn the way the overlay draws them, through a render type
	// bound to our glyph atlas. Falls back to the game's tag renderer if the
	// atlas cannot be handed over.
	jclass    g_cgr_cls = nullptr;              // GlobalRef
	bool      g_atlas_tried = false;

	jmethodID g_renderer_render_text = nullptr;

	// Which render type each half ends up drawn with, and what it wants per
	// vertex. Declared here because submit_boxes reads them.
	bool      g_tris_as_quads = false;
	int       g_mask_tris = 0;
	int       g_mask_lines = 0;
	jmethodID g_mid_set_masks = nullptr;
	jmethodID g_mid_set_quads = nullptr;
	jobject   g_rt_text = nullptr;              // GlobalRef
	jobject   g_proxy_text = nullptr;           // GlobalRef
	int       g_mask_text = 0;
	bool      g_text_as_quads = false;
	jmethodID g_renderer_upload_font = nullptr;
	bool      g_font_uploaded = false;

	// Text vertices: x, y, z, u, v, r, g, b, a
	constexpr int k_floats_per_text_vertex = 9;
	std::vector<float> g_text;
	jobject   g_text_buffer = nullptr;        // GlobalRef to the direct ByteBuffer
	size_t    g_text_buffer_floats = 0;

	jclass    g_box_class = nullptr;          // GlobalRef
	jfieldID  g_fid_box_min_x = nullptr;
	jfieldID  g_fid_box_min_y = nullptr;
	jfieldID  g_fid_box_min_z = nullptr;
	jfieldID  g_fid_box_max_x = nullptr;
	jfieldID  g_fid_box_max_y = nullptr;
	jfieldID  g_fid_box_max_z = nullptr;

	jmethodID g_mid_tick_progress = nullptr;  // optional

	// No render-method mapping is needed any more — nothing is hooked. What
	// remains required is what submit_frame reads to build the boxes.
	bool mapping_present()
	{
		return sdk::mappings::minecraftclass_sig[0] != '\0'
		    && sdk::mappings::entity_class_sig[0] != '\0';
	}

	void clear_exception(JNIEnv* env)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
	}

	// Same, but says what it swallowed.
	//
	// Every JNI call in this file used to end in a bare clear_exception, which
	// is right for the probing ones — a missing field is expected — and wrong
	// for the draw calls: a throw inside the Java renderer looked exactly like
	// a frame that drew nothing. Reported once per distinct site, because this
	// runs every frame.
	void report_exception(JNIEnv* env, const char* where)
	{
		if (!env->ExceptionCheck())
			return;

		jthrowable thrown = env->ExceptionOccurred();
		env->ExceptionClear();

		std::string detail;
		if (thrown)
		{
			if (jclass cls = env->GetObjectClass(thrown))
			{
				if (jmethodID to_string = env->GetMethodID(cls, "toString", "()Ljava/lang/String;"))
				{
					if (jstring s = static_cast<jstring>(env->CallObjectMethod(thrown, to_string)))
					{
						if (const char* utf = env->GetStringUTFChars(s, nullptr))
						{
							detail = utf;
							env->ReleaseStringUTFChars(s, utf);
						}
						env->DeleteLocalRef(s);
					}
				}
				env->DeleteLocalRef(cls);
			}
			env->DeleteLocalRef(thrown);
		}

		if (env->ExceptionCheck())
			env->ExceptionClear();

		static std::vector<std::string> reported;
		const std::string key = std::string(where) + detail;
		if (std::find(reported.begin(), reported.end(), key) != reported.end())
			return;
		reported.push_back(key);

		logger::log_error(std::string("[world_render] ") + where + " threw: " +
		                  (detail.empty() ? "(no detail)" : detail));
	}

	jclass global_class(JNIEnv* env, const char* sig)
	{
		jclass local = sdk::classloader::find_class(env, sig);
		clear_exception(env);
		if (!local)
			return nullptr;

		jclass global = static_cast<jclass>(env->NewGlobalRef(local));
		env->DeleteLocalRef(local);
		return global;
	}

	// Edges as quads rather than lines, and the scale that keeps them a constant
	// width on screen. Only set when the submit path is running with a
	// see-through render type to put them through -- otherwise lines() is
	// better geometry for a line.
	bool  g_edges_as_quads = false;
	float g_edge_scale = 0.0f;      // metres per pixel, per metre of distance

	// Thickness in pixels. Matches what lines() draws closely enough that
	// toggling "through walls" does not visibly change the outline's weight.
	constexpr float k_edge_px = 1.8f;

	void push_vertex(std::vector<float>& out, const float p[3], const float c[4])
	{
		out.push_back(p[0]); out.push_back(p[1]); out.push_back(p[2]);
		out.push_back(c[0]); out.push_back(c[1]); out.push_back(c[2]); out.push_back(c[3]);
	}

	// One edge, as a quad lying in the plane that faces the camera.
	//
	// The geometry is camera-relative, so the direction to the edge is its own
	// midpoint and the sideways direction is that crossed with the edge. Scaling
	// the half-width by the edge's distance is what makes the thickness constant
	// in pixels rather than in metres.
	void push_edge_quad(const float a[3], const float b[3], const float col[4])
	{
		const float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const float m[3] = { (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f };

		const float dist = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
		if (dist <= 1.0e-4f || g_edge_scale <= 0.0f)
			return;

		float side[3] = {
			d[1] * m[2] - d[2] * m[1],
			d[2] * m[0] - d[0] * m[2],
			d[0] * m[1] - d[1] * m[0],
		};
		const float len = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
		if (len <= 1.0e-6f)
			return;   // the edge points straight at the camera; nothing to widen

		const float half = 0.5f * k_edge_px * dist * g_edge_scale;
		for (float& v : side)
			v = v / len * half;

		const float p0[3] = { a[0] - side[0], a[1] - side[1], a[2] - side[2] };
		const float p1[3] = { a[0] + side[0], a[1] + side[1], a[2] + side[2] };
		const float p2[3] = { b[0] + side[0], b[1] + side[1], b[2] + side[2] };
		const float p3[3] = { b[0] - side[0], b[1] - side[1], b[2] - side[2] };

		// Wind it so it faces the camera, the same way the tags had to be.
		//
		// This is the second time this bill has come due: a box survives being
		// wound the wrong way because its twelve faces point everywhere and half
		// are always right, but a flat quad has one good side and these have
		// exactly the same problem the glyph quads did. The camera is at the
		// origin here too, so the test is the same -- the normal must point back
		// along the direction to the surface.
		const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
		const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
		const float n[3] = {
			e1[1] * e2[2] - e1[2] * e2[1],
			e1[2] * e2[0] - e1[0] * e2[2],
			e1[0] * e2[1] - e1[1] * e2[0],
		};
		const bool forward = (n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]) < 0.0f;

		if (forward)
		{
			push_vertex(g_tris, p0, col);
			push_vertex(g_tris, p1, col);
			push_vertex(g_tris, p2, col);
			push_vertex(g_tris, p0, col);
			push_vertex(g_tris, p2, col);
			push_vertex(g_tris, p3, col);
		}
		else
		{
			push_vertex(g_tris, p0, col);
			push_vertex(g_tris, p3, col);
			push_vertex(g_tris, p2, col);
			push_vertex(g_tris, p0, col);
			push_vertex(g_tris, p2, col);
			push_vertex(g_tris, p1, col);
		}
	}

	void push_box(const double mn[3], const double mx[3], const float color[4],
	              bool filled, double cam_x, double cam_y, double cam_z)
	{
		float c[8][3];
		sdk::render::box_corners(mn, mx, c);

		// Camera-relative: float loses far too much precision at Minecraft's
		// world coordinates, and the shader only ever sees floats.
		for (auto& corner : c)
		{
			corner[0] -= static_cast<float>(cam_x);
			corner[1] -= static_cast<float>(cam_y);
			corner[2] -= static_cast<float>(cam_z);
		}

		for (const auto& e : sdk::render::box_edges)
		{
			if (g_edges_as_quads)
				push_edge_quad(c[e[0]], c[e[1]], color);
			else
			{
				push_vertex(g_lines, c[e[0]], color);
				push_vertex(g_lines, c[e[1]], color);
			}
		}

		if (!filled)
			return;

		const float fill[4] = { color[0], color[1], color[2], color[3] * 0.22f };
		static const int faces[6][4] = {
			{ 0, 1, 2, 3 }, { 4, 5, 6, 7 },
			{ 0, 1, 5, 4 }, { 1, 2, 6, 5 },
			{ 2, 3, 7, 6 }, { 3, 0, 4, 7 },
		};

		for (const auto& q : faces)
		{
			push_vertex(g_tris, c[q[0]], fill);
			push_vertex(g_tris, c[q[1]], fill);
			push_vertex(g_tris, c[q[2]], fill);
			push_vertex(g_tris, c[q[0]], fill);
			push_vertex(g_tris, c[q[2]], fill);
			push_vertex(g_tris, c[q[3]], fill);
		}
	}

	// --- Name tags ----------------------------------------------------------
	//
	// The glyphs come from ImGui's atlas, which the client has already
	// rasterised for the menu — so the in-world tags use the same font as the
	// 2D ones instead of shipping a second copy of one. The atlas's white
	// pixel doubles as the panel texture, which is what lets the backdrop and
	// the text go out as a single draw.
	//
	// Everything is billboarded by hand from the camera basis: quads are built
	// in pixel space exactly as the 2D path lays them out, then mapped onto the
	// camera's right/up vectors. Scaling by metres-per-pixel at the tag's own
	// distance keeps the on-screen size identical to the overlay version, so
	// the only visible difference is that terrain now occludes them.

	// Gap between the top of the bounding box and the bottom of the panel,
	// in metres. Matches what the overlay leaves.
	constexpr double k_tag_height_offset = 0.35;

	std::string read_scoreboard_name(JNIEnv* env, jobject entity)
	{
		if (!g_mid_scoreboard_name)
			return {};

		jobject name_obj = env->CallObjectMethod(entity, g_mid_scoreboard_name);
		clear_exception(env);
		if (!name_obj)
			return {};

		std::string out;
		if (const char* utf = env->GetStringUTFChars(static_cast<jstring>(name_obj), nullptr))
		{
			out = utf;
			env->ReleaseStringUTFChars(static_cast<jstring>(name_obj), utf);
		}
		clear_exception(env);

		env->DeleteLocalRef(name_obj);
		return out;
	}

	// Leaves both at zero when the entity is not a LivingEntity or the mapping
	// is missing; build_tag then simply omits the health field.
	void read_health(JNIEnv* env, jobject entity, float& health, float& max_health)
	{
		health = 0.0f;
		max_health = 0.0f;

		if (!g_living_class || !g_mid_get_health || !g_mid_get_max_health)
			return;
		if (!env->IsInstanceOf(entity, g_living_class))
			return;

		health = env->CallFloatMethod(entity, g_mid_get_health);
		clear_exception(env);
		max_health = env->CallFloatMethod(entity, g_mid_get_max_health);
		clear_exception(env);
	}

	ImFont* tag_font()
	{
		if (!ImGui::GetCurrentContext())
			return nullptr;

		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		if (!atlas || atlas->Fonts.empty())
			return nullptr;

		return atlas->Fonts[0];
	}

	void push_text_vertex(float x, float y, float z, float u, float v, const float c[4])
	{
		g_text.push_back(x); g_text.push_back(y); g_text.push_back(z);
		g_text.push_back(u); g_text.push_back(v);
		g_text.push_back(c[0]); g_text.push_back(c[1]); g_text.push_back(c[2]); g_text.push_back(c[3]);
	}

	// One screen-space rectangle, placed in the world on the camera plane.
	// px/py are pixels relative to the tag's anchor, y growing downward as in
	// every 2D layout; `up` is negated to match.
	void push_billboard_quad(const float anchor[3], const float right[3], const float up[3],
	                         float wpp,
	                         float px0, float py0, float px1, float py1,
	                         float u0, float v0, float u1, float v1,
	                         const float color[4])
	{
		auto corner = [&](float px, float py, float out[3])
		{
			for (int i = 0; i < 3; ++i)
				out[i] = anchor[i] + right[i] * (px * wpp) - up[i] * (py * wpp);
		};

		float a[3], b[3], c[3], d[3];
		corner(px0, py0, a);   // top-left
		corner(px1, py0, b);   // top-right
		corner(px1, py1, c);   // bottom-right
		corner(px0, py1, d);   // bottom-left

		push_text_vertex(a[0], a[1], a[2], u0, v0, color);
		push_text_vertex(b[0], b[1], b[2], u1, v0, color);
		push_text_vertex(c[0], c[1], c[2], u1, v1, color);

		push_text_vertex(a[0], a[1], a[2], u0, v0, color);
		push_text_vertex(c[0], c[1], c[2], u1, v1, color);
		push_text_vertex(d[0], d[1], d[2], u0, v1, color);
	}

	// Rounded backdrop, triangulated as a fan from the panel's centre.
	//
	// The overlay gets its corners from ImDrawList::AddRectFilled; there is no
	// equivalent here, so the outline is walked by hand. It stays a single
	// convex polygon, which is exactly what a fan needs.
	void push_rounded_panel(const float anchor[3], const float right[3], const float up[3],
	                        float wpp,
	                        float x0, float y0, float x1, float y1, float radius,
	                        float u, float v, const float color[4])
	{
		const float w = x1 - x0;
		const float h = y1 - y0;
		const float max_r = (w < h ? w : h) * 0.5f;
		if (radius > max_r)
			radius = max_r;

		if (radius <= 0.5f)
		{
			push_billboard_quad(anchor, right, up, wpp, x0, y0, x1, y1, u, v, u, v, color);
			return;
		}

		constexpr int k_corner_segments = 5;
		constexpr float k_half_pi = 1.57079632679489661923f;

		struct pt_t { float x, y; };
		pt_t outline[4 * (k_corner_segments + 1)];
		int n = 0;

		// Screen-space convention, y growing downward, walked clockwise.
		const pt_t centers[4] = {
			{ x1 - radius, y0 + radius },
			{ x1 - radius, y1 - radius },
			{ x0 + radius, y1 - radius },
			{ x0 + radius, y0 + radius },
		};
		const float start[4] = { -k_half_pi, 0.0f, k_half_pi, k_half_pi * 2.0f };

		for (int c = 0; c < 4; ++c)
		{
			for (int s = 0; s <= k_corner_segments; ++s)
			{
				const float a = start[c] + k_half_pi * (static_cast<float>(s) / k_corner_segments);
				outline[n].x = centers[c].x + std::cos(a) * radius;
				outline[n].y = centers[c].y + std::sin(a) * radius;
				++n;
			}
		}

		auto to_world = [&](float px, float py, float out[3])
		{
			for (int i = 0; i < 3; ++i)
				out[i] = anchor[i] + right[i] * (px * wpp) - up[i] * (py * wpp);
		};

		float centre[3];
		to_world((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, centre);

		for (int i = 0; i < n; ++i)
		{
			const pt_t& a = outline[i];
			const pt_t& b = outline[(i + 1) % n];

			float aw[3], bw[3];
			to_world(a.x, a.y, aw);
			to_world(b.x, b.y, bw);

			push_text_vertex(centre[0], centre[1], centre[2], u, v, color);
			push_text_vertex(aw[0], aw[1], aw[2], u, v, color);
			push_text_vertex(bw[0], bw[1], bw[2], u, v, color);
		}
	}

	// Returns the advance actually consumed, so callers can lay out runs.
	float push_string(ImFont* font, float px, const char* text,
	                  const float anchor[3], const float right[3], const float up[3],
	                  float wpp, float start_x, float top_y, const float color[4])
	{
		const float scale = px / font->FontSize;
		float cursor = start_x;

		for (const char* s = text; *s; ++s)
		{
			// ASCII only: names arrive stripped of format codes, and a
			// multi-byte decoder here would be the only thing in this file
			// that needs one.
			const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(
				static_cast<unsigned char>(*s)));
			if (!glyph)
				continue;

			if (glyph->Visible)
			{
				push_billboard_quad(anchor, right, up, wpp,
					cursor + glyph->X0 * scale, top_y + glyph->Y0 * scale,
					cursor + glyph->X1 * scale, top_y + glyph->Y1 * scale,
					glyph->U0, glyph->V0, glyph->U1, glyph->V1,
					color);
			}

			cursor += glyph->AdvanceX * scale;
		}

		return cursor - start_x;
	}

	struct pending_tag_t
	{
		double x, y, z;          // anchor, world space
		float  distance;
		std::string name;
		float  health, max_health;
		bool   friendly;
	};

	std::vector<pending_tag_t> g_tags;

	// Minimal, ASCII-only cousin of esp.cpp's strip_format_codes. The full one
	// carries a UTF-8 decoder for names that arrive with multi-byte rank
	// glyphs; nothing here can draw those anyway — the glyph lookup is ASCII —
	// so unrepresentable bytes are dropped rather than decoded.
	std::string strip_codes_ascii(const std::string& in)
	{
		std::string out;
		out.reserve(in.size());

		for (size_t i = 0; i < in.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(in[i]);

			// The section sign is U+00A7, i.e. 0xC2 0xA7 in UTF-8.
			if (c == 0xC2 && i + 2 < in.size() &&
			    static_cast<unsigned char>(in[i + 1]) == 0xA7)
			{
				i += 2;
				continue;
			}

			if (c == '&' && i + 1 < in.size() && std::isalnum(static_cast<unsigned char>(in[i + 1])))
			{
				++i;
				continue;
			}

			if (c >= 0x20 && c < 0x7f)
				out += static_cast<char>(c);
		}

		return out;
	}

	void format_health_text(char* out, size_t n, int mode, float health, float max_health)
	{
		switch (mode)
		{
			case 1:
			{
				const int hearts = static_cast<int>(std::lround(health / 2.0f));
				const int max_hearts = static_cast<int>(std::lround((max_health > 1.0f ? max_health : 1.0f) / 2.0f));
				snprintf(out, n, "%d/%d", hearts, max_hearts);
				break;
			}
			case 2:
			{
				const float pct = max_health > 0.0f ? (health / max_health * 100.0f) : 0.0f;
				snprintf(out, n, "%.0f%%", pct);
				break;
			}
			default:
				snprintf(out, n, "%.1f", health);
				break;
		}
	}

	// Same curve as esp.cpp's tag_scale, so switching a tag between the overlay
	// and the world does not change its size.
	float tag_scale(float distance)
	{
		float size = globals::nametags_size / 0.35f;
		size = size < 0.2f ? 0.2f : (size > 4.0f ? 4.0f : size);

		if (globals::nametags_static_size)
			return size;

		const float range = globals::nametags_range > 1.0f ? globals::nametags_range : 1.0f;
		float t = distance / range;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

		const float scaled = size * (1.15f - t * 0.45f);
		return scaled < 0.45f ? 0.45f : (scaled > 3.0f ? 3.0f : scaled);
	}

	void build_tag(const pending_tag_t& tag, ImFont* font, const sdk::render::view_t& v,
	               double cam_x, double cam_y, double cam_z)
	{
		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		if (!atlas)
			return;

		const float raw_px = font->FontSize * tag_scale(tag.distance);
		const float px = raw_px < 6.0f ? 6.0f : raw_px;

		// Metres per screen pixel at this distance. Sizing by it is what makes
		// a world-space quad come out the same size as the overlay's text:
		// an object h metres tall covers h * fov_y * half_h / distance pixels.
		const float denom = v.fov_y * v.half_h;
		if (denom <= 0.0f)
			return;
		const float wpp = tag.distance / denom;

		float alpha = globals::nametags_alpha;
		alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);

		char health_buf[32] = { 0 };
		char dist_buf[24] = { 0 };
		if (globals::nametags_show_health && tag.max_health > 0.0f)
			format_health_text(health_buf, sizeof(health_buf), globals::nametags_health_mode,
			                   tag.health, tag.max_health);
		if (globals::nametags_show_distance)
			snprintf(dist_buf, sizeof(dist_buf), "%.0fm", tag.distance);

		const char* name_str = tag.name.empty() ? "Player" : tag.name.c_str();

		const float gap = px * 0.35f;
		const ImVec2 name_size = font->CalcTextSizeA(px, FLT_MAX, 0.0f, name_str);
		const ImVec2 health_size = health_buf[0] ? font->CalcTextSizeA(px, FLT_MAX, 0.0f, health_buf) : ImVec2(0, 0);
		const ImVec2 dist_size = dist_buf[0] ? font->CalcTextSizeA(px, FLT_MAX, 0.0f, dist_buf) : ImVec2(0, 0);

		float total = name_size.x;
		if (health_size.x > 0.0f) total += gap + health_size.x;
		if (dist_size.x > 0.0f) total += gap + dist_size.x;

		const float pad_x = px * 0.40f < 3.0f ? 3.0f : px * 0.40f;
		const float pad_y = px * 0.22f < 2.0f ? 2.0f : px * 0.22f;
		const float panel_h = px + pad_y * 2.0f;
		const float panel_w = total + pad_x * 2.0f;

		// Anchor is the bottom-centre of the panel, so the tag grows upward and
		// its gap to the player's head does not change with scale.
		const float panel_left = -panel_w * 0.5f;
		const float panel_top = -panel_h;

		const float anchor[3] = {
			static_cast<float>(tag.x - cam_x),
			static_cast<float>(tag.y - cam_y),
			static_cast<float>(tag.z - cam_z),
		};

		const ImVec2 white = atlas->TexUvWhitePixel;
		const float panel_color[4] = { 12.0f / 255.0f, 13.0f / 255.0f, 18.0f / 255.0f, alpha * 0.80f };

		const float radius = px * 0.25f < 2.0f ? 2.0f : px * 0.25f;
		push_rounded_panel(anchor, v.right, v.up, wpp,
			panel_left, panel_top, panel_left + panel_w, 0.0f, radius,
			white.x, white.y,
			panel_color);

		float cursor = panel_left + pad_x;
		const float top = panel_top + pad_y;

		float name_color[4] = { 1.0f, 1.0f, 1.0f, alpha };
		if (tag.friendly)
		{
			const ImVec4& fc = globals::nametags_friend_color;
			const float fa = fc.w * alpha;
			name_color[0] = fc.x;
			name_color[1] = fc.y;
			name_color[2] = fc.z;
			name_color[3] = fa < 0.0f ? 0.0f : (fa > 1.0f ? 1.0f : fa);
		}

		cursor += push_string(font, px, name_str, anchor, v.right, v.up, wpp, cursor, top, name_color);

		if (health_buf[0])
		{
			cursor += gap;
			const float ratio = tag.max_health > 0.0f ? (tag.health / tag.max_health) : 1.0f;
			const float health_color[4] = {
				ratio > 0.5f ? (1.0f - ratio) * 2.0f : 1.0f,
				ratio > 0.5f ? 1.0f : ratio * 2.0f,
				0.25f,
				alpha,
			};
			cursor += push_string(font, px, health_buf, anchor, v.right, v.up, wpp, cursor, top, health_color);
		}

		if (dist_buf[0])
		{
			cursor += gap;
			const float dist_color[4] = { 0.70f, 0.72f, 0.78f, alpha };
			push_string(font, px, dist_buf, anchor, v.right, v.up, wpp, cursor, top, dist_color);
		}
	}

	// 0..1 fraction between the last tick and this frame. Falls back to 1.0
	// (the current tick position) when the mapping is missing, which is exactly
	// the old, uninterpolated behaviour.
	float tick_progress(JNIEnv* env, jobject mc)
	{
		if (!g_fid_mc_tick_counter || !g_mid_tick_progress || !mc)
			return 1.0f;

		jobject counter = env->GetObjectField(mc, g_fid_mc_tick_counter);
		clear_exception(env);
		if (!counter)
			return 1.0f;

		const jfloat p = env->CallFloatMethod(counter, g_mid_tick_progress, JNI_TRUE);
		clear_exception(env);
		env->DeleteLocalRef(counter);

		if (p < 0.0f || p > 1.0f)
			return 1.0f;

		return p;
	}

	// RenderTickCounter.getTickProgress, by name first and by shape second.
	//
	// The name is the primary route because verify_mappings.py checks it
	// against Yarn at build time. The scan is what the header used to promise
	// and never had: if a version bump renumbers the method, one (Z)F method on
	// this class is unambiguous enough to keep interpolation alive until the
	// header catches up. Returning null only costs smoothness, so every failure
	// here is quiet.
	jmethodID resolve_tick_progress(JNIEnv* env, jclass counter_cls)
	{
		if (sdk::mappings::render_tick_counter_progress_name[0])
		{
			jmethodID mid = env->GetMethodID(counter_cls,
				sdk::mappings::render_tick_counter_progress_name,
				sdk::mappings::render_tick_counter_progress_sig);
			clear_exception(env);
			if (mid)
				return mid;

			logger::log("[world_render] " +
			            std::string(sdk::mappings::render_tick_counter_progress_name) +
			            " did not resolve — falling back to a signature scan.");
		}

		jvmtiEnv* jvmti = sdk::java::jvmti();
		if (!jvmti)
			return nullptr;

		jint count = 0;
		jmethodID* methods = nullptr;
		if (jvmti->GetClassMethods(counter_cls, &count, &methods) != JVMTI_ERROR_NONE)
			return nullptr;

		jmethodID found = nullptr;
		int matches = 0;

		for (jint i = 0; i < count; ++i)
		{
			char* m_sig = nullptr;
			if (jvmti->GetMethodName(methods[i], nullptr, &m_sig, nullptr) != JVMTI_ERROR_NONE)
				continue;

			if (m_sig && std::strcmp(m_sig, sdk::mappings::render_tick_counter_progress_sig) == 0)
			{
				++matches;
				if (!found)
					found = methods[i];
			}

			if (m_sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(m_sig));
		}

		jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));

		// More than one candidate means the shape stopped identifying the
		// method. Guessing would be worse than no interpolation: a wrong 0..1
		// value moves every box by a fraction of its velocity every frame.
		if (matches != 1)
			return nullptr;

		return found;
	}

	// Hands ImGui's glyph atlas to the Java side once. Deferred to the first
	// frame that actually wants text: the atlas is only guaranteed built after
	// the overlay has run, and init() happens before that.
	bool ensure_font_uploaded(JNIEnv* env)
	{
		if (g_font_uploaded)
			return true;
		if (!g_renderer_upload_font || !ImGui::GetCurrentContext())
			return false;

		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		if (!atlas)
			return false;

		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		atlas->GetTexDataAsRGBA32(&pixels, &width, &height);
		if (!pixels || width <= 0 || height <= 0)
			return false;

		jobject buffer = env->NewDirectByteBuffer(pixels,
			static_cast<jlong>(static_cast<size_t>(width) * height * 4));
		clear_exception(env);
		if (!buffer)
			return false;

		const jint status = env->CallStaticIntMethod(g_renderer_class, g_renderer_upload_font,
			buffer, static_cast<jint>(width), static_cast<jint>(height));
		report_exception(env, "EnhanceRenderer.uploadFont");
		env->DeleteLocalRef(buffer);

		if (status == 0)
		{
			g_font_uploaded = true;
			logger::log("[world_render] glyph atlas uploaded (" + std::to_string(width) + "x" +
			            std::to_string(height) + ") — in-world name tags active.");
			return true;
		}

		// Reported once: retried every frame otherwise, and a failing upload
		// fails the same way each time.
		static bool reported = false;
		if (!reported)
		{
			reported = true;

			std::string why;
			switch (status)
			{
				case -1: why = "the texture could not be created"; break;
				case -2: why = "the upload reported success but the texture reads back empty — "
				               "something redirected the pixel source"; break;
				default: why = "GL error 0x" + std::to_string(status); break;
			}

			logger::log_error("[world_render] glyph atlas upload failed: " + why +
			                  ". Name tags stay off; boxes are unaffected.");
		}

		return false;
	}

	// The direct buffer wrapping g_text. It has to be rebuilt whenever the
	// vector reallocates, and both the OpenGL path and the submit path want it.
	jobject ensure_text_buffer(JNIEnv* env)
	{
		if (g_text_buffer && g_text_buffer_floats == g_text.capacity())
			return g_text_buffer;

		if (g_text_buffer)
		{
			env->DeleteGlobalRef(g_text_buffer);
			g_text_buffer = nullptr;
		}

		jobject local = env->NewDirectByteBuffer(g_text.data(),
			static_cast<jlong>(g_text.capacity() * sizeof(float)));
		clear_exception(env);
		if (!local)
			return nullptr;

		g_text_buffer = env->NewGlobalRef(local);
		env->DeleteLocalRef(local);
		g_text_buffer_floats = g_text.capacity();
		return g_text_buffer;
	}

	void submit_text(JNIEnv* env, const float mvp[16])
	{
		if (g_text.empty() || !g_renderer_render_text)
			return;
		if (!ensure_font_uploaded(env))
			return;
		if (!ensure_text_buffer(env))
			return;

		jfloatArray mvp_array = env->NewFloatArray(16);
		clear_exception(env);
		if (!mvp_array)
			return;
		env->SetFloatArrayRegion(mvp_array, 0, 16, mvp);
		clear_exception(env);

		env->CallStaticVoidMethod(g_renderer_class, g_renderer_render_text,
			g_text_buffer,
			static_cast<jint>(g_text.size() / k_floats_per_text_vertex),
			mvp_array,
			globals::esp_world_through_walls ? JNI_FALSE : JNI_TRUE);
		report_exception(env, "EnhanceRenderer.renderText");

		env->DeleteLocalRef(mvp_array);
	}

	void submit_boxes(JNIEnv* env, const float mvp[16]);
	void submit_tags_native(JNIEnv* env, double cam_x, double cam_y, double cam_z);
	jobject ensure_text_buffer(JNIEnv* env);
	std::string read_topology(JNIEnv* env, jobject render_type);
	bool describe_format(JNIEnv* env, jobject render_type, const char* what, int& out_mask);
	jobject make_proxy(JNIEnv* env, jclass iface, jint kind);

	// Collects the boxes and tags for this frame and hands them to the Java
	// renderer.
	void submit_frame(JNIEnv* env)
	{
		if (!globals::esp_world_render_enabled)
			return;
		if (!g_renderer_class || !g_renderer_render)
			return;
		if (!g_mc_class || !g_fid_mc_instance)
			return;

		double cam_x = 0.0, cam_y = 0.0, cam_z = 0.0;
		float cam_yaw = 0.0f, cam_pitch = 0.0f, cam_fov = 70.0f;
		if (!sdk::render::sample_camera(cam_x, cam_y, cam_z, cam_yaw, cam_pitch, cam_fov))
			return;

		// Aspect only affects the horizontal term; the framebuffer size is not
		// available here, so reuse whatever the overlay published this frame.
		const sdk::render::view_t& current = sdk::render::view();
		const int w = current.valid ? static_cast<int>(current.half_w * 2.0f) : 0;
		const int h = current.valid ? static_cast<int>(current.half_h * 2.0f) : 0;
		if (w <= 0 || h <= 0)
			return;

		sdk::render::set_view(cam_x, cam_y, cam_z, cam_yaw, cam_pitch, cam_fov, w, h);

		float mvp[16];
		if (!sdk::render::build_view_projection(mvp))
			return;

		// Edges follow the fill through walls only when there is a see-through
		// type to put them through. The atlas is registered lazily, so this is
		// false for the first frame or two and the outline is drawn as lines
		// until then -- visible only as the outline changing weight once.
		{
			const sdk::render::view_t& vw = sdk::render::view();
			const float denom = vw.fov_y * vw.half_h;
			g_edge_scale = denom > 0.0f ? 1.0f / denom : 0.0f;
			g_edges_as_quads = g_frame_collector && globals::esp_world_through_walls &&
			                   g_rt_text && g_edge_scale > 0.0f;
		}

		g_tris.clear();
		g_lines.clear();
		g_text.clear();
		g_tags.clear();

		ImFont* font = nullptr;
		const bool want_tags = globals::nametags_enabled && globals::nametags_in_world;
		if (want_tags)
		{
			font = tag_font();
			if (font && !font->IsLoaded())
				font = nullptr;
		}

		jobject mc = env->GetStaticObjectField(g_mc_class, g_fid_mc_instance);
		clear_exception(env);
		if (!mc)
			return;

		const float delta = tick_progress(env, mc);

		jobject world = env->GetObjectField(mc, g_fid_mc_world);
		clear_exception(env);
		jobject local_player = env->GetObjectField(mc, g_fid_mc_player);
		clear_exception(env);

		if (world && g_fid_world_players)
		{
			jobject players = env->GetObjectField(world, g_fid_world_players);
			clear_exception(env);

			if (players)
			{
				const jint count = env->CallIntMethod(players, g_mid_list_size);
				clear_exception(env);

				const float color[4] = {
					globals::esp_color.x, globals::esp_color.y,
					globals::esp_color.z, globals::esp_color.w
				};

				for (jint i = 0; i < count; ++i)
				{
					jobject entity = env->CallObjectMethod(players, g_mid_list_get, i);
					clear_exception(env);
					if (!entity)
						continue;

					if (local_player && env->IsSameObject(entity, local_player))
					{
						env->DeleteLocalRef(entity);
						continue;
					}

					jobject box = env->GetObjectField(entity, g_fid_entity_box);
					clear_exception(env);
					if (!box)
					{
						env->DeleteLocalRef(entity);
						continue;
					}

					double mn[3] = {
						env->GetDoubleField(box, g_fid_box_min_x),
						env->GetDoubleField(box, g_fid_box_min_y),
						env->GetDoubleField(box, g_fid_box_min_z),
					};
					double mx[3] = {
						env->GetDoubleField(box, g_fid_box_max_x),
						env->GetDoubleField(box, g_fid_box_max_y),
						env->GetDoubleField(box, g_fid_box_max_z),
					};

					// The bounding box sits at the tick position. Shift it by the
					// same amount the game shifts the model, so the box tracks the
					// entity between ticks instead of stepping at 20 Hz.
					if (g_fid_last_render_x && g_fid_last_render_y && g_fid_last_render_z)
					{
						const double now_x = env->CallDoubleMethod(entity, g_mid_entity_get_x);
						const double now_y = env->CallDoubleMethod(entity, g_mid_entity_get_y);
						const double now_z = env->CallDoubleMethod(entity, g_mid_entity_get_z);
						clear_exception(env);

						const double prev_x = env->GetDoubleField(entity, g_fid_last_render_x);
						const double prev_y = env->GetDoubleField(entity, g_fid_last_render_y);
						const double prev_z = env->GetDoubleField(entity, g_fid_last_render_z);

						const double off_x = (prev_x + (now_x - prev_x) * delta) - now_x;
						const double off_y = (prev_y + (now_y - prev_y) * delta) - now_y;
						const double off_z = (prev_z + (now_z - prev_z) * delta) - now_z;

						mn[0] += off_x; mx[0] += off_x;
						mn[1] += off_y; mx[1] += off_y;
						mn[2] += off_z; mx[2] += off_z;
					}

					push_box(mn, mx, color, globals::esp_world_filled, cam_x, cam_y, cam_z);

					// Tags are queued rather than built here: they blend against
					// each other, so they have to go out far-to-near, which is
					// not the order the player list arrives in.
					if (font)
					{
						const double ax = (mn[0] + mx[0]) * 0.5;
						const double ay = mx[1] + k_tag_height_offset;
						const double az = (mn[2] + mx[2]) * 0.5;

						const double ddx = ax - cam_x;
						const double ddy = ay - cam_y;
						const double ddz = az - cam_z;
						const float distance = static_cast<float>(std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz));

						if (distance <= globals::nametags_range)
						{
							pending_tag_t tag{};
							tag.x = ax;
							tag.y = ay;
							tag.z = az;
							tag.distance = distance;
							tag.name = strip_codes_ascii(read_scoreboard_name(env, entity));
							tag.friendly = !tag.name.empty() &&
								enhance::modules::killaura::friends_list::contains(tag.name);

							read_health(env, entity, tag.health, tag.max_health);

							if (!tag.friendly || globals::nametags_show_friends)
								g_tags.push_back(std::move(tag));
						}
					}

					env->DeleteLocalRef(box);
					env->DeleteLocalRef(entity);
				}

				env->DeleteLocalRef(players);
			}
		}

		if (world) env->DeleteLocalRef(world);
		if (local_player) env->DeleteLocalRef(local_player);
		env->DeleteLocalRef(mc);

		// Far to near, so a nearer tag blends over a more distant one rather
		// than being cut out by it.
		//
		// Guarded on its own: this runs before the boxes are submitted, so
		// letting it throw up to native_frame would drop the whole frame and
		// take the boxes with it — which is not obviously a tag problem when
		// you are looking at an empty screen.
		if (font && !g_tags.empty())
		{
			try
			{
				std::sort(g_tags.begin(), g_tags.end(),
					[](const pending_tag_t& a, const pending_tag_t& b)
					{
						return a.distance > b.distance;
					});

				const sdk::render::view_t& view = sdk::render::view();
				for (const auto& tag : g_tags)
					build_tag(tag, font, view, cam_x, cam_y, cam_z);
			}
			catch (...)
			{
				g_text.clear();

				static bool reported = false;
				if (!reported)
				{
					reported = true;
					logger::log_error("[world_render] building the name tags threw — tags are "
					                  "skipped this frame, boxes are unaffected.");
				}
			}
		}

		// Boxes first, tags after — both are translucent, and a tag drawn
		// before the box it belongs to gets painted over by it. This is also
		// why the text submit is not an early return: a frame with tags but no
		// boxes still has to reach it.
		if (!g_tris.empty() || !g_lines.empty())
			submit_boxes(env, mvp);

		// The game's own tags where the submit path is running, ours otherwise.
		if (g_frame_collector)
			submit_tags_native(env, cam_x, cam_y, cam_z);
		else
			submit_text(env, mvp);

		// One line, on the first frame that draws anything. The per-frame
		// version of this was invaluable while the tags were coming out black,
		// but the log is shared with modules that write every tick and does
		// not need a heartbeat from this one as well.
		{
			static bool announced = false;
			if (!announced && (!g_tris.empty() || !g_text.empty()))
			{
				announced = true;
				logger::log("[world_render] drawing: boxes=" +
				            std::to_string(g_tris.size() / k_floats_per_vertex) + "tri/" +
				            std::to_string(g_lines.size() / k_floats_per_vertex) + "line, tags=" +
				            std::to_string(g_tags.size()));
			}
		}
	}

	// Our atlas is antialiased and wants linear sampling. Minecraft's own fonts
	// are pixel art and are sampled nearest, which is what a DynamicTexture gets
	// by default -- and what made the glyphs come out crunchy through the text
	// render type. The sampler is a field, so it can simply be replaced.
	void set_linear_sampler(JNIEnv* env, jobject texture)
	{
		if (!texture || !sdk::mappings::have(sdk::mappings::filter_mode_linear_name))
			return;

		jclass rs_cls = sdk::classloader::find_class(env, sdk::mappings::render_system_class_sig);
		jclass fm_cls = sdk::classloader::find_class(env, sdk::mappings::filter_mode_class_sig);
		jclass at_cls = sdk::classloader::find_class(env, sdk::mappings::abstract_texture_class_sig);
		if (!rs_cls || !fm_cls || !at_cls)
			return;

		jmethodID mid_cache = env->GetStaticMethodID(rs_cls,
			sdk::mappings::render_system_sampler_cache_name,
			sdk::mappings::render_system_sampler_cache_sig);
		jfieldID fid_linear = env->GetStaticFieldID(fm_cls,
			sdk::mappings::filter_mode_linear_name, sdk::mappings::filter_mode_linear_sig);
		jfieldID fid_sampler = env->GetFieldID(at_cls,
			sdk::mappings::abstract_texture_sampler_name,
			sdk::mappings::abstract_texture_sampler_sig);
		clear_exception(env);
		if (!mid_cache || !fid_linear || !fid_sampler)
			return;

		jobject cache = env->CallStaticObjectMethod(rs_cls, mid_cache);
		jobject linear = env->GetStaticObjectField(fm_cls, fid_linear);
		clear_exception(env);
		if (!cache || !linear)
			return;

		jclass sc_cls = env->GetObjectClass(cache);
		jmethodID mid_clamp = sc_cls
			? env->GetMethodID(sc_cls, sdk::mappings::sampler_cache_clamp_name,
			                   sdk::mappings::sampler_cache_clamp_sig)
			: nullptr;
		clear_exception(env);
		if (sc_cls)
			env->DeleteLocalRef(sc_cls);

		jobject sampler = mid_clamp ? env->CallObjectMethod(cache, mid_clamp, linear) : nullptr;
		clear_exception(env);
		if (sampler)
		{
			env->SetObjectField(texture, fid_sampler, sampler);
			clear_exception(env);
			logger::log("[world_render] glyph atlas sampled linearly");
			env->DeleteLocalRef(sampler);
		}

		env->DeleteLocalRef(linear);
		env->DeleteLocalRef(cache);
	}

	// Hands ImGui's glyph atlas to the game as a texture, once, and builds the
	// render type that names it.
	//
	// Deferred like the OpenGL upload: the atlas is only guaranteed built after
	// the overlay has drawn a frame, which is long after init().
	bool register_atlas_texture(JNIEnv* env)
	{
		if (g_rt_text)
			return true;
		if (g_atlas_tried)
			return false;

		if (!sdk::mappings::have(sdk::mappings::render_type_text_see_through_name) ||
		    !sdk::mappings::have(sdk::mappings::dynamic_texture_init_name))
			return false;

		if (!ImGui::GetCurrentContext())
			return false;

		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		if (atlas)
			atlas->GetTexDataAsRGBA32(&pixels, &width, &height);
		if (!pixels || width <= 0 || height <= 0)
			return false;

		g_atlas_tried = true;

		jclass dt_cls = sdk::classloader::find_class(env,
			sdk::mappings::dynamic_texture_class_sig);
		jclass tm_cls = sdk::classloader::find_class(env,
			sdk::mappings::texture_manager_class_sig);
		jclass id_cls = sdk::classloader::find_class(env, sdk::mappings::identifier_class_sig);
		jclass types_cls = sdk::classloader::find_class(env,
			sdk::mappings::render_types_class_sig);
		if (!dt_cls || !tm_cls || !id_cls || !types_cls)
			return false;

		jmethodID dt_init = env->GetMethodID(dt_cls,
			sdk::mappings::dynamic_texture_init_name, sdk::mappings::dynamic_texture_init_sig);
		jmethodID dt_pixels = env->GetMethodID(dt_cls,
			sdk::mappings::dynamic_texture_pixels_name,
			sdk::mappings::dynamic_texture_pixels_sig);
		jmethodID dt_upload = env->GetMethodID(dt_cls,
			sdk::mappings::dynamic_texture_upload_name,
			sdk::mappings::dynamic_texture_upload_sig);
		jmethodID id_of = env->GetStaticMethodID(id_cls,
			sdk::mappings::identifier_of_name, sdk::mappings::identifier_of_sig);
		jmethodID tm_register = env->GetMethodID(tm_cls,
			sdk::mappings::texture_manager_register_name,
			sdk::mappings::texture_manager_register_sig);
		jmethodID mc_tm = env->GetMethodID(g_mc_class,
			sdk::mappings::minecraft_texture_manager_name,
			sdk::mappings::minecraft_texture_manager_sig);
		clear_exception(env);
		if (!dt_init || !dt_pixels || !dt_upload || !id_of || !tm_register || !mc_tm)
		{
			logger::log_error("[world_render] the texture path did not resolve -- name tags "
			                  "fall back to the game's own renderer");
			return false;
		}

		jstring label = env->NewStringUTF("enhance-glyphs");
		jobject tex = env->NewObject(dt_cls, dt_init, label, (jint)width, (jint)height,
		                             JNI_FALSE);
		report_exception(env, "new DynamicTexture");
		env->DeleteLocalRef(label);
		if (!tex)
			return false;

		// One memcpy through the image's own pointer. setPixel would be a
		// million calls for a 1024x1024 atlas.
		jobject img = env->CallObjectMethod(tex, dt_pixels);
		clear_exception(env);
		if (img)
		{
			jclass ni_cls = env->GetObjectClass(img);
			jmethodID ni_ptr = ni_cls
				? env->GetMethodID(ni_cls, sdk::mappings::native_image_pointer_name,
				                   sdk::mappings::native_image_pointer_sig)
				: nullptr;
			clear_exception(env);
			if (ni_cls)
				env->DeleteLocalRef(ni_cls);

			const jlong addr = ni_ptr ? env->CallLongMethod(img, ni_ptr) : 0;
			clear_exception(env);
			if (addr)
				memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(addr)), pixels,
				       static_cast<size_t>(width) * height * 4);
			env->DeleteLocalRef(img);

			if (!addr)
			{
				env->DeleteLocalRef(tex);
				return false;
			}
		}

		env->CallVoidMethod(tex, dt_upload);
		report_exception(env, "DynamicTexture.upload");

		set_linear_sampler(env, tex);

		jstring ns = env->NewStringUTF("enhance");
		jstring path = env->NewStringUTF("glyphs");
		jobject id = env->CallStaticObjectMethod(id_cls, id_of, ns, path);
		report_exception(env, "Identifier.fromNamespaceAndPath");
		env->DeleteLocalRef(ns);
		env->DeleteLocalRef(path);
		if (!id)
		{
			env->DeleteLocalRef(tex);
			return false;
		}

		jobject mc = env->GetStaticObjectField(g_mc_class, g_fid_mc_instance);
		jobject tm = mc ? env->CallObjectMethod(mc, mc_tm) : nullptr;
		clear_exception(env);
		if (tm)
		{
			env->CallVoidMethod(tm, tm_register, id, tex);
			report_exception(env, "TextureManager.register");
			env->DeleteLocalRef(tm);
		}
		if (mc)
			env->DeleteLocalRef(mc);
		env->DeleteLocalRef(tex);

		jmethodID mid_text = env->GetStaticMethodID(types_cls,
			sdk::mappings::render_type_text_see_through_name,
			sdk::mappings::render_type_text_see_through_sig);
		clear_exception(env);
		jobject rt = mid_text ? env->CallStaticObjectMethod(types_cls, mid_text, id) : nullptr;
		report_exception(env, "RenderTypes.textSeeThrough");
		env->DeleteLocalRef(id);
		if (!rt)
			return false;

		g_rt_text = env->NewGlobalRef(rt);
		env->DeleteLocalRef(rt);

		const std::string topo = read_topology(env, g_rt_text);
		g_text_as_quads = (topo == "QUADS");
		if (!topo.empty() && topo != "QUADS" && topo != "TRIANGLES")
		{
			logger::log_error("[world_render] the text render type draws " + topo +
			                  ", which our glyph quads are not -- falling back");
			env->DeleteGlobalRef(g_rt_text);
			g_rt_text = nullptr;
			return false;
		}

		if (!describe_format(env, g_rt_text, "name tags", g_mask_text))
		{
			env->DeleteGlobalRef(g_rt_text);
			g_rt_text = nullptr;
			return false;
		}

		if (g_cgr_cls)
			g_proxy_text = make_proxy(env, g_cgr_cls, 3);   // KIND_SUBMIT_TEXT
		if (!g_proxy_text)
		{
			env->DeleteGlobalRef(g_rt_text);
			g_rt_text = nullptr;
			return false;
		}

		// The atlas's white pixel, so a quad with no texture of its own still
		// comes out solid through a textured render type.
		if (jmethodID set_white = env->GetStaticMethodID(g_renderer_class, "setWhiteUv", "(FF)V"))
		{
			const ImVec2 white = atlas->TexUvWhitePixel;
			env->CallStaticVoidMethod(g_renderer_class, set_white, white.x, white.y);
		}
		clear_exception(env);

		logger::log("[world_render] the glyph atlas is a game texture now -- name tags are "
		            "the client's own again, panel and all (topology " +
		            (topo.empty() ? "unknown" : topo) + ")");
		return true;
	}

	// One name tag per target, drawn by the game.
	//
	// The pose is moved to the anchor and the offset argument left at zero,
	// rather than building a Vec3 per tag per frame. It is pushed and popped
	// around each one: everything submitted afterwards shares this transform.
	void submit_tags_native(JNIEnv* env, double cam_x, double cam_y, double cam_z)
	{
		if (!g_tags_ready || !g_frame_collector || !g_frame_pose || !g_frame_state)
			return;
		if (g_tags.empty())
			return;

		jobject camera = env->GetObjectField(g_frame_state, g_fid_lrs_camera);
		clear_exception(env);
		if (!camera)
			return;

		jobject ordered = env->CallObjectMethod(g_frame_collector, g_mid_order, 0);
		clear_exception(env);
		if (!ordered)
		{
			env->DeleteLocalRef(camera);
			return;
		}

		// Ours, if the atlas made it across. Everything the overlay draws -- the
		// panel, the colours, the health readout, the distance -- is already in
		// g_text; it only ever needed somewhere to go.
		if (register_atlas_texture(env) && !g_text.empty() && g_mid_stage_geometry)
		{
			jobject buf = ensure_text_buffer(env);
			if (buf)
			{
				jmethodID stage = env->GetStaticMethodID(g_renderer_class, "stageText",
					"(Ljava/nio/ByteBuffer;IIZ)V");
				clear_exception(env);
				if (stage)
				{
					env->CallStaticVoidMethod(g_renderer_class, stage, buf,
						static_cast<jint>(g_text.size() / k_floats_per_text_vertex),
						(jint)g_mask_text, g_text_as_quads ? JNI_TRUE : JNI_FALSE);
					clear_exception(env);

					env->CallVoidMethod(ordered, g_mid_submit_custom,
					                    g_frame_pose, g_rt_text, g_proxy_text);
					report_exception(env, "submitCustomGeometry(text)");

					// Numbers rather than another guess about why nothing shows.
					static ULONGLONG s_next = 0;
					const ULONGLONG now = GetTickCount64();
					if (now >= s_next)
					{
						s_next = now + 3000;
						char line[192];
						sprintf_s(line, sizeof(line),
						          "[world_render] text submitted: %zu verts mask=%d quads=%d",
						          g_text.size() / k_floats_per_text_vertex, g_mask_text,
						          (int)g_text_as_quads);
						logger::log(line);
					}

					env->DeleteLocalRef(ordered);
					env->DeleteLocalRef(camera);
					return;
				}
			}
		}

		for (const auto& tag : g_tags)
		{
			// Health rides in the text. The OpenGL path drew it as a bar, which
			// has no equivalent here -- the game's tag renderer draws a string.
			std::string text = tag.name;
			if (tag.max_health > 0.0f)
				text += "  " + std::to_string(static_cast<int>(tag.health + 0.5f));

			jstring js = env->NewStringUTF(text.c_str());
			if (!js)
				continue;

			jobject comp = env->CallStaticObjectMethod(g_component_cls, g_mid_component_of, js);
			clear_exception(env);
			env->DeleteLocalRef(js);
			if (!comp)
				continue;

			env->CallVoidMethod(g_frame_pose, g_mid_pose_push);
			env->CallVoidMethod(g_frame_pose, g_mid_pose_translate,
			                    tag.x - cam_x, tag.y - cam_y, tag.z - cam_z);

			// Full-bright, and see-through on: a tag that is only legible when
			// the target is already visible is not worth drawing.
			env->CallVoidMethod(ordered, g_mid_submit_name_tag,
			                    g_frame_pose, g_vec3_zero, (jint)0, comp,
			                    JNI_TRUE, (jint)0x00F000F0, camera);
			clear_exception(env);

			env->CallVoidMethod(g_frame_pose, g_mid_pose_pop);
			clear_exception(env);

			env->DeleteLocalRef(comp);
		}

		env->DeleteLocalRef(ordered);
		env->DeleteLocalRef(camera);
	}

	// Splits out of submit_frame so the tag path can run after it without
	// duplicating the buffer bookkeeping.
	void submit_boxes(JNIEnv* env, const float mvp[16])
	{

		// Triangles first, then lines: the Java side draws them as two ranges of
		// one buffer.
		g_packed.clear();
		g_packed.insert(g_packed.end(), g_tris.begin(), g_tris.end());
		g_packed.insert(g_packed.end(), g_lines.begin(), g_lines.end());

		// The direct buffer wraps our storage, so it has to be rebuilt whenever
		// the vector reallocates.
		if (!g_packed_buffer || g_packed_buffer_floats != g_packed.capacity())
		{
			if (g_packed_buffer)
			{
				env->DeleteGlobalRef(g_packed_buffer);
				g_packed_buffer = nullptr;
			}

			jobject local = env->NewDirectByteBuffer(g_packed.data(),
				static_cast<jlong>(g_packed.capacity() * sizeof(float)));
			clear_exception(env);
			if (!local)
				return;

			g_packed_buffer = env->NewGlobalRef(local);
			env->DeleteLocalRef(local);
			g_packed_buffer_floats = g_packed.capacity();

			if (!g_packed_buffer)
				return;
		}

		// The game's own renderer, when this version has one to hand the vertices
		// to. Two submissions because the triangle and line halves want different
		// render types, and each carries its own proxy: a submitted node is drawn
		// later, during the drain, so a shared flag set here would be stale by the
		// time the callback reads it.
		if (g_submit_ready && g_frame_collector && g_frame_pose)
		{
			env->CallStaticVoidMethod(g_renderer_class, g_mid_stage_geometry,
				g_packed_buffer,
				static_cast<jint>(g_tris.size() / k_floats_per_vertex),
				static_cast<jint>(g_lines.size() / k_floats_per_vertex));
			report_exception(env, "EnhanceRenderer.stageGeometry");

			jobject ordered = env->CallObjectMethod(g_frame_collector, g_mid_order, 0);
			report_exception(env, "SubmitNodeCollector.order");
			if (!ordered)
				return;

			// Through walls means through the depth test, and nothing in the game's
			// plain geometry types skips it -- but the text type does, and it is
			// already bound to our atlas. Pointing the boxes at the atlas's white
			// pixel makes it a solid-colour type, which is all they need.
			jobject rt_fill = g_rt_tris;
			int     fill_mask = g_mask_tris;
			bool    fill_quads = g_tris_as_quads;
			if (globals::esp_world_through_walls && g_rt_text)
			{
				rt_fill = g_rt_text;
				fill_mask = g_mask_text;
				fill_quads = g_text_as_quads;
			}

			if (!g_tris.empty() && rt_fill)
			{
				env->CallStaticVoidMethod(g_renderer_class, g_mid_set_masks,
				                          (jint)fill_mask, (jint)g_mask_lines);
				env->CallStaticVoidMethod(g_renderer_class, g_mid_set_quads,
				                          fill_quads ? JNI_TRUE : JNI_FALSE);
				clear_exception(env);

				env->CallVoidMethod(ordered, g_mid_submit_custom,
				                    g_frame_pose, rt_fill, g_proxy_tris);
				report_exception(env, "submitCustomGeometry(tris)");
			}
			if (!g_lines.empty() && g_rt_lines)
			{
				env->CallVoidMethod(ordered, g_mid_submit_custom,
				                    g_frame_pose, g_rt_lines, g_proxy_lines);
				report_exception(env, "submitCustomGeometry(lines)");
			}

			env->DeleteLocalRef(ordered);
			return;
		}

		jfloatArray mvp_array = env->NewFloatArray(16);
		clear_exception(env);
		if (!mvp_array)
			return;
		env->SetFloatArrayRegion(mvp_array, 0, 16, mvp);
		clear_exception(env);

		env->CallStaticVoidMethod(g_renderer_class, g_renderer_render,
			g_packed_buffer,
			static_cast<jint>(g_tris.size() / k_floats_per_vertex),
			static_cast<jint>(g_lines.size() / k_floats_per_vertex),
			mvp_array,
			globals::esp_world_through_walls ? JNI_FALSE : JNI_TRUE);
		report_exception(env, "EnhanceRenderer.render");

		env->DeleteLocalRef(mvp_array);
	}

	// EnhanceRenderer.frame(), reached from the proxy on Fabric's world render
	// event. Runs on the render thread, with that thread's own JNIEnv — which
	// is the one the renderer needs and the one the worker's cached env is not.
	void JNICALL native_frame(JNIEnv* env, jclass)
	{
		try
		{
			submit_frame(env);
		}
		catch (...)
		{
			clear_exception(env);

			static bool reported = false;
			if (!reported)
			{
				reported = true;
				logger::log_error("[world_render] submit_frame threw — nothing was drawn this "
				                  "frame. Everything after the throw is skipped, so an empty "
				                  "screen here is one fault, not several.");
			}
		}
	}

	// Calls a static void method on the Java renderer, ignoring absence. Used
	// for the two state setters, where failing is not worth aborting over.
	void call_renderer_setter(JNIEnv* env, const char* name, const char* sig, jvalue arg)
	{
		if (!g_renderer_class)
			return;

		jmethodID mid = env->GetStaticMethodID(g_renderer_class, name, sig);
		clear_exception(env);
		if (!mid)
			return;

		env->CallStaticVoidMethodA(g_renderer_class, mid, &arg);
		clear_exception(env);
	}

	// Builds a Proxy implementing the event's interface and hands it to
	// WorldRenderEvents.<field>.register().
	//
	// Fabric's Event has no unregister, so this subscription outlives the
	// module. EnhanceRenderer.setActive/setCurrent are what make that safe:
	// teardown flips them, and the listener returns without calling native
	// code. See the fields on the Java side.
	// Resolves a class AND forces it through initialisation.
	//
	// sdk::classloader::find_class goes through KnotClassLoader.loadClass,
	// which can leave the class merely loaded — not linked. JVMTI's
	// GetClassFields then fails with CLASS_NOT_PREPARED, and that looked from
	// the caller like a class with no fields at all: the subscription reported
	// "WorldRenderEvents exposes no Event fields" on exactly the runs where
	// nothing else had touched that class yet, and worked on the runs where
	// the game happened to have initialised it first. Same feature, different
	// outcome per launch, which is what made it look like a Fabric version
	// difference rather than a linking one.
	//
	// (The root cause is worth naming: classloader.cpp calls
	// loadClass(String, boolean) with only one argument, so `resolve` is
	// whatever was on the stack.)
	//
	// Class.forName(name, true, loader) makes it deterministic — it also runs
	// the static initialiser, which is what fills in the event fields read
	// below.
	jclass load_initialised(JNIEnv* env, const char* binary_name)
	{
		jclass cls = sdk::classloader::find_class(env, binary_name);
		clear_exception(env);
		if (!cls)
			return nullptr;

		jclass class_cls = env->FindClass("java/lang/Class");
		clear_exception(env);
		if (!class_cls)
			return cls;

		jmethodID for_name = env->GetStaticMethodID(class_cls, "forName",
			"(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
		jmethodID get_loader = env->GetMethodID(class_cls, "getClassLoader",
			"()Ljava/lang/ClassLoader;");
		clear_exception(env);
		if (!for_name || !get_loader)
			return cls;

		jobject loader = env->CallObjectMethod(cls, get_loader);
		clear_exception(env);

		std::string dotted(binary_name);
		std::replace(dotted.begin(), dotted.end(), '/', '.');

		jstring jname = env->NewStringUTF(dotted.c_str());
		clear_exception(env);
		if (!jname)
		{
			if (loader) env->DeleteLocalRef(loader);
			return cls;
		}

		jclass initialised = static_cast<jclass>(env->CallStaticObjectMethod(
			class_cls, for_name, jname, JNI_TRUE, loader));
		report_exception(env, "Class.forName");

		env->DeleteLocalRef(jname);
		if (loader) env->DeleteLocalRef(loader);

		if (initialised)
		{
			env->DeleteLocalRef(cls);
			return initialised;
		}

		return cls;
	}

	// --- the submit-node backend --------------------------------------------

	jmethodID ORIG_submit_entities = nullptr;
	jmethodID g_hooked_submit_entities = nullptr;
	jmethodID g_submit_mid = nullptr;
	bool      g_submit_attach_failed = false;
	bool      g_submit_pending = false;
	// Set when the filled half is drawn through a QUADS topology, where each
	// triangle goes out as four vertices with the last one repeated.
	jclass    g_level_renderer_cls = nullptr;    // GlobalRef

	// LevelRenderer.submitEntities. The frame is taking entity geometry and is
	// holding the collector open; it is handed to us as the third argument,
	// which is the whole reason this method was chosen over the alternatives.
	void hkSubmitEntities(JNIEnv* env, jobject thiz, jobject pose_stack,
	                      jobject state, jobject collector)
	{
		// The game's entities first. Ours are meant to sit over them, and
		// submitting afterwards keeps that order within the bucket.
		if (ORIG_submit_entities && g_level_renderer_cls && thiz)
		{
			env->CallNonvirtualVoidMethod(thiz, g_level_renderer_cls, ORIG_submit_entities,
			                              pose_stack, state, collector);
			if (env->ExceptionCheck())
				env->ExceptionClear();
		}

		if (!g_submit_ready || !globals::esp_world_render_enabled)
			return;

		// submit_frame reads these instead of drawing, and they are only
		// meaningful for the length of this call: the collector does not exist
		// outside it, and the PoseStack is the frame's, not ours to keep.
		g_frame_pose = pose_stack;
		g_frame_collector = collector;
		g_frame_state = state;
		try { submit_frame(env); } catch (...) {}
		g_frame_pose = nullptr;
		g_frame_collector = nullptr;
		g_frame_state = nullptr;
	}

	// Proxy.newProxyInstance for a single-method interface, with a handler the
	// Java side builds. The same shape as the Fabric event subscription below.
	jobject make_proxy(JNIEnv* env, jclass iface, jint kind)
	{
		jclass class_cls = env->FindClass("java/lang/Class");
		jclass proxy_cls = env->FindClass("java/lang/reflect/Proxy");
		jclass handler_cls = env->FindClass("java/lang/reflect/InvocationHandler");
		clear_exception(env);
		if (!class_cls || !proxy_cls || !handler_cls)
			return nullptr;

		jmethodID get_loader = env->GetMethodID(class_cls, "getClassLoader",
			"()Ljava/lang/ClassLoader;");
		jmethodID new_proxy = env->GetStaticMethodID(proxy_cls, "newProxyInstance",
			"(Ljava/lang/ClassLoader;[Ljava/lang/Class;"
			"Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;");
		jmethodID mk_handler = env->GetStaticMethodID(g_renderer_class, "submitHandler",
			"(I)Ljava/lang/Object;");
		clear_exception(env);
		if (!get_loader || !new_proxy || !mk_handler)
			return nullptr;

		jobject handler = env->CallStaticObjectMethod(g_renderer_class, mk_handler, kind);
		clear_exception(env);
		if (!handler)
			return nullptr;

		// The proxy class has to be defined in a loader that can see the
		// interface, which is the game's, not ours.
		jobject loader = env->CallObjectMethod(iface, get_loader);
		clear_exception(env);

		jobjectArray ifaces = env->NewObjectArray(1, class_cls, iface);
		clear_exception(env);
		if (!ifaces)
			return nullptr;

		jobject local = env->CallStaticObjectMethod(proxy_cls, new_proxy,
		                                            loader, ifaces, handler);
		report_exception(env, "Proxy.newProxyInstance(CustomGeometryRenderer)");

		jobject global = local ? env->NewGlobalRef(local) : nullptr;
		if (local)
			env->DeleteLocalRef(local);
		env->DeleteLocalRef(ifaces);
		if (loader)
			env->DeleteLocalRef(loader);
		env->DeleteLocalRef(handler);
		return global;
	}

	// Which primitive a render type draws, as the enum constant's own name.
	// Empty when this version does not expose it.
	std::string read_topology(JNIEnv* env, jobject render_type)
	{
		if (!render_type || !sdk::mappings::have(sdk::mappings::render_type_topology_name))
			return std::string();

		jclass rt_cls = env->GetObjectClass(render_type);
		jmethodID mid = rt_cls
			? env->GetMethodID(rt_cls, sdk::mappings::render_type_topology_name,
			                   sdk::mappings::render_type_topology_sig)
			: nullptr;
		clear_exception(env);
		if (rt_cls)
			env->DeleteLocalRef(rt_cls);
		if (!mid)
			return std::string();

		jobject topo = env->CallObjectMethod(render_type, mid);
		clear_exception(env);
		if (!topo)
			return std::string();

		// It is an enum, so name() is java.lang.Enum's and needs no mapping.
		jclass topo_cls = env->GetObjectClass(topo);
		jmethodID mid_name = topo_cls
			? env->GetMethodID(topo_cls, "name", "()Ljava/lang/String;")
			: nullptr;
		clear_exception(env);
		if (topo_cls)
			env->DeleteLocalRef(topo_cls);

		jstring js = mid_name ? (jstring)env->CallObjectMethod(topo, mid_name) : nullptr;
		clear_exception(env);
		env->DeleteLocalRef(topo);
		if (!js)
			return std::string();

		const char* raw = env->GetStringUTFChars(js, nullptr);
		std::string out = raw ? raw : "";
		if (raw)
			env->ReleaseStringUTFChars(js, raw);
		env->DeleteLocalRef(js);
		return out;
	}

	// Element bits, mirrored in EnhanceRenderer.
	constexpr int k_elem_color = 1;
	constexpr int k_elem_normal = 2;
	constexpr int k_elem_uv0 = 4;
	constexpr int k_elem_uv1 = 8;
	constexpr int k_elem_uv2 = 16;
	constexpr int k_elem_line_width = 32;

	// What this render type declares per vertex, or false with the offending
	// name logged.
	//
	// The names come from the format itself rather than from a table here,
	// because a table is a guess and a guess costs a crash: a vertex missing an
	// element makes BufferBuilder.build() throw from inside the drain, after the
	// callback has returned.
	bool describe_format(JNIEnv* env, jobject render_type, const char* what, int& out_mask)
	{
		out_mask = 0;
		if (!render_type)
			return false;

		// Resolved earlier in init(), but this walks a java.util.List and a null
		// method id here would be a crash rather than a missing feature.
		if (!g_mid_list_size || !g_mid_list_get)
			return false;

		if (!sdk::mappings::have(sdk::mappings::render_type_format_name) ||
		    !sdk::mappings::have(sdk::mappings::vertex_format_element_name_name))
		{
			logger::log(std::string("[world_render] cannot read the vertex format on this "
			                        "version, so ") + what + " stays off");
			return false;
		}

		jclass rt_cls = env->GetObjectClass(render_type);
		jmethodID mid_format = rt_cls
			? env->GetMethodID(rt_cls, sdk::mappings::render_type_format_name,
			                   sdk::mappings::render_type_format_sig)
			: nullptr;
		clear_exception(env);
		if (rt_cls)
			env->DeleteLocalRef(rt_cls);
		if (!mid_format)
			return false;

		jobject fmt = env->CallObjectMethod(render_type, mid_format);
		clear_exception(env);
		if (!fmt)
			return false;

		jclass fmt_cls = env->GetObjectClass(fmt);
		jmethodID mid_elems = fmt_cls
			? env->GetMethodID(fmt_cls, sdk::mappings::vertex_format_get_elements_name,
			                   sdk::mappings::vertex_format_get_elements_sig)
			: nullptr;
		clear_exception(env);
		if (fmt_cls)
			env->DeleteLocalRef(fmt_cls);

		jobject list = mid_elems ? env->CallObjectMethod(fmt, mid_elems) : nullptr;
		clear_exception(env);
		env->DeleteLocalRef(fmt);
		if (!list)
			return false;

		const jint count = env->CallIntMethod(list, g_mid_list_size);
		clear_exception(env);

		std::string seen;
		bool ok = true;

		for (jint i = 0; i < count && ok; ++i)
		{
			jobject el = env->CallObjectMethod(list, g_mid_list_get, i);
			clear_exception(env);
			if (!el)
				continue;

			jclass el_cls = env->GetObjectClass(el);
			jmethodID mid_name = el_cls
				? env->GetMethodID(el_cls, sdk::mappings::vertex_format_element_name_name,
				                   sdk::mappings::vertex_format_element_name_sig)
				: nullptr;
			clear_exception(env);
			if (el_cls)
				env->DeleteLocalRef(el_cls);

			jstring js = mid_name ? (jstring)env->CallObjectMethod(el, mid_name) : nullptr;
			clear_exception(env);
			env->DeleteLocalRef(el);
			if (!js)
			{
				ok = false;
				break;
			}

			const char* raw = env->GetStringUTFChars(js, nullptr);
			const std::string name = raw ? raw : "";
			if (raw)
				env->ReleaseStringUTFChars(js, raw);
			env->DeleteLocalRef(js);

			seen += (seen.empty() ? "" : ", ") + name;

			if      (name == "Position")  { /* always, through addVertex */ }
			else if (name == "Color")     out_mask |= k_elem_color;
			else if (name == "Normal")    out_mask |= k_elem_normal;
			else if (name == "UV0" || name == "UV") out_mask |= k_elem_uv0;
			else if (name == "UV1")       out_mask |= k_elem_uv1;
			else if (name == "UV2")       out_mask |= k_elem_uv2;
			else if (name.find("Width") != std::string::npos ||
			         name.find("width") != std::string::npos) out_mask |= k_elem_line_width;
			else
			{
				// Do not draw rather than find out the hard way. The name is
				// printed because it is the one thing needed to support it.
				logger::log_error("[world_render] " + std::string(what) +
				                  " wants a vertex element this client cannot fill: '" +
				                  name + "' -- that half is off. Format: " + seen);
				ok = false;
			}
		}

		env->DeleteLocalRef(list);

		if (ok)
			logger::log("[world_render] " + std::string(what) + " vertex format: " + seen);
		return ok;
	}

	// Calls a RenderTypes factory once and keeps what it returns.
	jobject resolve_render_type(JNIEnv* env, jclass types, const char* name, const char* sig)
	{
		if (!sdk::mappings::have(name))
			return nullptr;

		jmethodID mid = env->GetStaticMethodID(types, name, sig);
		clear_exception(env);
		if (!mid)
			return nullptr;

		jobject local = env->CallStaticObjectMethod(types, mid);
		clear_exception(env);
		if (!local)
			return nullptr;

		jobject global = env->NewGlobalRef(local);
		env->DeleteLocalRef(local);
		return global;
	}

	// The name tag path, which is allowed to fail on its own: without it the
	// boxes still draw, and saying so beats taking the feature down with it.
	void resolve_name_tags(JNIEnv* env, jclass ordered_cls)
	{
		g_tags_ready = false;

		if (!sdk::mappings::have(sdk::mappings::submit_name_tag_name))
		{
			logger::log("[world_render] no submitNameTag on this version -- in-world tags "
			            "stay off on the submit path");
			return;
		}

		g_mid_submit_name_tag = env->GetMethodID(ordered_cls,
			sdk::mappings::submit_name_tag_name, sdk::mappings::submit_name_tag_sig);
		clear_exception(env);

		jclass lrs_cls = sdk::classloader::find_class(env,
			sdk::mappings::level_render_state_class_sig);
		if (lrs_cls)
		{
			g_fid_lrs_camera = env->GetFieldID(lrs_cls,
				sdk::mappings::level_render_state_camera_name,
				sdk::mappings::level_render_state_camera_sig);
			clear_exception(env);
			env->DeleteLocalRef(lrs_cls);
		}

		jclass pose_cls = sdk::classloader::find_class(env, sdk::mappings::pose_stack_class_sig);
		if (pose_cls)
		{
			g_mid_pose_push = env->GetMethodID(pose_cls,
				sdk::mappings::pose_stack_push_name, sdk::mappings::pose_stack_push_sig);
			g_mid_pose_pop = env->GetMethodID(pose_cls,
				sdk::mappings::pose_stack_pop_name, sdk::mappings::pose_stack_pop_sig);
			g_mid_pose_translate = env->GetMethodID(pose_cls,
				sdk::mappings::pose_stack_translate_name,
				sdk::mappings::pose_stack_translate_sig);
			clear_exception(env);
			env->DeleteLocalRef(pose_cls);
		}

		jclass vec3_cls = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
		if (vec3_cls)
		{
			jfieldID fid = env->GetStaticFieldID(vec3_cls,
				sdk::mappings::vec3_zero_name, sdk::mappings::vec3_zero_sig);
			clear_exception(env);
			if (fid)
			{
				jobject local = env->GetStaticObjectField(vec3_cls, fid);
				clear_exception(env);
				if (local)
				{
					g_vec3_zero = env->NewGlobalRef(local);
					env->DeleteLocalRef(local);
				}
			}
			env->DeleteLocalRef(vec3_cls);
		}

		jclass comp_cls = sdk::classloader::find_class(env, sdk::mappings::text_class_sig);
		if (comp_cls)
		{
			g_mid_component_of = env->GetStaticMethodID(comp_cls,
				sdk::mappings::component_null_to_empty_name,
				sdk::mappings::component_null_to_empty_sig);
			clear_exception(env);
			if (g_mid_component_of)
				g_component_cls = (jclass)env->NewGlobalRef(comp_cls);
			env->DeleteLocalRef(comp_cls);
		}

		g_tags_ready = g_mid_submit_name_tag && g_fid_lrs_camera && g_vec3_zero &&
		               g_component_cls && g_mid_component_of && g_mid_pose_push &&
		               g_mid_pose_pop && g_mid_pose_translate;

		logger::log(g_tags_ready
			? "[world_render] in-world name tags go through the game's own font and phase"
			: "[world_render] the name tag path did not resolve fully -- boxes only");
	}

	// Everything the submit path needs, or false and a reason.
	bool init_submit_path(JNIEnv* env)
	{
		if (!sdk::mappings::have(sdk::mappings::submit_custom_geometry_name) ||
		    !sdk::mappings::have(sdk::mappings::level_renderer_submit_entities_name))
		{
			logger::log("[world_render] no submit-node path on this version (it arrives in "
			            "1.21.9) -- falling back to the Fabric event and OpenGL");
			return false;
		}

		jclass collector_cls = sdk::classloader::find_class(env,
			sdk::mappings::submit_node_collector_class_sig);
		jclass ordered_cls = sdk::classloader::find_class(env,
			sdk::mappings::ordered_submit_collector_class_sig);
		jclass cgr_cls = sdk::classloader::find_class(env,
			sdk::mappings::custom_geometry_renderer_class_sig);
		jclass types_cls = sdk::classloader::find_class(env,
			sdk::mappings::render_types_class_sig);
		jclass consumer_cls = sdk::classloader::find_class(env,
			sdk::mappings::vertex_consumer_class_sig);
		jclass pose_cls = sdk::classloader::find_class(env,
			sdk::mappings::pose_stack_pose_class_sig);
		jclass lr_cls = sdk::classloader::find_class(env,
			sdk::mappings::world_renderer_class_sig);

		if (!collector_cls || !ordered_cls || !cgr_cls || !types_cls ||
		    !consumer_cls || !pose_cls || !lr_cls)
		{
			logger::log_error("[world_render] the submit-node classes are named in the "
			                  "mappings but did not resolve in this JVM");
			return false;
		}

		g_mid_order = env->GetMethodID(collector_cls,
			sdk::mappings::submit_node_order_name, sdk::mappings::submit_node_order_sig);
		clear_exception(env);
		g_mid_submit_custom = env->GetMethodID(ordered_cls,
			sdk::mappings::submit_custom_geometry_name,
			sdk::mappings::submit_custom_geometry_sig);
		clear_exception(env);
		if (!g_mid_order || !g_mid_submit_custom)
		{
			logger::log_error("[world_render] order() or submitCustomGeometry did not resolve");
			return false;
		}

		// --- the line half ---------------------------------------------------
		g_rt_lines = resolve_render_type(env, types_cls,
			sdk::mappings::render_type_lines_name, sdk::mappings::render_type_lines_sig);
		if (g_rt_lines)
		{
			const std::string topo = read_topology(env, g_rt_lines);
			logger::log("[world_render] box edges topology: " + (topo.empty() ? "unknown" : topo));
			if (!topo.empty() && topo.find("LINE") == std::string::npos)
			{
				logger::log_error("[world_render] the line render type does not draw lines -- "
				                  "that half is off");
				env->DeleteGlobalRef(g_rt_lines);
				g_rt_lines = nullptr;
			}
		}

		// --- the filled half -------------------------------------------------
		//
		// The geometry is a triangle LIST, so the render type has to be one that
		// reads it as one. DEBUG_FILLED_BOX is a strip: every vertex continues
		// the previous two, which turns box faces into uneven halves with sides
		// missing a triangle. DEBUG_QUADS reads four vertices at a time, and a
		// triangle becomes an exact quad by repeating its last vertex.
		jobject rt_quads = resolve_render_type(env, types_cls,
			sdk::mappings::render_type_debug_quads_name,
			sdk::mappings::render_type_debug_quads_sig);
		jobject rt_box = resolve_render_type(env, types_cls,
			sdk::mappings::render_type_debug_filled_box_name,
			sdk::mappings::render_type_debug_filled_box_sig);

		const std::string topo_quads = read_topology(env, rt_quads);
		const std::string topo_box = read_topology(env, rt_box);
		logger::log("[world_render] filled candidates: debugQuads=" +
		            (topo_quads.empty() ? "n/a" : topo_quads) + " debugFilledBox=" +
		            (topo_box.empty() ? "n/a" : topo_box));

		g_tris_as_quads = false;
		if (rt_quads && topo_quads == "QUADS")
		{
			g_rt_tris = rt_quads;
			rt_quads = nullptr;
			g_tris_as_quads = true;
		}
		else if (rt_box && topo_box == "TRIANGLES")
		{
			g_rt_tris = rt_box;
			rt_box = nullptr;
		}
		else
		{
			logger::log_error("[world_render] no render type on this version reads a triangle "
			                  "list -- the filled half is off, edges are unaffected");
			g_rt_tris = nullptr;
		}

		if (rt_quads) env->DeleteGlobalRef(rt_quads);
		if (rt_box)   env->DeleteGlobalRef(rt_box);

		if (!g_rt_lines && !g_rt_tris)
		{
			logger::log_error("[world_render] neither half can be drawn -- there would be "
			                  "nothing to submit");
			return false;
		}

		// Hand the Java side the vertex API. It compiles against the JDK and
		// LWJGL only, so it cannot name these types, and on a vanilla jar the
		// method names are obfuscated -- only this side has the mappings.
		jmethodID bind = env->GetStaticMethodID(g_renderer_class, "bindVertexApi",
			"(Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;"
			"Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
			"Ljava/lang/String;)Z");
		g_mid_stage_geometry = env->GetStaticMethodID(g_renderer_class, "stageGeometry",
			"(Ljava/nio/ByteBuffer;II)V");
		clear_exception(env);
		if (!bind || !g_mid_stage_geometry)
		{
			logger::log("[world_render] this JVM holds an older EnhanceRenderer without the "
			            "submit entry points -- restart the game to pick up the new one");
			return false;
		}

		// A name this version does not have goes over as empty: the Java side
		// treats those setters as optional and only a format that actually asks
		// for the element makes it matter.
		auto name_or_empty = [](const char* n) { return sdk::mappings::have(n) ? n : ""; };

		jstring s_add = env->NewStringUTF(sdk::mappings::vertex_add_vertex_name);
		jstring s_col = env->NewStringUTF(sdk::mappings::vertex_set_color_name);
		jstring s_nrm = env->NewStringUTF(sdk::mappings::vertex_set_normal_name);
		jstring s_lw  = env->NewStringUTF(name_or_empty(sdk::mappings::vertex_set_line_width_name));
		jstring s_uv  = env->NewStringUTF(name_or_empty(sdk::mappings::vertex_set_uv_name));
		jstring s_uv1 = env->NewStringUTF(name_or_empty(sdk::mappings::vertex_set_uv1_name));
		jstring s_uv2 = env->NewStringUTF(name_or_empty(sdk::mappings::vertex_set_uv2_name));
		const jboolean bound = env->CallStaticBooleanMethod(g_renderer_class, bind,
			consumer_cls, pose_cls, s_add, s_col, s_nrm, s_lw, s_uv, s_uv1, s_uv2);
		report_exception(env, "EnhanceRenderer.bindVertexApi");
		for (jstring js : { s_add, s_col, s_nrm, s_lw, s_uv, s_uv1, s_uv2 })
			env->DeleteLocalRef(js);

		if (bound != JNI_TRUE)
		{
			logger::log_error("[world_render] the vertex API did not bind -- the names are "
			                  "right for this version but the reflection lookup failed");
			return false;
		}

		// What each half's render type actually declares. A half whose format
		// cannot be filled is switched off here rather than on the frame that
		// would have crashed the game.
		int mask_tris = 0, mask_lines = 0;
		if (g_rt_tris && !describe_format(env, g_rt_tris, "filled boxes", mask_tris))
		{
			env->DeleteGlobalRef(g_rt_tris);
			g_rt_tris = nullptr;
		}
		if (g_rt_lines && !describe_format(env, g_rt_lines, "box edges", mask_lines))
		{
			env->DeleteGlobalRef(g_rt_lines);
			g_rt_lines = nullptr;
		}

		jmethodID can_fill = env->GetStaticMethodID(g_renderer_class, "canFill", "(I)Z");
		jmethodID set_masks = env->GetStaticMethodID(g_renderer_class, "setElementMasks", "(II)V");
		g_mid_set_masks = set_masks;
		g_mid_set_quads = env->GetStaticMethodID(g_renderer_class, "setTrisAsQuads", "(Z)V");
		clear_exception(env);
		clear_exception(env);
		if (!can_fill || !set_masks)
		{
			logger::log("[world_render] this JVM holds an older EnhanceRenderer without the "
			            "format-aware emitter -- restart the game to pick up the new one");
			return false;
		}

		if (g_rt_tris && env->CallStaticBooleanMethod(g_renderer_class, can_fill, mask_tris) != JNI_TRUE)
		{
			logger::log_error("[world_render] no setter bound for something the filled-box "
			                  "format wants -- that half is off");
			env->DeleteGlobalRef(g_rt_tris);
			g_rt_tris = nullptr;
		}
		if (g_rt_lines && env->CallStaticBooleanMethod(g_renderer_class, can_fill, mask_lines) != JNI_TRUE)
		{
			logger::log_error("[world_render] no setter bound for something the line format "
			                  "wants -- that half is off");
			env->DeleteGlobalRef(g_rt_lines);
			g_rt_lines = nullptr;
		}
		clear_exception(env);

		if (!g_rt_tris && !g_rt_lines)
		{
			logger::log_error("[world_render] neither half can be drawn through the game's "
			                  "renderer on this version");
			return false;
		}

		g_mask_tris = mask_tris;
		g_mask_lines = mask_lines;
		env->CallStaticVoidMethod(g_renderer_class, set_masks, mask_tris, mask_lines);
		clear_exception(env);

		if (jmethodID set_quads = env->GetStaticMethodID(g_renderer_class,
		        "setTrisAsQuads", "(Z)V"))
		{
			env->CallStaticVoidMethod(g_renderer_class, set_quads,
			                          g_tris_as_quads ? JNI_TRUE : JNI_FALSE);
		}
		clear_exception(env);

		resolve_name_tags(env, ordered_cls);

		g_proxy_tris = make_proxy(env, cgr_cls, 1);    // KIND_SUBMIT_TRIS
		g_proxy_lines = make_proxy(env, cgr_cls, 2);   // KIND_SUBMIT_LINES
		if (!g_proxy_tris || !g_proxy_lines)
		{
			logger::log_error("[world_render] could not build the custom-geometry proxies");
			return false;
		}

		g_ordered_cls = (jclass)env->NewGlobalRef(ordered_cls);
		g_cgr_cls = (jclass)env->NewGlobalRef(cgr_cls);
		g_level_renderer_cls = (jclass)env->NewGlobalRef(lr_cls);

		g_submit_mid = env->GetMethodID(lr_cls,
			sdk::mappings::level_renderer_submit_entities_name,
			sdk::mappings::level_renderer_submit_entities_sig);
		clear_exception(env);
		if (!g_submit_mid)
		{
			logger::log_error("[world_render] LevelRenderer.submitEntities did not resolve");
			return false;
		}

		return true;
	}

	// The attach itself, and the only part that may not run on the worker.
	// A jmethodID is thread-safe once resolved, so it travels here from the
	// resolve above.
	bool attach_submit_entities()
	{
		if (!g_submit_mid)
			return false;

		const jnihook_result_t r = JNIHook_Attach(g_submit_mid,
			reinterpret_cast<void*>(hkSubmitEntities), &ORIG_submit_entities);
		if (r != JNIHOOK_OK)
		{
			// result=6 is JNIHOOK_ERR_JVMTI_OPERATION, which is returned from
			// several places and names nothing by itself. The JVMTI error behind
			// it and the capability set Init settled for are both available --
			// anything but "full" means another agent already holds something we
			// wanted, and can_suspend in particular is solo per JVM and is not
			// released promptly, so a re-injection routinely goes without it.
			char line[320];
			sprintf_s(line, sizeof(line),
			          "[world_render] hook on submitEntities failed=%d jvmti=%d caps=%s %s",
			          (int)r, JNIHook_LastJvmtiError(),
			          JNIHook_AcquiredCapabilities() ? JNIHook_AcquiredCapabilities() : "?",
			          JNIHook_LastErrorDetail() ? JNIHook_LastErrorDetail() : "");
			logger::log_error(line);
			return false;
		}

		g_hooked_submit_entities = g_submit_mid;
		g_submit_ready = true;
		logger::log("[world_render] in-world geometry goes through the game's own renderer "
		            "(submitCustomGeometry) -- no Fabric API needed, and whichever backend "
		            "the game is running draws it");
		return true;
	}

	enum class submit_state { pending, ready, unavailable };

	// Resolve once, then keep trying to attach until it takes or until it is
	// clearly never going to.
	//
	// Three outcomes, because the caller must tell "not yet" from "never":
	// falling back to the Fabric event while an attach is still being retried
	// would subscribe to both and draw everything twice.
	//
	// The retry is on a clock rather than per call. The same attach succeeded
	// five seconds after one injection and failed eighty milliseconds after the
	// next, which is the shape of a game that has not finished coming up --
	// and the caller retries every worker iteration, so ungated attempts would
	// all be spent inside the first fiftieth of a second.
	submit_state ensure_submit_path(JNIEnv* env)
	{
		static bool      s_resolved = false;
		static bool      s_refused = false;
		static int       s_attempts = 0;
		static ULONGLONG s_next_try = 0;

		if (g_submit_ready)
		{
			g_submit_pending = false;
			return submit_state::ready;
		}
		if (s_refused)
		{
			g_submit_pending = false;
			return submit_state::unavailable;
		}

		if (!s_resolved)
		{
			if (!init_submit_path(env))
			{
				s_refused = true;
				g_submit_pending = false;
				return submit_state::unavailable;
			}
			s_resolved = true;
		}

		const ULONGLONG now = GetTickCount64();
		if (now < s_next_try)
		{
			g_submit_pending = true;
			return submit_state::pending;
		}
		s_next_try = now + 2000;

		// Posted rather than called: post runs inline today, but if a drain site
		// ever exists where the client thread is executing Java rather than
		// sitting in a native call, this attach should move there with the rest.
		g_submit_attach_failed = false;
		enhance::client_thread::post([]() {
			if (!attach_submit_entities())
				g_submit_attach_failed = true;
		});

		if (g_submit_ready)
		{
			g_submit_pending = false;
			return submit_state::ready;
		}

		if (++s_attempts >= 10)
		{
			logger::log_error("[world_render] submitEntities refused the hook ten times over "
			                  "twenty seconds -- giving up and falling back to the Fabric "
			                  "event");
			s_refused = true;
			g_submit_pending = false;
			return submit_state::unavailable;
		}

		char line[160];
		sprintf_s(line, sizeof(line),
		          "[world_render] attach attempt %d did not take; trying again in 2s",
		          s_attempts);
		logger::log(line);

		g_submit_pending = true;
		return submit_state::pending;
	}

	bool register_event(JNIEnv* env)
	{
		jclass class_cls = env->FindClass("java/lang/Class");
		jclass proxy_cls = env->FindClass("java/lang/reflect/Proxy");
		clear_exception(env);
		if (!class_cls || !proxy_cls)
			return false;

		jmethodID get_loader = env->GetMethodID(class_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
		jmethodID new_proxy = env->GetStaticMethodID(proxy_cls, "newProxyInstance",
			"(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;");
		clear_exception(env);
		if (!get_loader || !new_proxy)
			return false;

		jclass events_cls = load_initialised(env, k_events_class);
		if (!events_cls)
		{
			logger::log_error("[world_render] could not resolve " +
			                  std::string(k_events_class) + ". Asking the JVM what it "
			                  "actually has loaded:");

			// find_class swallows the ClassNotFoundException, so "absent" and
			// "present under another name" look identical from here. The JVM's
			// own list tells them apart.
			sdk::java::dump_loaded_classes_matching(env, "WorldRenderEvents", 20);
			sdk::java::dump_loaded_classes_matching(env, "fabric/api/client/rendering", 40);
			sdk::java::dump_loaded_classes_matching(env, "fabric/api/event", 20);
			return false;
		}

		// --- Discover the events instead of naming them ----------------------
		//
		// Every static field whose type is an Event is a candidate. The Event
		// class itself comes out of the field signature rather than a constant,
		// so a move of that package cannot break this the way the last one did.
		struct discovered_t
		{
			std::string field;
			std::string sig;     // e.g. "Lnet/fabricmc/fabric/api/event/Event;"
		};

		std::vector<discovered_t> found;

		if (jvmtiEnv* jvmti = sdk::java::jvmti())
		{
			jint field_count = 0;
			jfieldID* fields = nullptr;

			const jvmtiError fields_err = jvmti->GetClassFields(events_cls, &field_count, &fields);
			if (fields_err != JVMTI_ERROR_NONE)
			{
				// 22 is CLASS_NOT_PREPARED, which is the one worth naming: it
				// means the class resolved but was never linked.
				logger::log_error("[world_render] GetClassFields failed (jvmtiError " +
				                  std::to_string(static_cast<int>(fields_err)) + ")");
			}

			if (fields_err == JVMTI_ERROR_NONE)
			{
				for (jint i = 0; i < field_count; ++i)
				{
					char* name = nullptr;
					char* sig = nullptr;

					if (jvmti->GetFieldName(events_cls, fields[i], &name, &sig, nullptr) == JVMTI_ERROR_NONE)
					{
						if (name && sig && std::strstr(sig, "/Event;"))
							found.push_back({ name, sig });

						if (name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
						if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
					}
				}

				jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
			}
		}

		if (found.empty())
		{
			logger::log_error("[world_render] WorldRenderEvents exposes no Event fields — "
			                  "this is not the class this expects.");
			return false;
		}

		{
			std::string names;
			for (const auto& f : found)
				names += (names.empty() ? "" : ", ") + f.field;
			logger::log("[world_render] events offered by this Fabric API: " + names);
		}

		// Preferred ones first, in the order listed; everything else after, so
		// an unfamiliar API still gets tried rather than refused.
		std::stable_sort(found.begin(), found.end(),
			[](const discovered_t& a, const discovered_t& b)
			{
				auto rank = [](const std::string& name) -> int
				{
					int i = 0;
					for (const char* pref : k_event_preference)
					{
						if (name == pref)
							return i;
						++i;
					}
					return i;   // unlisted: after every named preference
				};

				return rank(a.field) < rank(b.field);
			});

		jmethodID ctor = env->GetMethodID(g_renderer_class, "<init>", "()V");
		clear_exception(env);
		jobject handler = ctor ? env->NewObject(g_renderer_class, ctor) : nullptr;
		clear_exception(env);
		if (!handler)
		{
			logger::log_error("[world_render] could not instantiate the renderer handler");
			return false;
		}

		bool registered = false;

		for (const auto& candidate : found)
		{
			// Event class from the field's own signature: strip the leading L
			// and the trailing ;.
			const std::string event_binary = candidate.sig.substr(1, candidate.sig.size() - 2);

			jclass event_cls = sdk::classloader::find_class(env, event_binary.c_str());
			clear_exception(env);
			jmethodID register_mid = event_cls
				? env->GetMethodID(event_cls, "register", "(Ljava/lang/Object;)V")
				: nullptr;
			clear_exception(env);
			if (event_cls)
				env->DeleteLocalRef(event_cls);

			if (!register_mid)
				continue;

			const std::string iface_name =
				std::string(k_events_class) + "$" + field_to_interface_name(candidate.field);

			jclass iface = sdk::classloader::find_class(env, iface_name.c_str());
			clear_exception(env);
			if (!iface)
				continue;

			jfieldID field = env->GetStaticFieldID(events_cls, candidate.field.c_str(),
				candidate.sig.c_str());
			clear_exception(env);
			if (!field)
			{
				env->DeleteLocalRef(iface);
				continue;
			}

			jobject event = env->GetStaticObjectField(events_cls, field);
			clear_exception(env);
			if (!event)
			{
				env->DeleteLocalRef(iface);
				continue;
			}

			// The proxy must be defined in a loader that can see the interface;
			// the interface's own is the one loader guaranteed to.
			jobject loader = env->CallObjectMethod(iface, get_loader);
			clear_exception(env);

			jobjectArray ifaces = env->NewObjectArray(1, class_cls, iface);
			clear_exception(env);

			jobject proxy = ifaces
				? env->CallStaticObjectMethod(proxy_cls, new_proxy, loader, ifaces, handler)
				: nullptr;
			clear_exception(env);

			if (proxy)
			{
				// Publish the handler as current BEFORE subscribing: the event
				// can fire on the very next frame, and a listener that does not
				// match `current` does nothing.
				jvalue v;
				v.l = handler;
				call_renderer_setter(env, "setCurrent", "(Ljava/lang/Object;)V", v);

				env->CallVoidMethod(event, register_mid, proxy);

				if (env->ExceptionCheck())
				{
					env->ExceptionClear();
					logger::log_error("[world_render] register() threw for " + candidate.field);
				}
				else
				{
					registered = true;
					logger::log("[world_render] subscribed to WorldRenderEvents." +
					            candidate.field + " — no class was hooked or redefined.");
				}

				env->DeleteLocalRef(proxy);
			}

			if (ifaces) env->DeleteLocalRef(ifaces);
			if (loader) env->DeleteLocalRef(loader);
			env->DeleteLocalRef(event);
			env->DeleteLocalRef(iface);

			if (registered)
				break;
		}

		env->DeleteLocalRef(handler);

		if (!registered)
			logger::log_error("[world_render] none of the offered events could be "
			                  "subscribed to — see the list logged above.");

		return registered;
	}
}

bool enhance::modules::world_render_hook::has_mapping()
{
	return mapping_present();
}

void enhance::modules::world_render_hook::dump_mappings()
{
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (env)
		sdk::java::dump_render_mappings(env);
}

bool enhance::modules::world_render_hook::is_pending()
{
	return g_submit_pending;
}

bool enhance::modules::world_render_hook::is_attached()
{
	return g_attached;
}

bool enhance::modules::world_render_hook::init()
{
	if (g_attached)
		return true;

	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	auto jvm = enhance::instance ? enhance::instance->get_java_vm() : nullptr;
	if (!env || !jvm)
		return false;

	if (!mapping_present())
	{
		logger::log_error("[world_render] core class mappings missing — the renderer stays "
		                  "off. Fill them in mappings.hpp from the [jvmti] dump above.");
		return false;
	}

	// --- Java renderer, defined straight from the DLL's memory --------------
	g_renderer_class = sdk::java::define_from_memory(env, k_renderer_binary_name,
	                                                  enhance_renderer_class,
	                                                  enhance_renderer_class_size);
	if (!g_renderer_class)
	{
		logger::log_error("[world_render] could not define the Java renderer class");
		return false;
	}

	g_renderer_render = env->GetStaticMethodID(g_renderer_class, "render",
		"(Ljava/nio/ByteBuffer;II[FZ)V");
	clear_exception(env);
	if (!g_renderer_render)
	{
		logger::log_error("[world_render] EnhanceRenderer.render not found");
		return false;
	}

	// Text is optional: an older EnhanceRenderer already defined into this JVM
	// by a previous injection has no renderText, and boxes should still work.
	g_renderer_render_text = env->GetStaticMethodID(g_renderer_class, "renderText",
		"(Ljava/nio/ByteBuffer;I[FZ)V");
	clear_exception(env);
	g_renderer_upload_font = env->GetStaticMethodID(g_renderer_class, "uploadFont",
		"(Ljava/nio/ByteBuffer;II)I");
	clear_exception(env);

	if (!g_renderer_render_text || !g_renderer_upload_font)
	{
		logger::log("[world_render] this JVM holds an older EnhanceRenderer without the text "
		            "entry points — in-world name tags stay off until the game restarts. "
		            "Boxes are unaffected.");
	}

	// --- Minecraft accessors ------------------------------------------------
	g_mc_class = global_class(env, sdk::mappings::minecraftclass_sig);
	g_world_class = global_class(env, sdk::mappings::client_world_class_sig);
	g_entity_class = global_class(env, sdk::mappings::entity_class_sig);
	g_box_class = global_class(env, sdk::mappings::aabb_class_sig);

	if (!g_mc_class || !g_world_class || !g_entity_class || !g_box_class)
	{
		// Name the one that failed. The previous message said only that
		// something had not resolved, and the answer turned out to be a
		// hardcoded intermediary name for AABB that exists on neither 26.x nor
		// a vanilla jar -- a whole session's worth of the feature quietly
		// unticking itself, which this line would have answered immediately.
		std::string missing;
		if (!g_mc_class)     missing += std::string(" ") + sdk::mappings::minecraftclass_sig;
		if (!g_world_class)  missing += std::string(" ") + sdk::mappings::client_world_class_sig;
		if (!g_entity_class) missing += std::string(" ") + sdk::mappings::entity_class_sig;
		if (!g_box_class)    missing += std::string(" ") + sdk::mappings::aabb_class_sig;
		logger::log_error("[world_render] these core classes did not resolve:" + missing);
		return false;
	}

	{
		jclass list_local = env->FindClass("java/util/List");
		clear_exception(env);
		if (list_local)
		{
			g_list_class = static_cast<jclass>(env->NewGlobalRef(list_local));
			g_mid_list_size = env->GetMethodID(list_local, "size", "()I");
			clear_exception(env);
			g_mid_list_get = env->GetMethodID(list_local, "get", "(I)Ljava/lang/Object;");
			clear_exception(env);
			env->DeleteLocalRef(list_local);
		}
	}

	g_fid_mc_instance = env->GetStaticFieldID(g_mc_class, sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
	clear_exception(env);
	g_fid_mc_world = env->GetFieldID(g_mc_class, sdk::mappings::world_name, sdk::mappings::world_sig);
	clear_exception(env);
	g_fid_mc_player = env->GetFieldID(g_mc_class, sdk::mappings::player_name, sdk::mappings::player_sig);
	clear_exception(env);

	g_fid_world_players = env->GetFieldID(g_world_class, sdk::mappings::players_field_name, sdk::mappings::players_field_sig);
	clear_exception(env);

	g_mid_entity_get_x = env->GetMethodID(g_entity_class, sdk::mappings::entity_get_x_name, sdk::mappings::entity_get_x_sig);
	clear_exception(env);
	g_mid_entity_get_y = env->GetMethodID(g_entity_class, sdk::mappings::entity_get_y_name, sdk::mappings::entity_get_y_sig);
	clear_exception(env);
	g_mid_entity_get_z = env->GetMethodID(g_entity_class, sdk::mappings::entity_get_z_name, sdk::mappings::entity_get_z_sig);
	clear_exception(env);
	g_fid_entity_box = env->GetFieldID(g_entity_class, sdk::mappings::get_bounding_box_name, sdk::mappings::get_bounding_box_sig);
	clear_exception(env);

	g_fid_box_min_x = env->GetFieldID(g_box_class, sdk::mappings::box_min_x_name, sdk::mappings::box_min_x_sig);
	clear_exception(env);
	g_fid_box_min_y = env->GetFieldID(g_box_class, sdk::mappings::box_min_y_name, sdk::mappings::box_min_y_sig);
	clear_exception(env);
	g_fid_box_min_z = env->GetFieldID(g_box_class, sdk::mappings::box_min_z_name, sdk::mappings::box_min_z_sig);
	clear_exception(env);
	g_fid_box_max_x = env->GetFieldID(g_box_class, sdk::mappings::box_max_x_name, sdk::mappings::box_max_x_sig);
	clear_exception(env);
	g_fid_box_max_y = env->GetFieldID(g_box_class, sdk::mappings::box_max_y_name, sdk::mappings::box_max_y_sig);
	clear_exception(env);
	g_fid_box_max_z = env->GetFieldID(g_box_class, sdk::mappings::box_max_z_name, sdk::mappings::box_max_z_sig);
	clear_exception(env);

	if (!g_fid_mc_instance || !g_fid_mc_world || !g_fid_world_players ||
	    !g_mid_list_size || !g_mid_list_get || !g_fid_entity_box ||
	    !g_fid_box_min_x || !g_fid_box_max_z)
	{
		logger::log_error("[world_render] required Minecraft accessors missing");
		return false;
	}

	// --- Optional: per-frame interpolation ----------------------------------
	// Missing mappings here only cost smoothness, so they must never block the
	// hook. Without them the boxes sit on the raw tick position.
	if (sdk::mappings::entity_last_render_x_name[0])
	{
		g_fid_last_render_x = env->GetFieldID(g_entity_class, sdk::mappings::entity_last_render_x_name, sdk::mappings::entity_last_render_sig);
		clear_exception(env);
		g_fid_last_render_y = env->GetFieldID(g_entity_class, sdk::mappings::entity_last_render_y_name, sdk::mappings::entity_last_render_sig);
		clear_exception(env);
		g_fid_last_render_z = env->GetFieldID(g_entity_class, sdk::mappings::entity_last_render_z_name, sdk::mappings::entity_last_render_sig);
		clear_exception(env);
	}

	if (sdk::mappings::mc_render_tick_counter_name[0] && sdk::mappings::render_tick_counter_class_sig[0])
	{
		g_fid_mc_tick_counter = env->GetFieldID(g_mc_class, sdk::mappings::mc_render_tick_counter_name, sdk::mappings::mc_render_tick_counter_sig);
		clear_exception(env);

		if (jclass counter_cls = sdk::classloader::find_class(env, sdk::mappings::render_tick_counter_class_sig))
		{
			g_mid_tick_progress = resolve_tick_progress(env, counter_cls);
			env->DeleteLocalRef(counter_cls);
		}
	}

	// --- Optional: name tag sources -----------------------------------------
	g_mid_scoreboard_name = env->GetMethodID(g_entity_class,
		sdk::mappings::entity_scoreboard_name_name,
		sdk::mappings::entity_scoreboard_name_sig);
	clear_exception(env);

	g_living_class = global_class(env, sdk::mappings::living_entity_class_sig);
	if (g_living_class)
	{
		g_mid_get_health = env->GetMethodID(g_living_class,
			sdk::mappings::living_entity_get_health_name,
			sdk::mappings::living_entity_get_health_sig);
		clear_exception(env);
		g_mid_get_max_health = env->GetMethodID(g_living_class,
			sdk::mappings::living_entity_get_max_health_name,
			sdk::mappings::living_entity_get_max_health_sig);
		clear_exception(env);
	}

	if (!g_fid_last_render_x || !g_mid_tick_progress)
	{
		logger::log("[world_render] interpolation mappings absent — boxes will sit on the "
		            "tick position (no per-frame smoothing).");
	}

	// --- Native entry point -------------------------------------------------
	// Bound on our own class, which nothing else owns — the whole point of the
	// event route is that no Minecraft class is modified.
	{
		JNINativeMethod native = {
			const_cast<char*>("frame"),
			const_cast<char*>("()V"),
			reinterpret_cast<void*>(native_frame),
		};

		if (env->RegisterNatives(g_renderer_class, &native, 1) != JNI_OK)
		{
			clear_exception(env);
			logger::log_error("[world_render] RegisterNatives failed for EnhanceRenderer.frame");
			return false;
		}
		clear_exception(env);
		g_natives_registered = true;
	}

	// Arm before subscribing, or the first event would find the listener
	// inactive and skip a frame.
	{
		jvalue v;
		v.z = JNI_TRUE;
		call_renderer_setter(env, "setActive", "(Z)V", v);
	}

	// --- How the frame is reached -------------------------------------------
	//
	// The submit path first where it exists: it needs no Fabric API, so it also
	// covers a vanilla install, and it hands the geometry to the game rather than
	// drawing it behind the game's back. The event route stays as the fallback
	// for everything older.
	switch (ensure_submit_path(env))
	{
	case submit_state::ready:
		g_attached = true;
		logger::log("[world_render] in-world Java renderer attached");
		return true;

	case submit_state::pending:
		// Not a failure: the attach is queued for the client thread. Returning
		// false has the caller try again next tick, which is what is wanted --
		// what must NOT happen is falling through to the event route meanwhile.
		return false;

	case submit_state::unavailable:
		break;
	}

	// --- Subscribe -----------------------------------------------------------
	if (!register_event(env))
	{
		jvalue v;
		v.z = JNI_FALSE;
		call_renderer_setter(env, "setActive", "(Z)V", v);
		env->UnregisterNatives(g_renderer_class);
		clear_exception(env);
		g_natives_registered = false;
		return false;
	}

	g_attached = true;
	logger::log("[world_render] in-world Java renderer attached");
	return true;
}

void enhance::modules::world_render_hook::shutdown()
{
	if (!g_attached)
		return;

	g_attached = false;

	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;

	// The submit path goes first and in this order: stop the hook from calling
	// into us, then give the method back. A native method left bound to a module
	// that is about to be unmapped is a crash on the next frame, not a leak.
	g_submit_ready = false;
	g_frame_pose = nullptr;
	g_frame_collector = nullptr;

	if (g_hooked_submit_entities)
	{
		const jnihook_result_t r = JNIHook_Detach(g_hooked_submit_entities);
		logger::log(std::string("[unload] submitEntities detach result=") +
		            std::to_string((int)r));
		g_hooked_submit_entities = nullptr;
		ORIG_submit_entities = nullptr;
	}

	if (env)
	{
		for (jobject* ref : { &g_rt_lines, &g_rt_tris, &g_proxy_tris, &g_proxy_lines,
		                      &g_vec3_zero, &g_rt_text, &g_proxy_text })
		{
			if (*ref)
			{
				env->DeleteGlobalRef(*ref);
				*ref = nullptr;
			}
		}
		for (jclass* ref : { &g_ordered_cls, &g_level_renderer_cls, &g_component_cls,
		                     &g_cgr_cls })
		{
			if (*ref)
			{
				env->DeleteGlobalRef(*ref);
				*ref = nullptr;
			}
		}
	}
	g_mid_order = nullptr;
	g_mid_submit_custom = nullptr;
	g_mid_stage_geometry = nullptr;
	g_tags_ready = false;
	g_mid_submit_name_tag = nullptr;

	// The subscription cannot be undone — Fabric's Event has no unregister —
	// so the listener has to be told to stand down instead. Order matters:
	// setActive(false) stops it entering native code, and only then is it safe
	// to unbind the native method that is about to disappear with the module.
	if (env)
	{
		jvalue off;
		off.z = JNI_FALSE;
		call_renderer_setter(env, "setActive", "(Z)V", off);

		jvalue none;
		none.l = nullptr;
		call_renderer_setter(env, "setCurrent", "(Ljava/lang/Object;)V", none);

		if (g_natives_registered && g_renderer_class)
		{
			env->UnregisterNatives(g_renderer_class);
			clear_exception(env);
		}
	}

	g_natives_registered = false;

	if (env && g_packed_buffer)
	{
		env->DeleteGlobalRef(g_packed_buffer);
		g_packed_buffer = nullptr;
		g_packed_buffer_floats = 0;
	}

	if (env && g_text_buffer)
	{
		env->DeleteGlobalRef(g_text_buffer);
		g_text_buffer = nullptr;
		g_text_buffer_floats = 0;
	}

	if (env)
	{
		if (g_mc_class) { env->DeleteGlobalRef(g_mc_class); g_mc_class = nullptr; }
		if (g_world_class) { env->DeleteGlobalRef(g_world_class); g_world_class = nullptr; }
		if (g_entity_class) { env->DeleteGlobalRef(g_entity_class); g_entity_class = nullptr; }
		if (g_box_class) { env->DeleteGlobalRef(g_box_class); g_box_class = nullptr; }
		if (g_list_class) { env->DeleteGlobalRef(g_list_class); g_list_class = nullptr; }
		if (g_living_class) { env->DeleteGlobalRef(g_living_class); g_living_class = nullptr; }
	}

	g_renderer_class = nullptr;   // owned by sdk::java
	g_renderer_render = nullptr;
	g_renderer_render_text = nullptr;
	g_renderer_upload_font = nullptr;

	// The GL texture belongs to the Java side and survives this; re-uploading
	// on the next attach is what keeps it correct if the atlas was rebuilt.
	g_font_uploaded = false;
}
