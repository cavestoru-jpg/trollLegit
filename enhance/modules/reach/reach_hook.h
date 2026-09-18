#pragma once

#include <sdk/includes.h>

namespace enhance
{
	namespace modules
	{
		namespace reach_hook
		{
			bool init();
			void shutdown();

	// False when shutdown could not undo the class redefinition.
	bool detached_cleanly();
			void set_reach(double distance);

			// Why the hook is not installed, in words, for the menu. Empty while
			// it is attached or has not been tried. Retrying an attach that cannot
			// work costs a class redefinition every time, so the module stops
			// trying and says why instead.
			const char* unavailable_reason();
		}
	}
}
