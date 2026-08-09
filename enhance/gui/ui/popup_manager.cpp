#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <functional>
#include <animations.hpp>
#include <xorstr.hpp>
#include "popup_manager.hpp"

using namespace ImGui;

void popup_manager::open_popup( std::function< void( ) > code ) {
	m_anim_dest = 1.f;
	m_popup = code;
}

void popup_manager::close_popup( ) {
	m_anim_dest = 0.f;
}

void popup_manager::handle( ) {
	m_anim = anim_lerp( m_anim, m_anim_dest, 12.f );

	if ( m_anim_dest == 0.f && m_anim < 0.01f ) {
		m_popup = nullptr;
	} else if ( m_popup ) {
		SetNextWindowPos( GetWindowPos( ) );
		SetNextWindowSize( GetWindowSize( ) );

		PushStyleVar( ImGuiStyleVar_Alpha, m_anim );
		PushStyleColor( ImGuiCol_WindowBg, GetColorU32( ImGuiCol_WindowBg, 0.96f ) );
		Begin( xorstr_( "popup" ), 0, ImGuiWindowFlags_NoDecoration );
		{
			BringWindowToFocusFront( GetCurrentWindow( ) );
			BringWindowToDisplayFront( GetCurrentWindow( ) );

			m_popup( );
		}
		End( );
		PopStyleColor( );
		PopStyleVar( );
	}
}
