#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <functional>
#include <vector>
#include <string>
#include <comp_builder.hpp>
#include <animations.hpp>
#include <xorstr.hpp>
#include <unicodes.hpp>
#include "tabs_manager.hpp"
#include "font_manager.hpp"
#include "child_manager.hpp"
#include "lang_manager.hpp"

using namespace ImGui;

bool tabs_manager::tab( int i ) {
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + ImVec2{ GetWindowWidth( ) - 28, 32 } };
	comp_builder::get( ).selectable( m_tabs[i].label.c_str( ), i == next, bb, [&]( const comp_builder::selectable_env_t& env ) {
		if ( env.pressed && i != next ) {
			next = i;
			m_anim_dest = 0.f;
		}

		if ( env.anim.selected > 0.001f )
			GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, IM_COL32( 19, 20, 26, (int)( 190.f * env.anim.selected * GImGui->Style.Alpha ) ), GImGui->Style.FrameRounding );

		auto col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextDisabled, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Text ), env.anim.selected );
		auto icon_col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextDisabled, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Scheme ), env.anim.selected );

		ImFont* icon_fnt = fonts[icons].get( 14.f );
		if ( !icon_fnt )
			icon_fnt = GetFont( );
		GetWindowDrawList( )->AddText( icon_fnt, icon_fnt->FontSize, bb.Min + ImVec2{ 9, bb.GetHeight( ) / 2 - 7 }, icon_col, m_tabs[i].icon );
		GetWindowDrawList( )->AddText( bb.Min + ImVec2{ 32, bb.GetHeight( ) / 2 - GImGui->FontSize / 2 }, col, env.label, FindRenderedTextEnd( env.label ) );
	} );

	return false;
}

bool tabs_manager::subtab( int i ) {
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + CalcTextSize( lang_manager::get( ).translate( m_tabs[current].subtabs[i].c_str( ) ), 0, 1 ) };
	comp_builder::get( ).selectable( m_tabs[current].subtabs[i].c_str( ), i == m_tabs[current].next, bb, [&]( const comp_builder::selectable_env_t& env ) {
		if ( env.pressed && i != m_tabs[current].next ) {
			m_tabs[current].next = i;
			m_subtabs_anim_dest = 0.f;
		}

		auto col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextDisabled, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Text ), env.anim.selected );
		GetWindowDrawList( )->AddText( bb.Min, col, env.label, FindRenderedTextEnd( env.label ) );
	} );

	return false;
}

void tabs_manager::render_tabs( float spacing, bool line ) {
	BeginGroup( );
    {
        PushStyleVar( ImGuiStyleVar_ItemSpacing, { spacing, spacing } );

        for ( int i = 0; i < m_tabs.size( ); ++i ) {
            tab( i );

            if ( line ) SameLine( );
        }

        PopStyleVar( );
    }
    EndGroup( );

	handle( );
}

void tabs_manager::render_subtabs( float spacing, bool line ) {
	if ( current < 0 || current >= ( int )m_tabs.size( ) || m_tabs[current].subtabs.empty( ) )
        return;

	SetCursorPos( { 14.f, 14.f } );
    BeginGroup( );
    {
        PushStyleVar( ImGuiStyleVar_ItemSpacing, { spacing, spacing } );
        PushStyleVar( ImGuiStyleVar_Alpha, m_anim * GImGui->Style.Alpha );

        for ( int i = 0; i < m_tabs[current].subtabs.size( ); ++i ) {
            subtab( i );

            if ( line && i < ( m_tabs[current].subtabs.size( ) - 1 ) ) SameLine( );
        }

        PopStyleVar( 2 );
    }
    EndGroup( );

	SetCursorPos( { 0, GetCursorPos( ).y - GImGui->Style.ItemSpacing.y } );
}

void tabs_manager::handle( ) {
	m_anim = anim_lerp( m_anim, m_anim_dest, 20.f );
	m_subtabs_anim = anim_lerp( m_subtabs_anim, m_subtabs_anim_dest, 20.f );

	if ( m_anim < 0.05f ) {
		current = next;
		m_anim_dest = 1.f;
	}

	if ( m_subtabs_anim < 0.05f ) {
		if ( current >= 0 && current < ( int )m_tabs.size( ) )
			m_tabs[current].cur = m_tabs[current].next;
		m_subtabs_anim_dest = 1.f;
	}
}

void tabs_manager::draw_page( ImGuiWindow* window ) {
	if ( !window || current < 0 || current >= ( int )m_tabs.size( ) )
		return;

	auto& t = m_tabs[current];

	if ( t.cur < 0 || t.pages.size( ) <= ( size_t )t.cur )
		return;

	ImGuiWindow* content_window = GetCurrentWindow( );

	child_manager::get( ).smooth_scroll( );

	t.pages[t.cur]( );

	SetNextWindowPos( window->Pos );
	SetNextWindowSize( window->Size );
	PushStyleVar( ImGuiStyleVar_WindowPadding, { 0, 0 } );
	Begin( xorstr_( "glow" ), 0, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration );
	{
		PopStyleVar( );

		BringWindowToFocusFront( GetCurrentWindow( ) );
		BringWindowToDisplayFront( GetCurrentWindow( ) );

		static float anim1 = 0.f;
		anim1 = anim_lerp( anim1, float( content_window->Scroll.y > 0 ), 24.f );

		GetWindowDrawList( )->AddRectFilledMultiColor( content_window->Pos, content_window->Pos + ImVec2{ content_window->Size.x, GImGui->Style.WindowPadding.y }, GetColorU32( ImGuiCol_WindowBg, anim1 ), GetColorU32( ImGuiCol_WindowBg, anim1 ), GetColorU32( ImGuiCol_WindowBg, 0.f ), GetColorU32( ImGuiCol_WindowBg, 0.f ) );
	}
	End( );
}

void tabs_manager::add_page( int tab, std::function< void( ) > code ) {
	if ( tab < 0 || tab >= ( int )m_tabs.size( ) )
		return;

	m_tabs[tab].pages.push_back( code );
}
