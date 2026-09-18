#pragma once

#include <sdk/includes.h>

// What this Minecraft version can actually do.
//
// A feature that needs a symbol the running version does not have must not be
// offered as if it worked. AGENT.md puts it plainly: do not ship a control that
// does nothing -- "it is off" and "this version cannot do it" have to look
// different, or the next hour goes into debugging a feature that was never
// installed.
//
// Each entry names the symbols the feature cannot work without. Availability is
// derived from the same table the client binds from, so a version that gains or
// loses API is reflected without anyone editing this list.
namespace sdk
{
	namespace caps
	{
		enum class feature
		{
			model_pitch,            // the silent pitch the player model shows
			rotation_echo,          // replying to the server's rotation packet
			riptide,                // trident launch direction
			reach,                  // the interaction-range override
			slot_switch,            // writing the selected hotbar slot
			storage_esp,            // chests through the world's block entity set
			team_colours,           // team colour from the dyed-armour component
			input_write,            // writing movement input
			count
		};

		// True when every symbol the feature needs resolved on this version.
		bool available(feature f);

		// Why it is not available, phrased for the menu: "needs updateRenderState,
		// absent on 1.21.1". Empty when it is available.
		const char* why_not(feature f);

		// Writes the unavailable features to the log, once, after binding. The menu
		// shows the same thing, but a log line is what a bug report carries.
		void log_summary();
	}
}
