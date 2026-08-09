#include "reach_hook.h"
#include "../../enhance.h"
#include "../../utils/logger.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <sdk/java/jvmti_env.h>
#include <sdk/render/render_view.h>
#include <cstdio>
#include <string>

static jmethodID g_hooked_mid = nullptr;
static bool      g_detached_cleanly = true;
static jmethodID ORIG_getEntityInteractionRange = nullptr;
static jclass g_player_entity_class = nullptr;
static double g_reach_override = -1.0;

jdouble hkGetEntityInteractionRange(JNIEnv *env, jobject thiz)
{
	if (g_reach_override > 0.0)
	{
		return static_cast<jdouble>(g_reach_override);
	}
	
	if (ORIG_getEntityInteractionRange && thiz && g_player_entity_class)
	{
		return env->CallNonvirtualDoubleMethod(thiz, g_player_entity_class, ORIG_getEntityInteractionRange);
	}
	
	return 3.0;
}

static bool jnihook_initialized = false;

bool enhance::modules::reach_hook::init()
{
	if (g_player_entity_class != nullptr)
	{
		return true;
	}

	// The attach may run on the client thread (posted there so it does not
	// need can_suspend), and JNIEnv is thread-local -- the cached one belongs
	// to the enhance worker and is not valid here.
	auto env = sdk::render::current_thread_env();
	auto jvm = enhance::instance ? enhance::instance->get_java_vm() : nullptr;
	if (!env || !jvm) 
	{
		return false;
	}

	g_reach_override = -1.0;
	ORIG_getEntityInteractionRange = nullptr;

	if (!jnihook_initialized)
	{
		jnihook_result_t result = JNIHook_Init(jvm);
		if (result != JNIHOOK_OK)
		{
			// 3 means JNIHook is already up — most likely because killaura
			// attached first. Treat it as success, the way every other module
			// does. The previous code answered it by calling JNIHook_Shutdown
			// and re-initialising, which tore down the OTHER modules' hooks
			// through the broken FindClass restore path.
			if (result != 3)
			{
				return false;
			}
		}
		jnihook_initialized = true;
	}

	jclass player_entity_class = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
	if (!player_entity_class)
	{

		return false;
	}

	jmethodID method_id = env->GetMethodID(player_entity_class, 
		sdk::mappings::get_entity_interaction_range_name, 
		sdk::mappings::get_entity_interaction_range_sig);
	
	if (!method_id)
	{
		env->DeleteLocalRef(player_entity_class);
		return false;
	}

	jnihook_result_t result = JNIHook_Attach(method_id, reinterpret_cast<void*>(hkGetEntityInteractionRange), &ORIG_getEntityInteractionRange);
	if (result == JNIHOOK_OK) g_hooked_mid = method_id;
	if (result != JNIHOOK_OK)
	{
		// A failed attach is not automatically harmless. If the class was
		// redefined before the failure, getEntityInteractionRange is left
		// native with nothing behind it, and the first call from the render
		// thread throws UnsatisfiedLinkError and takes the game down. Say so,
		// loudly, instead of returning a quiet false.
		std::string msg = "[reach] attach failed=" + std::to_string((int)result) +
		                  " jvmti_err=" + std::to_string(JNIHook_LastJvmtiError()) +
		                  " caps=" + JNIHook_AcquiredCapabilities();

		jvmtiEnv* jvmti = sdk::java::jvmti();
		if (jvmti)
		{
			jint mods = 0;
			if (jvmti->GetMethodModifiers(method_id, &mods) == JVMTI_ERROR_NONE)
				msg += (mods & 0x0100 /* ACC_NATIVE */)
					? " -- METHOD LEFT NATIVE, the game will crash on the next call"
					: " -- method is not native, no damage done";
		}

		logger::log_error(msg);
		env->DeleteLocalRef(player_entity_class);
		return false;
	}

	logger::log(std::string("[reach] hook attached, jvmti caps=") + JNIHook_AcquiredCapabilities());

	g_player_entity_class = reinterpret_cast<jclass>(env->NewGlobalRef(player_entity_class));
	env->DeleteLocalRef(player_entity_class);
	
	if (!g_player_entity_class)
	{
		return false;
	}

	return true;
}

void enhance::modules::reach_hook::shutdown()
{
	// Undo the redefinition before the DLL can be unmapped.
	if (g_hooked_mid)
	{
		const jnihook_result_t r = JNIHook_Detach(g_hooked_mid);
		g_detached_cleanly = (r == JNIHOOK_OK);
		logger::log(std::string("[unload] reach detach result=") + std::to_string((int)r));
		g_hooked_mid = nullptr;
	}
	else
	{
		logger::log("[unload] reach was never attached");
	}

	g_reach_override = -1.0;
	ORIG_getEntityInteractionRange = nullptr;
	
	if (enhance::instance)
	{
		try
		{
			auto env = enhance::instance->get_env();
			if (g_player_entity_class && env)
			{
				env->DeleteGlobalRef(g_player_entity_class);
				g_player_entity_class = nullptr;
			}
		}
		catch (...)
		{
			g_player_entity_class = nullptr;
		}
	}
	else
	{
		g_player_entity_class = nullptr;
	}
	
	// JNIHook_Shutdown is deliberately NOT called. It restores classes by name
	// through env->FindClass, which on a native thread resolves against the
	// AppClassLoader and cannot see net/minecraft/* under Fabric — it throws
	// NoClassDefFoundError, leaves the class still routed through this DLL, and
	// tears down the JNIHook state that the other modules' JNIHook_Detach calls
	// still need. Per-method Detach uses GetMethodDeclaringClass instead and
	// needs no name lookup, so that is the only teardown path used here.
	jnihook_initialized = false;
}

void enhance::modules::reach_hook::set_reach(double distance)
{
	g_reach_override = distance;
}

bool enhance::modules::reach_hook::detached_cleanly()
{
	return g_detached_cleanly;
}
