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
static std::string g_unavailable_reason;

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

// 1.20 - 1.20.4: MultiPlayerGameMode.getPickRange(), a float on a different
// class. A native registered with the wrong return kind does not fail at attach
// -- the JVM reads a value that was never pushed -- so this shape gets its own
// callback and the bound descriptor decides which one is installed.
jfloat hkGetPickRange(JNIEnv *env, jobject thiz)
{
	if (g_reach_override > 0.0)
	{
		return static_cast<jfloat>(g_reach_override);
	}

	if (ORIG_getEntityInteractionRange && thiz && g_player_entity_class)
	{
		return env->CallNonvirtualFloatMethod(thiz, g_player_entity_class, ORIG_getEntityInteractionRange);
	}

	// Vanilla survival block reach. Whatever this returns when the original is
	// unavailable should at least not shorten the player's arm.
	return 4.5f;
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
			// 3 is JNIHOOK_ERR_ADD_JVMTI_CAPS, NOT "already initialised" -- Init
			// returns OK when it is already up. This code used to read it as
			// success, which is the exact trap AGENT.md opens the hooking section
			// with: g_jnihook is left null and the attach that follows can only
			// fail, having already told the user it was fine.
			logger::log_error(std::string("[reach] JNIHook_Init failed=") +
				std::to_string((int)result) +
				(result == JNIHOOK_ERR_ADD_JVMTI_CAPS
					? " -- the JVM would not grant the JVMTI capabilities (another agent holds can_suspend?)"
					: ""));
			return false;
		}
		jnihook_initialized = true;
	}

	// Two mechanisms, one per era: the interaction-range attribute from 1.20.5 on,
	// and MultiPlayerGameMode.getPickRange before it. They live on different
	// classes and return different types, so both the owner and the callback come
	// from what actually resolved.
	const bool use_attribute = sdk::mappings::have(sdk::mappings::get_entity_interaction_range_name);
	const char* symbol = use_attribute ? "get_entity_interaction_range" : "pick_range";
	const char* method_name = use_attribute ? sdk::mappings::get_entity_interaction_range_name
	                                        : sdk::mappings::pick_range_name;
	const char* method_sig = use_attribute ? sdk::mappings::get_entity_interaction_range_sig
	                                       : sdk::mappings::pick_range_sig;
	void* callback = use_attribute ? reinterpret_cast<void*>(hkGetEntityInteractionRange)
	                               : reinterpret_cast<void*>(hkGetPickRange);

	if (!sdk::mappings::have(method_name))
	{
		g_unavailable_reason = "neither the interaction-range attribute nor getPickRange resolves here";
		return false;
	}

	const char* owner = sdk::mappings::owner_of(symbol);
	jclass player_entity_class = sdk::mappings::have(owner)
		? sdk::classloader::find_class(env, owner) : nullptr;
	if (!player_entity_class)
	{
		g_unavailable_reason = std::string("class not found: ") +
			(sdk::mappings::have(owner) ? owner : "<unresolved>");
		return false;
	}

	jmethodID method_id = env->GetMethodID(player_entity_class, method_name, method_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();

	if (!method_id)
	{
		g_unavailable_reason = std::string("method not found: ") + owner + "." + method_name;
		env->DeleteLocalRef(player_entity_class);
		return false;
	}

	jnihook_result_t result = JNIHook_Attach(method_id, callback, &ORIG_getEntityInteractionRange);
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

		// The result code names the step; this names the cause. Without it a
		// failure on one Minecraft version is indistinguishable from a failure on
		// another, which is exactly the position this hook was in on 26.2.
		const char* detail = JNIHook_LastErrorDetail();
		if (detail && detail[0])
		{
			msg += " -- ";
			msg += detail;
			g_unavailable_reason = detail;
		}
		else
		{
			g_unavailable_reason = "JNIHook error " + std::to_string((int)result);
		}

		jvmtiEnv* jvmti = sdk::java::jvmti();
		if (jvmti)
		{
			jint mods = 0;
			if (jvmti->GetMethodModifiers(method_id, &mods) == JVMTI_ERROR_NONE)
			{
				// The raw bits travel with the verdict: "left native" is a
				// frightening claim to make from one flag test, and when it turned
				// up on a JVM where nothing had been redefined there was no way to
				// tell a real casualty from a misread.
				msg += (mods & 0x0100 /* ACC_NATIVE */)
					? " -- METHOD LEFT NATIVE, the game will crash on the next call"
					: " -- method is not native, no damage done";
				char bits[32];
				sprintf_s(bits, sizeof(bits), " (modifiers 0x%04x)", (unsigned)mods);
				msg += bits;
			}
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

const char* enhance::modules::reach_hook::unavailable_reason()
{
	return g_unavailable_reason.c_str();
}
