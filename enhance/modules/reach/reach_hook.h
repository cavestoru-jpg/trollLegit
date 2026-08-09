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
		}
	}
}
