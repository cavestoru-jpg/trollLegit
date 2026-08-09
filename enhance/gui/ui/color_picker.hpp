#pragma once
#include "comp_builder.hpp"

class color_picker {
	std::vector< ImColor > m_saved_colors;
public:
	bool hue_bar( float& h, float s, float v );
	bool square( float col[4], float h, float& s, float& v );
	bool draw( const char* label, float col[4] );

	static color_picker& get( ) {
		static color_picker s{ };
		return s;
	}
};
