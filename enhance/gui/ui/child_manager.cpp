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
#include "child_manager.hpp"
#include "font_manager.hpp"
#include "lang_manager.hpp"

using namespace ImGui;

void child_manager::begin_child( const char* label, ImVec2 size ) {
	m_current = label;

	auto window = GetCurrentWindow( );

	PushStyleVar( ImGuiStyleVar_WindowPadding, { 0, 0 } );
	PushStyleColor( ImGuiCol_ChildBg, ImVec4{ 0.f, 0.f, 0.f, 0.f } );
	Begin( label, 0, ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoDecoration );
	PopStyleColor( );
	PopStyleVar( );
	SetWindowSize( CalcItemSize( size, window->Size.x / 2 - GImGui->Style.WindowPadding.x - GImGui->Style.ItemSpacing.x / 2, GetCurrentWindow( )->ContentSize.y ) );

	GetWindowDrawList( )->AddRectFilled( GetWindowPos( ), GetWindowPos( ) + GetWindowSize( ), IM_COL32( 19, 20, 26, (int)( 200.f * GImGui->Style.Alpha ) ), 4.f );

	window = GetCurrentWindow( );

	window->DrawList->AddText( GetWindowPos( ) + ImVec2{ 14.f, 14.f }, GetColorU32( ImGuiCol_TextDisabled ), lang_manager::get( ).translate( label ), FindRenderedTextEnd( lang_manager::get( ).translate( label ) ) );

	SetCursorPos( { 0, 32.f } );
	const float content_y = GetCursorPos( ).y;
	char temp[256];
	ImFormatString( temp, sizeof( temp ), xorstr_( "child %s" ), label );
	PushStyleVar( ImGuiStyleVar_WindowPadding, { 14.f, 14.f } );
	PushStyleVar( ImGuiStyleVar_ItemSpacing, { 12.f, 12.f } );
	Begin( temp, 0, ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoBackground | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoDecoration );
	SetWindowSize( { window->Size.x, size.y == 0 ? GetCurrentWindow( )->ContentSize.y + GImGui->Style.WindowPadding.y * 2 : size.y - content_y } );
	PushItemWidth( GetWindowWidth( ) - GImGui->Style.WindowPadding.x * 2 );
}

void child_manager::end_child( ) {
	PopItemWidth( );
	EndChild( );
	PopStyleVar( 2 );
	EndChild( );
	m_current.clear( );
}

void child_manager::smooth_scroll( ) {
	struct smooth_scroll_state_t {
		float scroll;
		float scroll_anim;
	};

	constexpr int smooth_scroll_seed = 23123;

	auto& obj = scoped_anim_obj( GetCurrentWindow( )->Name, smooth_scroll_seed, smooth_scroll_state_t{ } );

	obj.scroll = ImClamp( obj.scroll, 0.f, GetCurrentWindow( )->ScrollMax.y );
	obj.scroll_anim = ImClamp( obj.scroll_anim, 0.f, GetCurrentWindow( )->ScrollMax.y );

	ImGuiWindow* wheeling_window = nullptr;
	if ( GImGui->HoveredWindow ) {
		for ( ImGuiWindow* window = GImGui->HoveredWindow; window->Flags & ImGuiWindowFlags_ChildWindow; window = window->ParentWindow ) {
			if ( window->ScrollMax[ImGuiAxis_Y] == 0 )
				continue;

			wheeling_window = window;
			break;
		}
	}

	if ( !wheeling_window ) return;

	if ( wheeling_window == GetCurrentWindow( ) )
		obj.scroll = ImClamp( obj.scroll - GetIO( ).MouseWheel * 50.f, 0.f, GetCurrentWindow( )->ScrollMax.y );
	obj.scroll_anim = anim_lerp( obj.scroll_anim, obj.scroll, 40.f );
	GetCurrentWindow( )->Scroll.y = obj.scroll_anim;
}
