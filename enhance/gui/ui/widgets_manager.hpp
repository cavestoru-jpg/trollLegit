#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <functional>
#include <atomic>
#include <vector>
#include <string>
#include <animations.hpp>
#include "comp_builder.hpp"

using namespace ImGui;

class widgets_manager {
public:
	bool checkbox( const char* label, bool* v, int* key = nullptr, float* col = nullptr, std::function< void( ) > options = nullptr, bool warning = false, float* col2 = nullptr, bool add_to_search = true );
	bool checkbox( const char* label, std::atomic<bool>* v );
	bool checkbox( const char* label, std::atomic<bool>* v, int* key, float* col, std::function< void( ) > options );
	template < typename T >
	bool slider( const char* label, T* v, T min, T max, const char* format ) {
		register_search_item( label, [label_copy = std::string( label ), this, v, min, max, format_copy = std::string( format )]( ) { slider( label_copy.c_str( ), v, min, max, format_copy.c_str( ) ); } );

		ImRect total_bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + ImVec2{ CalcItemWidth( ), GImGui->FontSize + GImGui->Style.ItemInnerSpacing.y + 5 } };
		ImRect bb{ total_bb.Max - ImVec2{ total_bb.GetWidth( ), 5 }, total_bb.Max };
		return comp_builder::get( ).slider( label, v, min, max, format,
			total_bb,
			bb,
			[&]( const comp_builder::slider_env_t& env ) {
				ImColor col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextDisabled, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Text ), env.anim.held );

				GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, GetColorU32( ImGuiCol_FrameBg ), 2 );
				GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Min + ImVec2{ env.anim.val_anim, bb.GetHeight( ) }, GetColorU32( ImGuiCol_Scheme ), 2 );
				GetWindowDrawList( )->AddCircleFilled( { bb.Min.x + env.anim.val_anim, bb.GetCenter( ).y }, 5.5f + env.anim.anim - 2.f * env.anim.held, GetColorU32( ImGuiCol_Text ), 36 );

				GetWindowDrawList( )->AddText( total_bb.Min, GetColorU32( ImGuiCol_Text ), env.label, FindRenderedTextEnd( env.label ) );
				GetWindowDrawList( )->AddText( { total_bb.Max.x - CalcTextSize( env.buf ).x, total_bb.Min.y }, col, env.buf );
			} );
	}
	bool slider_int( const char* label, int* v, int min, int max, const char* format = "%d" );
	bool slider_float( const char* label, float* v, float min, float max, const char* format = "%.1f" );
	void combo_ex( const char* label, const char* preview_value, std::function< void( comp_builder::combo_env_t env ) > code );
	void combo( const char* label, int* v, const std::vector< std::string >& items );
	void combo( const char* label, std::atomic<int>* v, const std::vector< std::string >& items );
	// add_to_search must be false when v points at storage that dies with the frame:
	// the search index keeps the pointer and replays the widget on a later frame
	void multi_combo( const char* label, bool* v, const std::vector< std::string >& items, bool add_to_search = true );
	template < size_t N >
	void multi_combo( const char* label, bool ( &v )[N], const std::vector< std::string >& items, bool add_to_search = true ) {
		if ( N < items.size( ) )
			return multi_combo( label, ( bool* )v, std::vector< std::string >( items.begin( ), items.begin( ) + N ), add_to_search );

		multi_combo( label, ( bool* )v, items, add_to_search );
	}
	void binder( const char* label, int* key, bool add_to_search = true );
	void binder( const char* label, std::atomic<int>* key );
	bool text_field( const char* label, char* buf, size_t buf_size, ImVec2 size = ImVec2{ 0, 0 }, const char* hint = 0, const char* icon = 0 );
	// only a button that carries its action can be replayed by the search view
	bool button( const char* label, ImVec2 size = ImVec2{ 0, 0 }, std::function< void( ) > on_click = nullptr );
	bool color_edit( const char* label, float col[4] );
	bool selectable( const char* label, bool selected, ImVec2 size = ImVec2{ 0, 0 } );
	void separator( );

	static widgets_manager& get( ) {
		static widgets_manager s{ };
		return s;
	}
private:
	void register_search_item( const char* label, std::function< void( ) > code );
};
