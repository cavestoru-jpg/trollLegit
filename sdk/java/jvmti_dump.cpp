#include "jvmti_dump.h"
#include "jvmti_env.h"

#include <sdk/classloader.h>
#include <enhance/utils/logger.h>

#include <string>
#include <cstring>

namespace
{
	bool g_dumped = false;

	std::string modifiers_to_string(jint mods)
	{
		std::string s;
		if (mods & 0x0001) s += "public ";
		if (mods & 0x0002) s += "private ";
		if (mods & 0x0004) s += "protected ";
		if (mods & 0x0008) s += "static ";
		if (mods & 0x0010) s += "final ";
		if (mods & 0x0020) s += "synchronized ";
		if (mods & 0x0100) s += "native ";
		if (mods & 0x0400) s += "abstract ";
		if (!s.empty()) s.pop_back();
		return s;
	}
}

void sdk::java::dump_class_methods(JNIEnv* env, const char* class_sig)
{
	jvmtiEnv* jvmti = sdk::java::jvmti();
	if (!jvmti || !env || !class_sig)
	{
		logger::log_error(std::string("[jvmti] no environment for ") + (class_sig ? class_sig : "?"));
		return;
	}

	// A symbol the running version does not have binds to "". Dumping it would
	// report a class-not-found for the empty string, once per entry, which is how
	// this diagnostic turned into eleven error lines per injection on 26.x.
	if (!class_sig[0])
	{
		return;
	}

	jclass klass = sdk::classloader::find_class(env, class_sig);
	if (!klass)
	{
		logger::log_error(std::string("[jvmti] class not found: ") + class_sig);
		return;
	}

	jint count = 0;
	jmethodID* methods = nullptr;
	if (jvmti->GetClassMethods(klass, &count, &methods) != JVMTI_ERROR_NONE)
	{
		logger::log_error(std::string("[jvmti] GetClassMethods failed: ") + class_sig);
		env->DeleteLocalRef(klass);
		return;
	}

	logger::log(std::string("[jvmti] ==== methods of ") + class_sig + " (" + std::to_string(count) + ") ====");

	for (jint i = 0; i < count; ++i)
	{
		char* name = nullptr;
		char* sig = nullptr;
		jint mods = 0;

		if (jvmti->GetMethodName(methods[i], &name, &sig, nullptr) == JVMTI_ERROR_NONE)
		{
			jvmti->GetMethodModifiers(methods[i], &mods);

			logger::log(std::string("[jvmti]   ") + modifiers_to_string(mods) + " " +
			            (name ? name : "?") + " " + (sig ? sig : "?"));

			if (name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
			if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
		}
	}

	jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));
	env->DeleteLocalRef(klass);
}

void sdk::java::dump_class_fields(JNIEnv* env, const char* class_sig)
{
	jvmtiEnv* jvmti = sdk::java::jvmti();
	if (!jvmti || !env || !class_sig || !class_sig[0])
		return;

	jclass klass = sdk::classloader::find_class(env, class_sig);
	if (!klass)
	{
		logger::log_error(std::string("[jvmti] class not found: ") + class_sig);
		return;
	}

	jint count = 0;
	jfieldID* fields = nullptr;
	if (jvmti->GetClassFields(klass, &count, &fields) != JVMTI_ERROR_NONE)
	{
		logger::log_error(std::string("[jvmti] GetClassFields failed: ") + class_sig);
		env->DeleteLocalRef(klass);
		return;
	}

	logger::log(std::string("[jvmti] ==== fields of ") + class_sig + " (" + std::to_string(count) + ") ====");

	for (jint i = 0; i < count; ++i)
	{
		char* name = nullptr;
		char* sig = nullptr;
		jint mods = 0;

		if (jvmti->GetFieldName(klass, fields[i], &name, &sig, nullptr) == JVMTI_ERROR_NONE)
		{
			jvmti->GetFieldModifiers(klass, fields[i], &mods);

			logger::log(std::string("[jvmti]   ") + modifiers_to_string(mods) + " " +
			            (name ? name : "?") + " " + (sig ? sig : "?"));

			if (name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
			if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
		}
	}

	jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
	env->DeleteLocalRef(klass);
}

bool sdk::java::class_is_mixin_instrumented(JNIEnv* env, const char* class_sig,
                                            std::string* first_example)
{
	jvmtiEnv* jvmti = sdk::java::jvmti();
	if (!jvmti || !env || !class_sig)
		return false;

	jclass klass = sdk::classloader::find_class(env, class_sig);
	if (!klass)
		return false;

	jint count = 0;
	jmethodID* methods = nullptr;
	if (jvmti->GetClassMethods(klass, &count, &methods) != JVMTI_ERROR_NONE)
	{
		env->DeleteLocalRef(klass);
		return false;
	}

	// Mixin names its injected members with a a fixed set of prefixes. Their
	// presence is a reliable signal that another agent has already rewritten
	// this class.
	static const char* k_markers[] = {
		"handler$", "redirect$", "modify$", "mixinextras$", "wrapOperation$", "cancellable$",
	};

	bool found = false;
	for (jint i = 0; i < count && !found; ++i)
	{
		char* name = nullptr;
		if (jvmti->GetMethodName(methods[i], &name, nullptr, nullptr) != JVMTI_ERROR_NONE)
			continue;

		if (name)
		{
			for (const char* marker : k_markers)
			{
				if (std::strstr(name, marker) == name)
				{
					found = true;
					if (first_example)
						*first_example = name;
					break;
				}
			}
			jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
		}
	}

	jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));
	env->DeleteLocalRef(klass);
	return found;
}

void sdk::java::dump_loaded_classes_matching(JNIEnv* env, const char* needle, int limit)
{
	jvmtiEnv* jvmti = sdk::java::jvmti();
	if (!jvmti || !env || !needle || !needle[0])
		return;

	jint count = 0;
	jclass* classes = nullptr;
	if (jvmti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE)
	{
		logger::log_error("[jvmti] GetLoadedClasses failed");
		return;
	}

	logger::log(std::string("[jvmti] loaded classes containing '") + needle + "':");

	int hits = 0;
	for (jint i = 0; i < count; ++i)
	{
		char* sig = nullptr;
		if (jvmti->GetClassSignature(classes[i], &sig, nullptr) == JVMTI_ERROR_NONE && sig)
		{
			if (hits < limit && std::strstr(sig, needle))
			{
				logger::log(std::string("[jvmti]   ") + sig);
				++hits;
			}
			jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
		}

		// Released immediately: the loaded set runs to tens of thousands, and
		// holding a local ref per class overflows the frame long before the
		// loop ends.
		env->DeleteLocalRef(classes[i]);
	}

	jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));

	if (hits == 0)
		logger::log(std::string("[jvmti]   (none out of ") + std::to_string(count) + " loaded)");
	else if (hits >= limit)
		logger::log("[jvmti]   (truncated)");
}

void sdk::java::dump_render_mappings(JNIEnv* env)
{
	if (g_dumped)
		return;

	g_dumped = true;

	logger::log("[jvmti] ################ render mapping dump ################");
	logger::log("[jvmti] Looking for: the world render entry point (hook target),");
	logger::log("[jvmti] Entity lastRender X/Y/Z fields, and the render tick counter.");

	// WorldRenderer and GameRenderer — no longer hook targets (the renderer
	// subscribes to Fabric's WorldRenderEvents instead of hooking anything),
	// but still the classes whose Mixin state says which mods own the render
	// path on this install.
	dump_class_methods(env, sdk::mappings::world_renderer_class_sig);
	dump_class_methods(env, sdk::mappings::gamerenderer_class_sig);
	// Entity — lastRenderX/Y/Z for per-frame interpolation.
	dump_class_fields(env, sdk::mappings::entity_class_sig);
	// MinecraftClient — holds the RenderTickCounter.
	dump_class_fields(env, sdk::mappings::minecraftclass_sig);
	// RenderTickCounter — the tick-progress getter is picked by signature, so
	// its method list is the only way to confirm the pick is unambiguous.
	dump_class_methods(env, sdk::mappings::render_tick_counter_class_sig);
	// Camera — confirms the yaw/pitch/pos accessors the projection relies on.
	dump_class_methods(env, sdk::mappings::camera_class_sig);
	dump_class_fields(env, sdk::mappings::camera_class_sig);

	// ---- Name tags -------------------------------------------------------
	// The scoreboard and team classes are not mapped anywhere yet, and their
	// obfuscated numbers are not something to guess at. They are reachable by
	// return type instead: World.getScoreboard() names the Scoreboard class,
	// and Scoreboard's own methods then name the Team class. Dumping the
	// classes we DO know lets those be read off rather than invented.
	dump_class_methods(env, sdk::mappings::world_class_sig);        // World -> getScoreboard
	dump_class_methods(env, sdk::mappings::player_entity_class_sig); // PlayerEntity -> name, hands, id
	dump_class_methods(env, sdk::mappings::living_entity_class_sig); // LivingEntity -> health, absorption, equipment
	// World.method_8428() returned class_269, so that is Scoreboard. Its own
	// methods name the Team class and the add/remove/lookup calls the vanilla
	// name-tag hiding needs.
	dump_class_methods(env, sdk::mappings::scoreboard_class_sig);   // Scoreboard
	dump_class_methods(env, sdk::mappings::entity_class_sig);       // Entity -> getTeam, scoreboard name

	logger::log("[jvmti] ################ end of dump ################");
}
