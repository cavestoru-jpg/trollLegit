#include "esp.h"
#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include "../../utils/logger.h"
#include <imgui_internal.h>
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>
#include <sdk/render/render_view.h>
#include <sdk/java/interp.h>
#include "../world_render/world_render_hook.h"
#include "../killaura/friends.h"
#include "../nametags/vanilla_hide.h"
#include <sdk/classloader.h>
#define _USE_MATH_DEFINES
#include <algorithm>
#include <string>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <vector>
#include <mutex>


namespace
{
	// ------------------------------------------------------------------
	// Per-frame collection, on the render thread.
	//
	// This used to be sampled on the 10 ms worker while the tick fraction was
	// read at draw time. Those two are only combinable if the sampled
	// (lastRenderPos, pos) pair belongs to the same inter-tick interval as the
	// fraction — nothing enforced that, so whenever a tick boundary fell inside
	// the sampling gap the box was lerped between the PREVIOUS interval's
	// endpoints using this interval's fraction, i.e. drawn a full tick of
	// movement behind, snapping forward when the worker caught up. That is a
	// 20 Hz sawtooth, not a constant offset, which is why it read as the box
	// never quite sticking to the player.
	//
	// Reading positions and the fraction in the same instant removes the
	// failure by construction. All ids are resolved once; per frame this is
	// only field reads and a couple of virtual calls.
	// ------------------------------------------------------------------
	struct entity_ids_t
	{
		jclass    mc_class = nullptr;        // GlobalRef
		jfieldID  mc_instance = nullptr;
		jfieldID  mc_world = nullptr;
		jfieldID  mc_player = nullptr;

		jclass    world_class = nullptr;     // GlobalRef
		jfieldID  world_players = nullptr;

		jmethodID list_size = nullptr;
		jmethodID list_get = nullptr;

		jclass    entity_class = nullptr;    // GlobalRef
		jmethodID get_x = nullptr;
		jmethodID get_y = nullptr;
		jmethodID get_z = nullptr;
		jfieldID  box = nullptr;
		jfieldID  last_x = nullptr;
		jfieldID  last_y = nullptr;
		jfieldID  last_z = nullptr;

		jclass    box_class = nullptr;       // GlobalRef
		jfieldID  box_min_x = nullptr, box_min_y = nullptr, box_min_z = nullptr;
		jfieldID  box_max_x = nullptr, box_max_y = nullptr, box_max_z = nullptr;

		jmethodID get_health = nullptr;
		jmethodID get_max_health = nullptr;
		jmethodID get_absorption = nullptr;   // optional, unmapped on this build
		jmethodID is_alive = nullptr;
		jmethodID scoreboard_name = nullptr;

		// Name reading, cached here rather than resolved per player per frame:
		// each find_class is a real reflective ClassLoader.loadClass round-trip,
		// and this runs on the render thread inside the swap hook.
		jmethodID get_name = nullptr;         // Entity.getName() -> Text

		bool resolved = false;
		bool usable = false;
	};

	entity_ids_t g_ids;

	void clear_exc(JNIEnv* env)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
	}

	bool in_world_path_active()
	{
		return globals::esp_world_render_enabled &&
		       enhance::modules::world_render_hook::is_attached();
	}

	jclass hold(JNIEnv* env, const char* sig)
	{
		jclass local = sdk::classloader::find_class(env, sig);
		clear_exc(env);
		if (!local)
			return nullptr;
		jclass global = static_cast<jclass>(env->NewGlobalRef(local));
		env->DeleteLocalRef(local);
		return global;
	}

	bool resolve_ids(JNIEnv* env)
	{
		if (g_ids.resolved)
			return g_ids.usable;

		entity_ids_t ids;

		ids.mc_class = hold(env, sdk::mappings::minecraftclass_sig);
		ids.world_class = hold(env, sdk::mappings::client_world_class_sig);
		ids.entity_class = hold(env, sdk::mappings::entity_class_sig);
		ids.box_class = hold(env, "net/minecraft/class_238");

		// Bail without latching if the game is not far enough along to resolve
		// its own classes; a later frame will succeed. Release what we did take,
		// or every failed attempt leaks four global refs.
		if (!ids.mc_class || !ids.world_class || !ids.entity_class || !ids.box_class)
		{
			if (ids.mc_class) env->DeleteGlobalRef(ids.mc_class);
			if (ids.world_class) env->DeleteGlobalRef(ids.world_class);
			if (ids.entity_class) env->DeleteGlobalRef(ids.entity_class);
			if (ids.box_class) env->DeleteGlobalRef(ids.box_class);
			return false;
		}

		ids.mc_instance = env->GetStaticFieldID(ids.mc_class, sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
		clear_exc(env);
		ids.mc_world = env->GetFieldID(ids.mc_class, sdk::mappings::world_name, sdk::mappings::world_sig);
		clear_exc(env);
		ids.mc_player = env->GetFieldID(ids.mc_class, sdk::mappings::player_name, sdk::mappings::player_sig);
		clear_exc(env);
		ids.world_players = env->GetFieldID(ids.world_class, sdk::mappings::players_field_name, sdk::mappings::players_field_sig);
		clear_exc(env);

		if (jclass list_cls = env->FindClass("java/util/List"))
		{
			ids.list_size = env->GetMethodID(list_cls, "size", "()I");
			clear_exc(env);
			ids.list_get = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;");
			clear_exc(env);
			env->DeleteLocalRef(list_cls);
		}
		clear_exc(env);

		ids.get_x = env->GetMethodID(ids.entity_class, sdk::mappings::entity_get_x_name, sdk::mappings::entity_get_x_sig);
		clear_exc(env);
		ids.get_y = env->GetMethodID(ids.entity_class, sdk::mappings::entity_get_y_name, sdk::mappings::entity_get_y_sig);
		clear_exc(env);
		ids.get_z = env->GetMethodID(ids.entity_class, sdk::mappings::entity_get_z_name, sdk::mappings::entity_get_z_sig);
		clear_exc(env);
		ids.box = env->GetFieldID(ids.entity_class, sdk::mappings::get_bounding_box_name, sdk::mappings::get_bounding_box_sig);
		clear_exc(env);

		sdk::java::last_render_fields(env, ids.last_x, ids.last_y, ids.last_z);

		ids.box_min_x = env->GetFieldID(ids.box_class, sdk::mappings::box_min_x_name, sdk::mappings::box_min_x_sig); clear_exc(env);
		ids.box_min_y = env->GetFieldID(ids.box_class, sdk::mappings::box_min_y_name, sdk::mappings::box_min_y_sig); clear_exc(env);
		ids.box_min_z = env->GetFieldID(ids.box_class, sdk::mappings::box_min_z_name, sdk::mappings::box_min_z_sig); clear_exc(env);
		ids.box_max_x = env->GetFieldID(ids.box_class, sdk::mappings::box_max_x_name, sdk::mappings::box_max_x_sig); clear_exc(env);
		ids.box_max_y = env->GetFieldID(ids.box_class, sdk::mappings::box_max_y_name, sdk::mappings::box_max_y_sig); clear_exc(env);
		ids.box_max_z = env->GetFieldID(ids.box_class, sdk::mappings::box_max_z_name, sdk::mappings::box_max_z_sig); clear_exc(env);

		ids.is_alive = env->GetMethodID(ids.entity_class, sdk::mappings::entity_is_alive_name, sdk::mappings::entity_is_alive_sig);
		clear_exc(env);
		ids.scoreboard_name = env->GetMethodID(ids.entity_class, sdk::mappings::entity_scoreboard_name_name, sdk::mappings::entity_scoreboard_name_sig);
		clear_exc(env);
		ids.get_name = env->GetMethodID(ids.entity_class, sdk::mappings::entity_get_name_name, sdk::mappings::entity_get_name_sig);
		clear_exc(env);

		if (jclass living = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig))
		{
			ids.get_health = env->GetMethodID(living, sdk::mappings::living_entity_get_health_name, sdk::mappings::living_entity_get_health_sig);
			clear_exc(env);
			ids.get_max_health = env->GetMethodID(living, sdk::mappings::living_entity_get_max_health_name, sdk::mappings::living_entity_get_max_health_sig);
			clear_exc(env);
			if (sdk::mappings::living_entity_get_absorption_name[0])
			{
				ids.get_absorption = env->GetMethodID(living, sdk::mappings::living_entity_get_absorption_name, sdk::mappings::living_entity_get_absorption_sig);
				clear_exc(env);
			}
			env->DeleteLocalRef(living);
		}
		clear_exc(env);

		ids.usable = ids.mc_instance && ids.mc_world && ids.world_players &&
		             ids.list_size && ids.list_get && ids.box &&
		             ids.get_x && ids.get_y && ids.get_z &&
		             ids.box_min_x && ids.box_max_z;

		// The latch must live on the struct that is stored, not be written to
		// g_ids beforehand: assigning `ids` over it would put `resolved` back to
		// false and re-run this every single frame, leaking four global refs and
		// making four real loadClass calls each time.
		ids.resolved = true;
		g_ids = ids;

		logger::log(g_ids.usable
			? "[esp] per-frame entity sampling ON"
			: "[esp] per-frame entity sampling OFF - accessors missing");

		return g_ids.usable;
	}

	std::string jstring_to_utf8(JNIEnv* env, jstring js)
	{
		std::string result;
		if (!js)
			return result;

		if (const char* chars = env->GetStringUTFChars(js, nullptr))
		{
			result = chars;
			env->ReleaseStringUTFChars(js, chars);
		}
		return result;
	}

	// Reads a player's display name. The Entity.getName id is cached in
	// resolve_ids; getString is resolved off the CONCRETE runtime class via
	// GetObjectClass rather than the Text interface, which is the pattern that
	// demonstrably works elsewhere in this sdk.
	std::string read_entity_name(JNIEnv* env, jobject entity)
	{
		std::string result;

		if (!g_ids.get_name)
			return result;

		jobject text = env->CallObjectMethod(entity, g_ids.get_name);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return result; }
		if (!text)
			return result;

		jclass text_class = env->GetObjectClass(text);
		if (!text_class)
		{
			env->DeleteLocalRef(text);
			return result;
		}

		jmethodID get_string_mid = env->GetMethodID(text_class, sdk::mappings::text_get_string_name, sdk::mappings::text_get_string_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(text_class);

		if (get_string_mid)
		{
			jstring js = static_cast<jstring>(env->CallObjectMethod(text, get_string_mid));
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (js)
			{
				result = jstring_to_utf8(env, js);
				env->DeleteLocalRef(js);
			}
		}

		env->DeleteLocalRef(text);
		return result;
	}

	// The identity servers key teams and friends on, as opposed to the
	// decorated display name that may carry rank prefixes and colours.
	std::string read_scoreboard_name(JNIEnv* env, jobject entity)
	{
		if (!g_ids.scoreboard_name)
			return {};

		jstring js = static_cast<jstring>(env->CallObjectMethod(entity, g_ids.scoreboard_name));
		if (env->ExceptionCheck()) { env->ExceptionClear(); return {}; }
		if (!js)
			return {};

		std::string result = jstring_to_utf8(env, js);
		env->DeleteLocalRef(js);
		return result;
	}
}

namespace
{
	// Fills `out` with this frame's players, already interpolated. Returns
	// false when the game isn't ready.
	bool collect_players(JNIEnv* env, std::vector<esp_player_data>& out, bool want_labels)
	{
		if (!resolve_ids(env))
			return false;

		const double delta = static_cast<double>(sdk::java::tick_progress());

		jobject mc = env->GetStaticObjectField(g_ids.mc_class, g_ids.mc_instance);
		clear_exc(env);
		if (!mc)
			return false;

		jobject world = env->GetObjectField(mc, g_ids.mc_world);
		clear_exc(env);
		jobject local_player = env->GetObjectField(mc, g_ids.mc_player);
		clear_exc(env);
		env->DeleteLocalRef(mc);

		if (!world)
		{
			if (local_player) env->DeleteLocalRef(local_player);
			return false;
		}

		jobject players = env->GetObjectField(world, g_ids.world_players);
		clear_exc(env);
		env->DeleteLocalRef(world);

		if (!players)
		{
			if (local_player) env->DeleteLocalRef(local_player);
			return false;
		}

		const jint count = env->CallIntMethod(players, g_ids.list_size);
		clear_exc(env);
		out.reserve(static_cast<size_t>(count > 0 ? count : 0));

		for (jint i = 0; i < count; ++i)
		{
			jobject entity = env->CallObjectMethod(players, g_ids.list_get, i);
			clear_exc(env);
			if (!entity)
				continue;

			if (local_player && env->IsSameObject(entity, local_player))
			{
				env->DeleteLocalRef(entity);
				continue;
			}

			// Corpses keep sitting in the players list for the whole death
			// animation, and some servers leave ghost entries behind.
			if (g_ids.is_alive)
			{
				const jboolean alive = env->CallBooleanMethod(entity, g_ids.is_alive);
				if (env->ExceptionCheck()) env->ExceptionClear();
				else if (!alive)
				{
					env->DeleteLocalRef(entity);
					continue;
				}
			}

			jobject box = env->GetObjectField(entity, g_ids.box);
			clear_exc(env);
			if (!box)
			{
				env->DeleteLocalRef(entity);
				continue;
			}

			esp_player_data data;
			data.x = env->CallDoubleMethod(entity, g_ids.get_x);
			data.y = env->CallDoubleMethod(entity, g_ids.get_y);
			data.z = env->CallDoubleMethod(entity, g_ids.get_z);
			clear_exc(env);

			data.min_x = env->GetDoubleField(box, g_ids.box_min_x);
			data.min_y = env->GetDoubleField(box, g_ids.box_min_y);
			data.min_z = env->GetDoubleField(box, g_ids.box_min_z);
			data.max_x = env->GetDoubleField(box, g_ids.box_max_x);
			data.max_y = env->GetDoubleField(box, g_ids.box_max_y);
			data.max_z = env->GetDoubleField(box, g_ids.box_max_z);
			env->DeleteLocalRef(box);

			// Shift the box by the same amount the game shifts the model. Read
			// in the same instant as `delta`, so the pair and the fraction
			// always describe the same inter-tick interval.
			data.has_last = false;
			if (g_ids.last_x)
			{
				const double lx = env->GetDoubleField(entity, g_ids.last_x);
				const double ly = env->GetDoubleField(entity, g_ids.last_y);
				const double lz = env->GetDoubleField(entity, g_ids.last_z);

				const double off_x = (lx + (data.x - lx) * delta) - data.x;
				const double off_y = (ly + (data.y - ly) * delta) - data.y;
				const double off_z = (lz + (data.z - lz) * delta) - data.z;

				// A teleport, or a mis-identified field, would otherwise fling
				// the box across the map. Drop the shift instead.
				if (std::abs(off_x) <= sdk::java::k_max_interp_offset &&
				    std::abs(off_y) <= sdk::java::k_max_interp_offset &&
				    std::abs(off_z) <= sdk::java::k_max_interp_offset)
				{
					data.min_x += off_x; data.max_x += off_x;
					data.min_y += off_y; data.max_y += off_y;
					data.min_z += off_z; data.max_z += off_z;
					data.x += off_x; data.y += off_y; data.z += off_z;
					data.has_last = true;
				}
			}

			// Health feeds the name tag too, so it cannot be gated on the ESP
			// box's own health-bar checkbox: doing that made every tag read a
			// constant green "20.0" whenever only name tags were enabled.
			data.health = 20.0f;
			data.max_health = 20.0f;
			const bool want_health = globals::esp_health_bar ||
			                         (globals::nametags_enabled && globals::nametags_show_health);
			if (want_health && g_ids.get_health && g_ids.get_max_health)
			{
				data.health = env->CallFloatMethod(entity, g_ids.get_health);
				if (env->ExceptionCheck()) { env->ExceptionClear(); data.health = 20.0f; }
				data.max_health = env->CallFloatMethod(entity, g_ids.get_max_health);
				if (env->ExceptionCheck()) { env->ExceptionClear(); data.max_health = 20.0f; }

				// The reference counts absorption into both sides, so a golden
				// apple shows as 30/30 rather than a green 20/20.
				if (g_ids.get_absorption)
				{
					const float absorption = env->CallFloatMethod(entity, g_ids.get_absorption);
					if (env->ExceptionCheck()) env->ExceptionClear();
					else if (absorption > 0.0f)
					{
						data.health += absorption;
						data.max_health += absorption;
					}
				}
			}

			if (want_labels)
			{
				data.name = read_entity_name(env, entity);
				data.scoreboard_name = read_scoreboard_name(env, entity);
			}

			out.push_back(std::move(data));
			env->DeleteLocalRef(entity);
		}

		env->DeleteLocalRef(players);
		if (local_player) env->DeleteLocalRef(local_player);
		return true;
	}
}

// Geometry is gathered per frame on the render thread now (see
// collect_players). This remains only so the module keeps its slot in the
// worker loop without doing redundant JNI work every 10 ms.
void enhance::modules::esp::run()
{
}


namespace
{
	ImU32 with_alpha(const ImVec4& c, float a)
	{
		return IM_COL32(
			static_cast<int>(c.x * 255.0f),
			static_cast<int>(c.y * 255.0f),
			static_cast<int>(c.z * 255.0f),
			static_cast<int>(ImClamp(c.w * a, 0.0f, 1.0f) * 255.0f));
	}

	// Draws the 12 projected edges of the AABB. Each edge is clipped against
	// the near plane on its own, so a box the camera is standing inside still
	// renders the parts that are actually in front.
	void draw_box_3d(ImDrawList* dl, const double mn[3], const double mx[3], ImU32 col)
	{
		float corners[8][3];
		sdk::render::box_corners(mn, mx, corners);

		for (const auto& e : sdk::render::box_edges)
		{
			float a[2], b[2];
			if (!sdk::render::project_segment(corners[e[0]], corners[e[1]], a, b))
				continue;

			dl->AddLine(ImVec2(a[0], a[1]), ImVec2(b[0], b[1]), col, 1.0f);
		}
	}

	// ------------------------------------------------------------------
	// Name tags — ported design.
	//
	// The reference supplies an anchor and a data model, not a layout: it has
	// no draw calls at all, and the icon x/y/size it echoes back are values
	// its caller had already chosen. So every formula below (anchor offset,
	// margin cull, scale falloff, health ramp and text, sanitisation, far-to-
	// near ordering) is the reference's; the arrangement of the line is ours.
	// ------------------------------------------------------------------
	constexpr float k_screen_margin = 96.0f;   // cull slack, never a clamp
	constexpr float k_tag_height_offset = 0.55f;

	// Length in bytes of the UTF-8 sequence starting at `lead`. Everything here
	// walks whole codepoints: skipping a fixed single byte after a format
	// marker would decapitate a multi-byte character and push its orphaned
	// continuation bytes into the output as mojibake.
	int utf8_len(unsigned char lead)
	{
		if (lead < 0x80) return 1;
		if ((lead & 0xE0) == 0xC0) return 2;
		if ((lead & 0xF0) == 0xE0) return 3;
		if ((lead & 0xF8) == 0xF0) return 4;
		return 1;
	}

	uint32_t utf8_decode(const std::string& s, size_t i, int len)
	{
		const unsigned char lead = static_cast<unsigned char>(s[i]);
		if (len == 1) return lead;

		uint32_t cp = lead & (0xFF >> (len + 1));
		for (int k = 1; k < len && i + k < s.size(); ++k)
			cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
		return cp;
	}

	bool is_unicode_space(uint32_t cp)
	{
		return cp == ' ' || cp == '\t' || cp == 0x00A0 || cp == 0x3000 ||
		       (cp >= 0x2000 && cp <= 0x200A);
	}

	bool is_format_code(uint32_t cp)
	{
		const uint32_t c = (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		       (c >= 'k' && c <= 'o') || c == 'r';
	}

	std::string trim_unicode(const std::string& s)
	{
		size_t b = 0;
		while (b < s.size())
		{
			const int n = utf8_len(static_cast<unsigned char>(s[b]));
			if (!is_unicode_space(utf8_decode(s, b, n))) break;
			b += n;
		}

		size_t e = s.size();
		while (e > b)
		{
			size_t p = e - 1;
			while (p > b && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) --p;
			const int n = utf8_len(static_cast<unsigned char>(s[p]));
			if (!is_unicode_space(utf8_decode(s, p, n))) break;
			e = p;
		}

		return s.substr(b, e - b);
	}

	// Names go through this: the marker and the character after it are dropped
	// ONLY when that character really is a format code, so a nickname with a
	// bare '&' in it survives intact.
	std::string strip_format_codes(const std::string& in)
	{
		std::string out;
		out.reserve(in.size());

		for (size_t i = 0; i < in.size();)
		{
			const int n = utf8_len(static_cast<unsigned char>(in[i]));
			const uint32_t cp = utf8_decode(in, i, n);

			if (cp == '&' || cp == 0x00A7)
			{
				const size_t j = i + n;
				if (j < in.size())
				{
					const int n2 = utf8_len(static_cast<unsigned char>(in[j]));
					if (is_format_code(utf8_decode(in, j, n2)))
					{
						i = j + n2;
						continue;
					}
				}
			}

			out.append(in, i, n);
			i += n;
		}

		return trim_unicode(out);
	}

	// The aggressive pass, used on the team prefix: drops every marker with the
	// character after it unconditionally, plus control characters and the
	// Private Use Area, which is where servers hide their custom rank glyphs.
	std::string sanitize_tag_text(const std::string& in)
	{
		std::string out;
		out.reserve(in.size());

		bool skip_next = false;
		for (size_t i = 0; i < in.size();)
		{
			const int n = utf8_len(static_cast<unsigned char>(in[i]));
			const uint32_t cp = utf8_decode(in, i, n);
			i += n;

			if (skip_next)
			{
				skip_next = false;
				continue;
			}

			if (cp == 0x00A7 || cp == '&')
			{
				skip_next = true;
				continue;
			}
			if (cp < 0x20 || cp == 0x7F)
				continue;
			if (cp >= 0xE000 && cp <= 0xF8FF)
				continue;

			out.append(in, i - n, n);
		}

		return trim_unicode(out);
	}

	// Stepped ramp, exactly as the reference: not a gradient.
	ImU32 health_ramp(float ratio, float alpha)
	{
		int r, g, b;
		if (ratio > 0.75f)      { r = 0;   g = 255; b = 0; }
		else if (ratio > 0.5f)  { r = 255; g = 255; b = 0; }
		else if (ratio > 0.25f) { r = 255; g = 170; b = 0; }
		else                    { r = 255; g = 85;  b = 85; }
		return IM_COL32(r, g, b, static_cast<int>(ImClamp(alpha, 0.0f, 1.0f) * 255.0f));
	}

	void format_health(char* out, size_t n, int mode, float health, float max_health)
	{
		switch (mode)
		{
			case 1:
			{
				const int hearts = static_cast<int>(std::lround(health / 2.0f));
				const int max_hearts = static_cast<int>(std::lround(ImMax(max_health, 1.0f) / 2.0f));
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

	float tag_scale(float distance)
	{
		const float size = ImClamp(globals::nametags_size / 0.35f, 0.2f, 4.0f);
		if (globals::nametags_static_size)
			return size;

		const float factor = 1.15f - ImClamp(distance / ImMax(globals::nametags_range, 1.0f), 0.0f, 1.0f) * 0.45f;
		return ImClamp(size * factor, 0.45f, 3.0f);
	}

	// Deliberately delegates instead of walking globals::killaura_friends here:
	// that vector is mutated under a mutex in friends.cpp, so reading it raw
	// from the render thread races a reallocation, and the local comparison was
	// case-sensitive while the shared one is not.
	bool is_friend(const std::string& name)
	{
		if (name.empty())
			return false;
		return enhance::modules::killaura::friends_list::contains(name);
	}

	void draw_nametags(ImDrawList* dl, const std::vector<esp_player_data>& players,
	                   const sdk::render::view_t& v)
	{
		if (!globals::nametags_enabled)
			return;

		// The in-world renderer draws them instead — gated on it actually being
		// attached, so a ticked box with a failed attach still gets overlay
		// tags rather than none at all.
		if (globals::nametags_in_world && enhance::modules::world_render_hook::is_attached())
			return;

		const ImGuiIO& io = ImGui::GetIO();
		ImFont* font = ImGui::GetFont();
		if (!font)
			return;

		// Far to near, so nearer tags land on top of more distant ones.
		std::vector<const esp_player_data*> ordered;
		ordered.reserve(players.size());
		for (const auto& p : players)
			ordered.push_back(&p);
		std::sort(ordered.begin(), ordered.end(),
			[&](const esp_player_data* a, const esp_player_data* b)
			{
				const double da = (a->x - v.cam_x) * (a->x - v.cam_x) + (a->y - v.cam_y) * (a->y - v.cam_y) + (a->z - v.cam_z) * (a->z - v.cam_z);
				const double db = (b->x - v.cam_x) * (b->x - v.cam_x) + (b->y - v.cam_y) * (b->y - v.cam_y) + (b->z - v.cam_z) * (b->z - v.cam_z);
				return da > db;
			});

		for (const esp_player_data* p : ordered)
		{
			const double dx = p->x - v.cam_x;
			const double dy = p->y - v.cam_y;
			const double dz = p->z - v.cam_z;
			const float distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
			if (distance > globals::nametags_range)
				continue;

			// The name keeps its text; only real format codes come off. Friend
			// matching uses the undecorated scoreboard identity, because a
			// display name carrying a rank prefix never matches a saved friend.
			const std::string name = strip_format_codes(p->name);
			const bool friendly = is_friend(p->scoreboard_name.empty() ? name : p->scoreboard_name);
			if (friendly && !globals::nametags_show_friends)
				continue;

			// Anchor sits above the top of the bounding box, which is the same
			// point as entity_y + bbHeight without needing a getBbHeight mapping.
			float sx, sy;
			if (!sdk::render::world_to_screen(static_cast<float>(p->x),
			                                  static_cast<float>(p->max_y + k_tag_height_offset),
			                                  static_cast<float>(p->z), sx, sy))
				continue;

			// Cull with slack, never clamp: a tag whose anchor has just left the
			// screen may still overlap it.
			if (sx < -k_screen_margin || sy < -k_screen_margin ||
			    sx > io.DisplaySize.x + k_screen_margin || sy > io.DisplaySize.y + k_screen_margin)
				continue;

			const float scale = tag_scale(distance);
			const float px = ImMax(6.0f, font->FontSize * scale);
			const float alpha = ImClamp(globals::nametags_alpha, 0.0f, 1.0f);

			char health_buf[32] = { 0 };
			char dist_buf[24] = { 0 };
			if (globals::nametags_show_health && p->max_health > 0.0f)
				format_health(health_buf, sizeof(health_buf), globals::nametags_health_mode, p->health, p->max_health);
			if (globals::nametags_show_distance)
				snprintf(dist_buf, sizeof(dist_buf), "%.0fm", distance);

			const char* name_str = name.empty() ? "Player" : name.c_str();

			const float gap = px * 0.35f;
			const ImVec2 name_size = font->CalcTextSizeA(px, FLT_MAX, 0.0f, name_str);
			const ImVec2 health_size = health_buf[0] ? font->CalcTextSizeA(px, FLT_MAX, 0.0f, health_buf) : ImVec2(0, 0);
			const ImVec2 dist_size = dist_buf[0] ? font->CalcTextSizeA(px, FLT_MAX, 0.0f, dist_buf) : ImVec2(0, 0);

			float total = name_size.x;
			if (health_size.x > 0.0f) total += gap + health_size.x;
			if (dist_size.x > 0.0f) total += gap + dist_size.x;

			// Panel geometry. The backdrop is what actually separates a client
			// tag from Minecraft's own floating name — bare white text with a
			// drop shadow is exactly what vanilla draws, which is why the port
			// looked unchanged.
			const float pad_x = ImMax(3.0f, px * 0.40f);
			const float pad_y = ImMax(2.0f, px * 0.22f);
			const float panel_h = px + pad_y * 2.0f;
			const float panel_w = total + pad_x * 2.0f;

			// Row of equipment icons sits above the panel. The icons themselves
			// need Minecraft's GUI item atlas, which is unported, so the row is
			// currently empty and contributes no height — the layout below is
			// already written around it so adding them shifts nothing else.
			const float icon_size = px * 1.15f;
			const int icon_count = 0;
			const float icon_row_h = icon_count > 0 ? icon_size + pad_y : 0.0f;

			// Anchor is the BOTTOM of the panel, so the tag grows upward and the
			// gap to the player's head stays constant as the scale changes.
			const float panel_bottom = sy;
			const float panel_top = panel_bottom - panel_h;
			const float panel_left = sx - panel_w * 0.5f;

			dl->AddRectFilled(ImVec2(panel_left, panel_top),
			                  ImVec2(panel_left + panel_w, panel_bottom),
			                  IM_COL32(12, 13, 18, static_cast<int>(alpha * 205.0f)),
			                  ImMax(2.0f, px * 0.25f));

			if (icon_count > 0)
			{
				const float row_w = icon_count * icon_size + (icon_count - 1) * (pad_x * 0.5f);
				float ix = sx - row_w * 0.5f;
				const float iy = panel_top - icon_row_h;
				(void)ix; (void)iy;   // icons draw here once the atlas is mapped
			}

			float cursor = panel_left + pad_x;
			const float top = panel_top + pad_y;

			const ImVec4& fc = globals::nametags_friend_color;
			const ImU32 name_col = friendly
				? IM_COL32(static_cast<int>(fc.x * 255.0f), static_cast<int>(fc.y * 255.0f), static_cast<int>(fc.z * 255.0f), static_cast<int>(ImClamp(fc.w * alpha, 0.0f, 1.0f) * 255.0f))
				: IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f));

			// No drop shadow any more: the panel provides the contrast, and a
			// shadow on top of it is what made this read as the vanilla tag.
			dl->AddText(font, px, ImVec2(cursor, top), name_col, name_str);
			cursor += name_size.x;

			if (health_size.x > 0.0f)
			{
				cursor += gap;
				const float ratio = ImClamp(p->health / p->max_health, 0.0f, 1.0f);
				dl->AddText(font, px, ImVec2(cursor, top), health_ramp(ratio, alpha), health_buf);
				cursor += health_size.x;
			}

			if (dist_size.x > 0.0f)
			{
				cursor += gap;
				dl->AddText(font, px, ImVec2(cursor, top), IM_COL32(180, 180, 180, static_cast<int>(alpha * 255.0f)), dist_buf);
			}
		}
	}

	void draw_box_corners(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col)
	{
		const float w = x1 - x0;
		const float h = y1 - y0;
		const float len_x = ImMax(2.0f, w * 0.25f);
		const float len_y = ImMax(2.0f, h * 0.25f);

		const ImVec2 pts[8][2] = {
			{ { x0, y0 }, { x0 + len_x, y0 } }, { { x0, y0 }, { x0, y0 + len_y } },
			{ { x1, y0 }, { x1 - len_x, y0 } }, { { x1, y0 }, { x1, y0 + len_y } },
			{ { x0, y1 }, { x0 + len_x, y1 } }, { { x0, y1 }, { x0, y1 - len_y } },
			{ { x1, y1 }, { x1 - len_x, y1 } }, { { x1, y1 }, { x1, y1 - len_y } },
		};

		for (const auto& seg : pts)
		{
			dl->AddLine(seg[0], seg[1], col, 1.6f);
		}
	}
}

void enhance::modules::esp::draw_boxes()
{
	if (!globals::box_enabled && !globals::esp_health_bar && !globals::nametags_enabled) return;
	if (!GUI::get_is_init()) return;

	// The view is published once per frame by GUI::draw from the render
	// thread, so everything below is already in sync with the frame the game
	// just produced — no stale worker-thread camera snapshot.
	const sdk::render::view_t& v = sdk::render::view();
	if (!v.valid) return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list) return;

	JNIEnv* env = sdk::render::current_thread_env();
	if (!env) return;

	// Sampled here, this frame, in the same instant as the tick fraction the
	// interpolation uses — see collect_players for why that matters. One walk
	// feeds both the boxes and the name tags.
	std::vector<esp_player_data> players_copy;
	if (!collect_players(env, players_copy, globals::nametags_enabled))
		return;
	if (players_copy.empty()) return;

	draw_nametags(draw_list, players_copy, v);

	// Applies and un-applies itself as the toggle changes, so the scoreboard is
	// left exactly as it was found once the feature goes off.
	enhance::modules::vanilla_nametags::tick(globals::nametags_enabled &&
	                                         globals::nametags_hide_vanilla);

	if (!globals::box_enabled && !globals::esp_health_bar)
		return;

	for (const auto& player : players_copy)
	{
		// Already interpolated by collect_players.
		const double mn[3] = { player.min_x, player.min_y, player.min_z };
		const double mx[3] = { player.max_x, player.max_y, player.max_z };

		float x0, y0, x1, y1;
		if (!sdk::render::project_box(mn, mx, x0, y0, x1, y1))
			continue;

		const double dx = player.x - v.cam_x;
		const double dy = player.y - v.cam_y;
		const double dz = player.z - v.cam_z;
		const float distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));

		float alpha = 1.0f;
		if (globals::esp_distance_fade)
		{
			const float start = globals::esp_fade_start;
			const float end = ImMax(start + 1.0f, globals::esp_fade_end);
			alpha = 1.0f - ImClamp((distance - start) / (end - start), 0.0f, 1.0f);
			if (alpha <= 0.01f)
				continue;
		}

		const ImU32 col = with_alpha(globals::esp_color, alpha);
		const ImU32 shadow = IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.0f));

		// The in-world 3D path draws its own geometry from inside the game's
		// render pass; this overlay box would just double it up. Keyed on the
		// hook actually being attached, not on the toggle: if the hook refused
		// to install, suppressing the overlay here would leave the user with a
		// ticked box and no ESP at all.
		if (globals::box_enabled && !in_world_path_active())
		{
			if (globals::esp_box_filled)
				draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), with_alpha(globals::esp_color, alpha * 0.18f));

			switch (globals::esp_box_style)
			{
				case 1:
					draw_box_corners(draw_list, x0, y0, x1, y1, col);
					break;
				case 2:
					draw_box_3d(draw_list, mn, mx, col);
					break;
				default:
					draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col, 0.0f, 0, 1.0f);
					break;
			}
		}

		// Tracers and labels stay screen-space in both modes — they are HUD
		// elements, not world geometry.
		if (globals::box_enabled && globals::esp_tracers)
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 from(io.DisplaySize.x * 0.5f, io.DisplaySize.y);
			draw_list->AddLine(from, ImVec2((x0 + x1) * 0.5f, y1), col, 1.0f);
		}

		// The old name/distance label is gone — draw_nametags replaces it.

		if (globals::esp_health_bar && player.max_health > 0)
		{
			const float health_percent = ImClamp(player.health / player.max_health, 0.0f, 1.0f);

			const float bar_width = 3.0f;
			const float bar_x = x0 - bar_width - 3.0f;
			const float filled_height = (y1 - y0) * health_percent;

			ImU32 health_color;
			if (health_percent > 0.5f)
			{
				const float t = (health_percent - 0.5f) * 2.0f;
				health_color = IM_COL32(static_cast<int>(255 * (1.0f - t)), 255, 0, static_cast<int>(alpha * 255.0f));
			}
			else
			{
				const float t = health_percent * 2.0f;
				health_color = IM_COL32(255, static_cast<int>(255 * t), 0, static_cast<int>(alpha * 255.0f));
			}

			draw_list->AddRectFilled(ImVec2(bar_x - 1, y0 - 1), ImVec2(bar_x + bar_width + 1, y1 + 1), shadow);
			draw_list->AddRectFilled(ImVec2(bar_x, y0), ImVec2(bar_x + bar_width, y1), IM_COL32(50, 50, 50, static_cast<int>(alpha * 200.0f)));
			draw_list->AddRectFilled(ImVec2(bar_x, y1 - filled_height), ImVec2(bar_x + bar_width, y1), health_color);
		}
	}
}
