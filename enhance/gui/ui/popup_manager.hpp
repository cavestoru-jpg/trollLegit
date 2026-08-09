#pragma once
#include <functional>

class popup_manager {
	std::function< void( ) > m_popup;

	float m_anim = 0.f;
	float m_anim_dest = 0.f;
public:
	void open_popup( std::function< void( ) > code );
	void close_popup( );
	void handle( );

	static popup_manager& get( ) {
		static popup_manager s{ };
		return s;
	}
};
