#include "sprint.h"

#include "../../utils/logger.h"
#include <sdk/mappings/mappings.hpp>

#include <atomic>

namespace
{
	std::atomic<bool> g_stop_requested{false};

	// Resolved on first use, on the tick thread. Method IDs are not
	// thread-bound, so caching them is safe; the JNIEnv they were looked up
	// with is not, which is why nothing here ever stores one.
	jmethodID g_mid_set_sprinting = nullptr;
	bool      g_resolve_failed = false;
}

void enhance::modules::killaura::request_stop_sprint()
{
	g_stop_requested.store(true, std::memory_order_release);
}

void enhance::modules::killaura::apply_on_tick(JNIEnv* env, jobject player)
{
	if (!env || !player) return;
	if (!g_stop_requested.exchange(false, std::memory_order_acq_rel)) return;
	if (g_resolve_failed) return;

	if (!g_mid_set_sprinting)
	{
		jclass cls = env->GetObjectClass(player);
		if (!cls) { g_resolve_failed = true; return; }

		g_mid_set_sprinting = env->GetMethodID(cls,
			sdk::mappings::set_sprinting_name, sdk::mappings::set_sprinting_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_set_sprinting = nullptr; }
		env->DeleteLocalRef(cls);

		if (!g_mid_set_sprinting)
		{
			// Once, not every tick: a failure here is permanent for the session.
			g_resolve_failed = true;
			logger::log_error("[sprint] setSprinting not found -- sprint reset disabled");
			return;
		}
	}

	env->CallVoidMethod(player, g_mid_set_sprinting, JNI_FALSE);
	if (env->ExceptionCheck()) env->ExceptionClear();
}
