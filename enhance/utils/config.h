#pragma once

#include <string>

namespace enhance
{
	namespace config
	{
		// Settings are persisted to %APPDATA%\enhance\config.cfg — deliberately
		// far from the DLL, which the injector drops into %TEMP% under a random
		// name and deletes, so anything written beside it is gone immediately.
		std::string path();

		// Reads the file if it exists. Unknown keys are ignored and missing
		// keys keep their compiled-in default, so adding or removing a setting
		// never invalidates an existing config.
		bool load();

		bool save();

		// Marks the config dirty. Cheap; call it freely.
		void touch();

		// Saves if something changed and the debounce has elapsed. Called once
		// per frame — writing on every change would hit the disk continuously
		// while a slider is being dragged.
		void tick_autosave();
	}
}
