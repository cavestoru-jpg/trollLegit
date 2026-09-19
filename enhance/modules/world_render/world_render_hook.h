#pragma once

namespace enhance
{
	namespace modules
	{
		namespace world_render_hook
		{
			// Defines the Java renderer into the JVM from memory and subscribes
			// it to Fabric's WorldRenderEvents, which drives it once per frame.
			//
			// Nothing is hooked or redefined, so no mod's instrumentation is
			// disturbed — the reason for the event route is at the top of
			// world_render_hook.cpp. The cost is that Fabric API must be
			// installed; without it this returns false and logs why, and the
			// client keeps working with the 2D overlay ESP.
			bool init();

			// Logs the JVMTI class dump used to derive the mappings. Read-only
			// and independent of init(), so it can run at startup without
			// touching the game.
			void dump_mappings();

			void shutdown();

			bool is_attached();

			// True while the hook is queued for the client thread but has not run
			// yet. init() returns false in that window, and the caller must not read
			// that as a refusal: unticking there turns the feature off before it can
			// ever come up.
			bool is_pending();

			// Whether the mapping needed to attach is present at all. Lets the
			// menu explain why the option is unavailable instead of silently
			// doing nothing.
			bool has_mapping();
		}
	}
}
