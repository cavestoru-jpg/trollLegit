#pragma once

#include <Windows.h>
#include <utils/imgui/imgui.h>

namespace enhance::modules::hud
{
	// Main HUD render function - call from GUI::draw()
	void render();

	// Individual HUD component renderers
	void render_watermark();
	void render_potion_list();
	void render_keybinds();
	void render_target_hud();
}
