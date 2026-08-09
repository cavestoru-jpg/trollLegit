#include "vanilla_hide.h"

#include "../../enhance.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"

#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/render/render_view.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	// Name of the throwaway team teamless players are parked in. Prefixed so it
	// is recognisable in an F3 dump and cannot collide with a server team.
	constexpr const char* k_temp_team = "enh_nt_hide";

	struct ids_t
	{
		jclass    mc_class = nullptr;          // GlobalRef
		jfieldID  mc_instance = nullptr;
		jfieldID  mc_world = nullptr;
		jfieldID  mc_player = nullptr;

		jclass    world_class = nullptr;       // GlobalRef
		jfieldID  world_players = nullptr;
		jmethodID world_get_scoreboard = nullptr;

		jmethodID list_size = nullptr;
		jmethodID list_get = nullptr;

		jmethodID entity_scoreboard_name = nullptr;

		jmethodID sb_get_holder_team = nullptr;
		jmethodID sb_get_team = nullptr;
		jmethodID sb_add_team = nullptr;
		jmethodID sb_add_holder_to_team = nullptr;
		jmethodID sb_clear_team = nullptr;

		jmethodID team_get_name = nullptr;
		jmethodID team_get_visibility = nullptr;
		jmethodID team_set_visibility = nullptr;

		jobject   visibility_never = nullptr;  // GlobalRef

		bool resolved = false;
		bool usable = false;
	};

	ids_t g;

	// Teams whose visibility we overwrote, with the value to put back.
	std::unordered_map<std::string, jobject> g_saved;      // team name -> GlobalRef of original rule
	// Players we parked in the throwaway team.
	std::unordered_set<std::string> g_parked;
	bool g_active = false;

	void clear_exc(JNIEnv* env)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
	}

	std::string jstr(JNIEnv* env, jstring js)
	{
		std::string out;
		if (!js)
			return out;
		if (const char* c = env->GetStringUTFChars(js, nullptr))
		{
			out = c;
			env->ReleaseStringUTFChars(js, c);
		}
		return out;
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

	bool resolve(JNIEnv* env)
	{
		if (g.resolved)
			return g.usable;

		ids_t ids;

		ids.mc_class = hold(env, sdk::mappings::minecraftclass_sig);
		ids.world_class = hold(env, sdk::mappings::client_world_class_sig);
		jclass world_base = sdk::classloader::find_class(env, sdk::mappings::world_class_sig);
		clear_exc(env);
		jclass sb_class = sdk::classloader::find_class(env, sdk::mappings::scoreboard_class_sig);
		clear_exc(env);
		jclass team_class = sdk::classloader::find_class(env, sdk::mappings::team_class_sig);
		clear_exc(env);
		jclass abstract_team = sdk::classloader::find_class(env, sdk::mappings::abstract_team_class_sig);
		clear_exc(env);
		jclass entity_class = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
		clear_exc(env);
		jclass vis_class = sdk::classloader::find_class(env, sdk::mappings::visibility_class_sig);
		clear_exc(env);

		// Not ready yet — try again next frame rather than latching a failure.
		if (!ids.mc_class || !ids.world_class || !world_base || !sb_class ||
		    !team_class || !abstract_team || !entity_class || !vis_class)
		{
			if (ids.mc_class) env->DeleteGlobalRef(ids.mc_class);
			if (ids.world_class) env->DeleteGlobalRef(ids.world_class);
			if (world_base) env->DeleteLocalRef(world_base);
			if (sb_class) env->DeleteLocalRef(sb_class);
			if (team_class) env->DeleteLocalRef(team_class);
			if (abstract_team) env->DeleteLocalRef(abstract_team);
			if (entity_class) env->DeleteLocalRef(entity_class);
			if (vis_class) env->DeleteLocalRef(vis_class);
			return false;
		}

		ids.mc_instance = env->GetStaticFieldID(ids.mc_class, sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig); clear_exc(env);
		ids.mc_world = env->GetFieldID(ids.mc_class, sdk::mappings::world_name, sdk::mappings::world_sig); clear_exc(env);
		ids.mc_player = env->GetFieldID(ids.mc_class, sdk::mappings::player_name, sdk::mappings::player_sig); clear_exc(env);
		ids.world_players = env->GetFieldID(ids.world_class, sdk::mappings::players_field_name, sdk::mappings::players_field_sig); clear_exc(env);
		ids.world_get_scoreboard = env->GetMethodID(world_base, sdk::mappings::world_get_scoreboard_name, sdk::mappings::world_get_scoreboard_sig); clear_exc(env);

		if (jclass list_cls = env->FindClass("java/util/List"))
		{
			ids.list_size = env->GetMethodID(list_cls, "size", "()I"); clear_exc(env);
			ids.list_get = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;"); clear_exc(env);
			env->DeleteLocalRef(list_cls);
		}
		clear_exc(env);

		ids.entity_scoreboard_name = env->GetMethodID(entity_class, sdk::mappings::entity_scoreboard_name_name, sdk::mappings::entity_scoreboard_name_sig); clear_exc(env);

		ids.sb_get_holder_team = env->GetMethodID(sb_class, sdk::mappings::scoreboard_get_holder_team_name, sdk::mappings::scoreboard_get_holder_team_sig); clear_exc(env);
		ids.sb_get_team = env->GetMethodID(sb_class, sdk::mappings::scoreboard_get_team_name, sdk::mappings::scoreboard_get_team_sig); clear_exc(env);
		ids.sb_add_team = env->GetMethodID(sb_class, sdk::mappings::scoreboard_add_team_name, sdk::mappings::scoreboard_add_team_sig); clear_exc(env);
		ids.sb_add_holder_to_team = env->GetMethodID(sb_class, sdk::mappings::scoreboard_add_holder_to_team_name, sdk::mappings::scoreboard_add_holder_to_team_sig); clear_exc(env);
		ids.sb_clear_team = env->GetMethodID(sb_class, sdk::mappings::scoreboard_clear_team_name, sdk::mappings::scoreboard_clear_team_sig); clear_exc(env);

		// getName / getNameTagVisibilityRule live on AbstractTeam, the setter on Team.
		ids.team_get_name = env->GetMethodID(abstract_team, sdk::mappings::team_get_name_name, sdk::mappings::team_get_name_sig); clear_exc(env);
		ids.team_get_visibility = env->GetMethodID(abstract_team, sdk::mappings::team_get_visibility_name, sdk::mappings::team_get_visibility_sig); clear_exc(env);
		ids.team_set_visibility = env->GetMethodID(team_class, sdk::mappings::team_set_visibility_name, sdk::mappings::team_set_visibility_sig); clear_exc(env);

		if (jfieldID never_fid = env->GetStaticFieldID(vis_class, sdk::mappings::visibility_never_name, sdk::mappings::visibility_sig))
		{
			if (jobject never = env->GetStaticObjectField(vis_class, never_fid))
			{
				ids.visibility_never = env->NewGlobalRef(never);
				env->DeleteLocalRef(never);
			}
		}
		clear_exc(env);

		env->DeleteLocalRef(world_base);
		env->DeleteLocalRef(sb_class);
		env->DeleteLocalRef(team_class);
		env->DeleteLocalRef(abstract_team);
		env->DeleteLocalRef(entity_class);
		env->DeleteLocalRef(vis_class);

		ids.usable = ids.mc_instance && ids.mc_world && ids.world_players &&
		             ids.world_get_scoreboard && ids.list_size && ids.list_get &&
		             ids.entity_scoreboard_name && ids.sb_get_holder_team &&
		             ids.sb_add_team && ids.sb_add_holder_to_team &&
		             ids.sb_clear_team && ids.team_get_name &&
		             ids.team_get_visibility && ids.team_set_visibility &&
		             ids.visibility_never;

		ids.resolved = true;
		g = ids;

		logger::log(g.usable
			? "[nametags] vanilla hiding available"
			: "[nametags] vanilla hiding unavailable - scoreboard accessors missing");

		return g.usable;
	}

	jobject get_scoreboard(JNIEnv* env, jobject world)
	{
		jobject sb = env->CallObjectMethod(world, g.world_get_scoreboard);
		clear_exc(env);
		return sb;
	}

	void set_visibility(JNIEnv* env, jobject team, jobject rule)
	{
		env->CallVoidMethod(team, g.team_set_visibility, rule);
		clear_exc(env);
	}
}

void enhance::modules::vanilla_nametags::tick(bool want_hidden)
{
	JNIEnv* env = sdk::render::current_thread_env();
	if (!env)
		return;

	if (!want_hidden)
	{
		if (g_active)
			restore();
		return;
	}

	if (!resolve(env))
		return;

	jobject mc = env->GetStaticObjectField(g.mc_class, g.mc_instance);
	clear_exc(env);
	if (!mc)
		return;

	jobject world = env->GetObjectField(mc, g.mc_world);
	clear_exc(env);
	jobject local_player = env->GetObjectField(mc, g.mc_player);
	clear_exc(env);
	env->DeleteLocalRef(mc);

	if (!world)
	{
		if (local_player) env->DeleteLocalRef(local_player);
		return;
	}

	jobject scoreboard = get_scoreboard(env, world);
	jobject players = env->GetObjectField(world, g.world_players);
	clear_exc(env);
	env->DeleteLocalRef(world);

	if (!scoreboard || !players)
	{
		if (scoreboard) env->DeleteLocalRef(scoreboard);
		if (players) env->DeleteLocalRef(players);
		if (local_player) env->DeleteLocalRef(local_player);
		return;
	}

	g_active = true;

	// Throwaway team, created lazily and only once.
	jobject temp_team = nullptr;
	auto ensure_temp_team = [&]() -> jobject
	{
		if (temp_team)
			return temp_team;

		jstring jname = env->NewStringUTF(k_temp_team);
		if (!jname)
			return nullptr;

		temp_team = env->CallObjectMethod(scoreboard, g.sb_get_team, jname);
		clear_exc(env);
		if (!temp_team)
		{
			temp_team = env->CallObjectMethod(scoreboard, g.sb_add_team, jname);
			clear_exc(env);
		}
		env->DeleteLocalRef(jname);

		if (temp_team)
			set_visibility(env, temp_team, g.visibility_never);

		return temp_team;
	};

	const jint count = env->CallIntMethod(players, g.list_size);
	clear_exc(env);

	for (jint i = 0; i < count; ++i)
	{
		jobject entity = env->CallObjectMethod(players, g.list_get, i);
		clear_exc(env);
		if (!entity)
			continue;

		if (local_player && env->IsSameObject(entity, local_player))
		{
			env->DeleteLocalRef(entity);
			continue;
		}

		jstring jholder = static_cast<jstring>(env->CallObjectMethod(entity, g.entity_scoreboard_name));
		clear_exc(env);
		env->DeleteLocalRef(entity);
		if (!jholder)
			continue;

		const std::string holder = jstr(env, jholder);

		jobject team = env->CallObjectMethod(scoreboard, g.sb_get_holder_team, jholder);
		clear_exc(env);

		if (team)
		{
			jstring jteam_name = static_cast<jstring>(env->CallObjectMethod(team, g.team_get_name));
			clear_exc(env);
			const std::string team_name = jstr(env, jteam_name);
			if (jteam_name) env->DeleteLocalRef(jteam_name);

			// Never touch our own throwaway team, and only snapshot the
			// original rule the first time we see a given team.
			if (team_name != k_temp_team && g_saved.find(team_name) == g_saved.end())
			{
				jobject rule = env->CallObjectMethod(team, g.team_get_visibility);
				clear_exc(env);
				if (rule)
				{
					g_saved[team_name] = env->NewGlobalRef(rule);
					env->DeleteLocalRef(rule);
				}
				set_visibility(env, team, g.visibility_never);
			}

			env->DeleteLocalRef(team);
		}
		else if (jobject tt = ensure_temp_team())
		{
			if (g_parked.find(holder) == g_parked.end())
			{
				const jboolean added = env->CallBooleanMethod(scoreboard, g.sb_add_holder_to_team, jholder, tt);
				clear_exc(env);
				if (added)
					g_parked.insert(holder);
			}
		}

		env->DeleteLocalRef(jholder);
	}

	if (temp_team) env->DeleteLocalRef(temp_team);
	env->DeleteLocalRef(players);
	env->DeleteLocalRef(scoreboard);
	if (local_player) env->DeleteLocalRef(local_player);
}

void enhance::modules::vanilla_nametags::restore()
{
	if (!g_active && g_saved.empty() && g_parked.empty())
		return;

	JNIEnv* env = sdk::render::current_thread_env();
	if (!env || !g.usable)
	{
		// Nothing we can do through JNI; drop the bookkeeping so we do not
		// keep global refs alive forever.
		g_saved.clear();
		g_parked.clear();
		g_active = false;
		return;
	}

	jobject mc = env->GetStaticObjectField(g.mc_class, g.mc_instance);
	clear_exc(env);
	jobject world = mc ? env->GetObjectField(mc, g.mc_world) : nullptr;
	clear_exc(env);
	if (mc) env->DeleteLocalRef(mc);

	jobject scoreboard = world ? get_scoreboard(env, world) : nullptr;
	if (world) env->DeleteLocalRef(world);

	if (scoreboard)
	{
		for (auto& entry : g_saved)
		{
			jstring jname = env->NewStringUTF(entry.first.c_str());
			if (!jname)
				continue;

			if (jobject team = env->CallObjectMethod(scoreboard, g.sb_get_team, jname))
			{
				set_visibility(env, team, entry.second);
				env->DeleteLocalRef(team);
			}
			clear_exc(env);
			env->DeleteLocalRef(jname);
		}

		for (const auto& holder : g_parked)
		{
			jstring jname = env->NewStringUTF(holder.c_str());
			if (!jname)
				continue;
			env->CallBooleanMethod(scoreboard, g.sb_clear_team, jname);
			clear_exc(env);
			env->DeleteLocalRef(jname);
		}

		env->DeleteLocalRef(scoreboard);
	}

	for (auto& entry : g_saved)
	{
		if (entry.second)
			env->DeleteGlobalRef(entry.second);
	}

	g_saved.clear();
	g_parked.clear();
	g_active = false;
}
