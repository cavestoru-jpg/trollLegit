#pragma once
#include <string>
#include <functional>
#include "comp_builder.hpp"

class child_manager {
	std::string m_current;
public:
	void begin_child( const char* label, ImVec2 size = ImVec2{ 0, 0 } );
	void end_child( );

	void smooth_scroll( );

	std::string& get_current( ) {
		return m_current;
	}

	static child_manager& get( ) {
		static child_manager s{ };
		return s;
	}
};
