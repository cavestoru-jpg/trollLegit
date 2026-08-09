#pragma once

#include <sdk/includes.h>

namespace enhance::modules::teams
{
	void run();   // ticked from main loop — refreshes our own color cache

	// Called by killaura to skip teammates. Both arguments are local JNI
	// refs; we don't take ownership.
	//
	// Returns true if the other player should be treated as a teammate
	// (skip the attack). False when teams module is off, the colors don't
	// match, or no detection was possible.
	bool is_teammate(jobject local_player, jobject other);
}
