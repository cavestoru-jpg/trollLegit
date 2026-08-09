#include "interp.h"
#include "jvmti_env.h"

#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/render/render_view.h>
#include <enhance/utils/logger.h>

#include <cstring>
#include <string>

namespace
{
	bool g_resolved = false;
	bool g_usable = false;

	jclass    g_mc_class = nullptr;          // GlobalRef
	jfieldID  g_fid_mc_instance = nullptr;
	jfieldID  g_fid_tick_counter = nullptr;
	jmethodID g_mid_tick_progress = nullptr;

	bool g_fields_resolved = false;
	jfieldID g_fid_last_x = nullptr;
	jfieldID g_fid_last_y = nullptr;
	jfieldID g_fid_last_z = nullptr;

	void clear_exception(JNIEnv* env)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
	}

	// RenderTickCounter's progress getter is the only (Z)F method it declares,
	// so it can be found by shape instead of by another hardcoded obfuscated
	// name that would rot on the next game update.
	jmethodID find_progress_method(JNIEnv* env, jclass counter_class)
	{
		jvmtiEnv* jvmti = sdk::java::jvmti();
		if (!jvmti)
			return nullptr;

		jint count = 0;
		jmethodID* methods = nullptr;
		if (jvmti->GetClassMethods(counter_class, &count, &methods) != JVMTI_ERROR_NONE)
			return nullptr;

		jmethodID found = nullptr;
		int matches = 0;

		for (jint i = 0; i < count; ++i)
		{
			char* name = nullptr;
			char* sig = nullptr;

			if (jvmti->GetMethodName(methods[i], &name, &sig, nullptr) != JVMTI_ERROR_NONE)
				continue;

			if (sig && std::strcmp(sig, sdk::mappings::render_tick_counter_progress_sig) == 0)
			{
				++matches;
				found = methods[i];
			}

			if (name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
			if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
		}

		jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));

		// More than one candidate means the shape is no longer unique and the
		// pick would be a coin flip; better to lose interpolation than to call
		// an unrelated method every frame.
		if (matches != 1)
		{
			logger::log_error("[interp] tick-progress lookup found " + std::to_string(matches) +
			                  " methods with signature " + sdk::mappings::render_tick_counter_progress_sig +
			                  " on " + sdk::mappings::render_tick_counter_class_sig +
			                  " — expected exactly 1, so interpolation is OFF. See the [jvmti] dump.");
			return nullptr;
		}

		return found;
	}

	void resolve(JNIEnv* env)
	{
		if (g_resolved)
			return;

		if (!sdk::mappings::mc_render_tick_counter_name[0])
		{
			g_resolved = true;
			return;
		}

		// Do NOT latch before the lookup can succeed: a single early call (JVM
		// not ready, classloader not initialised) would otherwise disable
		// interpolation for the whole session with no way back.
		jclass mc_local = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
		clear_exception(env);
		if (!mc_local)
			return;

		g_resolved = true;

		g_mc_class = static_cast<jclass>(env->NewGlobalRef(mc_local));
		g_fid_mc_instance = env->GetStaticFieldID(mc_local, sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
		clear_exception(env);
		g_fid_tick_counter = env->GetFieldID(mc_local, sdk::mappings::mc_render_tick_counter_name, sdk::mappings::mc_render_tick_counter_sig);
		clear_exception(env);
		env->DeleteLocalRef(mc_local);

		if (!g_mc_class || !g_fid_mc_instance || !g_fid_tick_counter)
			return;

		jclass counter_class = sdk::classloader::find_class(env, sdk::mappings::render_tick_counter_class_sig);
		clear_exception(env);
		if (!counter_class)
			return;

		g_mid_tick_progress = find_progress_method(env, counter_class);
		env->DeleteLocalRef(counter_class);

		g_usable = g_mid_tick_progress != nullptr;

		logger::log(g_usable
			? "[interp] per-frame interpolation ON"
			: "[interp] per-frame interpolation OFF — boxes will step at the tick rate");
	}
}

float sdk::java::tick_progress()
{
	JNIEnv* env = sdk::render::current_thread_env();
	if (!env)
		return 1.0f;

	resolve(env);
	if (!g_usable)
		return 1.0f;

	jobject mc = env->GetStaticObjectField(g_mc_class, g_fid_mc_instance);
	clear_exception(env);
	if (!mc)
		return 1.0f;

	jobject counter = env->GetObjectField(mc, g_fid_tick_counter);
	clear_exception(env);
	env->DeleteLocalRef(mc);
	if (!counter)
		return 1.0f;

	const jfloat progress = env->CallFloatMethod(counter, g_mid_tick_progress, JNI_TRUE);
	clear_exception(env);
	env->DeleteLocalRef(counter);

	if (progress < 0.0f || progress > 1.0f)
		return 1.0f;

	return progress;
}

bool sdk::java::last_render_fields(JNIEnv* env, jfieldID& x, jfieldID& y, jfieldID& z)
{
	if (!env)
		return false;

	if (!g_fields_resolved)
	{
		g_fields_resolved = true;

		if (sdk::mappings::entity_last_render_x_name[0])
		{
			if (jclass entity_class = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig))
			{
				g_fid_last_x = env->GetFieldID(entity_class, sdk::mappings::entity_last_render_x_name, sdk::mappings::entity_last_render_sig);
				clear_exception(env);
				g_fid_last_y = env->GetFieldID(entity_class, sdk::mappings::entity_last_render_y_name, sdk::mappings::entity_last_render_sig);
				clear_exception(env);
				g_fid_last_z = env->GetFieldID(entity_class, sdk::mappings::entity_last_render_z_name, sdk::mappings::entity_last_render_sig);
				clear_exception(env);
				env->DeleteLocalRef(entity_class);
			}
			clear_exception(env);
		}
	}

	if (!g_fid_last_x || !g_fid_last_y || !g_fid_last_z)
		return false;

	x = g_fid_last_x;
	y = g_fid_last_y;
	z = g_fid_last_z;
	return true;
}
