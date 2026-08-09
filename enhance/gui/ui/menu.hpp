#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

// Ported menu shell. Owns the window layout (navbar + search + tabs +
// content) and builds every feature page over enhance's globals. The old
// wolf_menu keeps handling login and the module keybind ticking; everything
// visual below the title bar lives here.
class menu {
	ImVec2 m_size{ 750, 500 };
	bool   m_init = false;

public:
	void init( );
	void draw( );

	bool is_init( ) const {
		return m_init;
	}

	static menu& get( ) {
		static menu inst{ };
		return inst;
	}
};
