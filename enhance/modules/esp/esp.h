#pragma once

#include <sdk/includes.h>
#include <sdk/minecraft/util/box.h>
#include <string>

namespace enhance
{
	namespace modules
	{
		class esp
		{
		public:
			static void run();
			static void draw_boxes();
		};
	}
}

struct esp_player_data
{
	double x, y, z;
	// Position the game interpolates from when drawing a frame between ticks.
	// The box is shifted by the same amount the model is, otherwise it steps at
	// 20 Hz while the player moves smoothly.
	double last_x, last_y, last_z;
	bool   has_last;
	double min_x, min_y, min_z;
	double max_x, max_y, max_z;
	float health;
	float max_health;
	// Decorated display name, as shown to the player.
	std::string name;
	// Undecorated identity used for team and friend matching — a display name
	// carrying a rank prefix never matches a saved friend.
	std::string scoreboard_name;
};

enum class ESPMode
{
	GLOW = 0,
	OUTLINE = 1,
	BOX = 2
};
