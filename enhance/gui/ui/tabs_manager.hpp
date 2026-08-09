#pragma once
#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <unicodes.hpp>
#include <xorstr.hpp>

class tabs_manager {
	float m_anim = 1.f, m_subtabs_anim = 1.f;
	float m_anim_dest = 1.f, m_subtabs_anim_dest = 1.f;

	struct tab_t {
		const char* icon;
		std::string label;

		std::vector< std::string > subtabs;

		std::vector< std::function< void( ) > > pages{ };

		int next = 0;
		int cur = 0;
	};

	// Subtab names are positional: entry N names the Nth add_page( tab )
	// call, in call order. A page added without a name here has nothing to
	// click and is simply unreachable.
	std::vector< tab_t > m_tabs {
		{ i_target_02,   xorstr_( "Combat" ),   { xorstr_( "Aim" ), xorstr_( "Melee" ), xorstr_( "Auto" ) } },
		{ i_move,        xorstr_( "Movement" ) },
		{ i_eye,         xorstr_( "Visuals" ) },
		{ i_tool_01,     xorstr_( "Utility" ) },
		{ i_command,     xorstr_( "Macros" ) },
		{ i_settings_02, xorstr_( "Settings" ) },
	};
public:
	int current = 0, next = 0;

	bool tab( int i );
	bool subtab( int i );

	void render_tabs( float spacing, bool line = false );
	void render_subtabs( float spacing, bool line = true );
	void handle( );

	void add_page( int tab, std::function< void( ) > code );
	void draw_page( ImGuiWindow* window );

	float& get_tabs_anim( ) {
		return m_anim;
	}

	float get_anim( ) {
		return m_subtabs_anim * m_anim;
	}

	tab_t& get_tab( ) {
		return m_tabs[ std::clamp( current, 0, ( int )m_tabs.size( ) - 1 ) ];
	}

	tab_t& get_tab( int i ) {
		return m_tabs[ std::clamp( i, 0, ( int )m_tabs.size( ) - 1 ) ];
	}

	static tabs_manager& get( ) {
		static tabs_manager s{ };
		return s;
	}
};
