#pragma once

namespace enhance::modules::killaura
{
	void run();   // ticked from the main loop in enhance.cpp

	// Menu diagnostics, so "it does nothing" and "it never found a target"
	// cannot look the same. Mirrors what silent aim exposes.
	struct debug_info
	{
		bool  active = false;      // gates passed, module is doing work
		bool  has_target = false;
		bool  locked = false;      // kept a previously committed target
		bool  attacked = false;    // queued a swing this tick
		int   candidates = 0;
		double distance = -1.0;
		float yaw = 0.0f;
		float pitch = 0.0f;
		const char* blocked_by = "";   // which gate stopped it, if any
	};
	debug_info debug_state();
}
