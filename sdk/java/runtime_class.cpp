#include "runtime_class.h"
#include "jvmti_env.h"

#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>

#include <string>
#include <unordered_map>

namespace
{
	// binary name -> GlobalRef
	std::unordered_map<std::string, jclass> g_defined;

	void clear_exception(JNIEnv* env)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
	}

	// Falls back to the loader that owns a known Minecraft class. JNIHook takes
	// the same route when it needs a loader for its own DefineClass call.
	jobject loader_from_minecraft_class(JNIEnv* env)
	{
		jvmtiEnv* jvmti = sdk::java::jvmti();
		if (!jvmti)
			return nullptr;

		jclass mc = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
		if (!mc)
			return nullptr;

		jobject loader = nullptr;
		const jvmtiError err = jvmti->GetClassLoader(mc, &loader);
		env->DeleteLocalRef(mc);

		if (err != JVMTI_ERROR_NONE)
			return nullptr;

		return loader;
	}
}

jclass sdk::java::define_from_memory(JNIEnv* env, const char* binary_name,
                                      const unsigned char* bytes, size_t len)
{
	if (!env || !binary_name || !bytes || len == 0)
		return nullptr;

	const std::string key(binary_name);

	const auto cached = g_defined.find(key);
	if (cached != g_defined.end())
		return cached->second;

	// A previous session of this DLL may have already defined it into a loader
	// that outlived us; ask before defining so a re-inject doesn't hit
	// LinkageError: duplicate class definition.
	if (jclass existing = sdk::classloader::find_class(env, binary_name))
	{
		clear_exception(env);
		jclass global = static_cast<jclass>(env->NewGlobalRef(existing));
		env->DeleteLocalRef(existing);
		if (global)
		{
			g_defined[key] = global;
			return global;
		}
	}
	clear_exception(env);

	jobject loader = sdk::classloader::get_classloader();
	bool loader_is_local = false;

	if (!loader)
	{
		loader = loader_from_minecraft_class(env);
		loader_is_local = loader != nullptr;
	}

	jclass defined = env->DefineClass(binary_name, loader,
	                                   reinterpret_cast<const jbyte*>(bytes),
	                                   static_cast<jsize>(len));

	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		defined = nullptr;
	}

	// Retry against the Minecraft class's loader if the primary one refused —
	// KnotClassLoader is the usual suspect and the two are not always the same
	// object.
	if (!defined && !loader_is_local)
	{
		if (jobject fallback = loader_from_minecraft_class(env))
		{
			defined = env->DefineClass(binary_name, fallback,
			                            reinterpret_cast<const jbyte*>(bytes),
			                            static_cast<jsize>(len));
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				defined = nullptr;
			}
			env->DeleteLocalRef(fallback);
		}
	}
	else if (loader_is_local && loader)
	{
		env->DeleteLocalRef(loader);
	}

	if (!defined)
		return nullptr;

	jclass global = static_cast<jclass>(env->NewGlobalRef(defined));
	env->DeleteLocalRef(defined);

	if (!global)
		return nullptr;

	g_defined[key] = global;
	return global;
}

void sdk::java::forget_defined_classes(JNIEnv* env)
{
	if (env)
	{
		for (auto& entry : g_defined)
		{
			if (entry.second)
				env->DeleteGlobalRef(entry.second);
		}
	}

	g_defined.clear();
}
