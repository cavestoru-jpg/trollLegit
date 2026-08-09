#define _CRT_SECURE_NO_WARNINGS
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <animations.hpp>
#include <xorstr.hpp>
#include "comp_builder.hpp"
#include "color_picker.hpp"
#include "widgets_manager.hpp"
#include "font_manager.hpp"
#include <unicodes.hpp>

constexpr size_t max_saved_colors = 32;
constexpr size_t saved_colors_columns = 11;

using namespace ImGui;

bool color_picker::draw( const char* label, float col[4] ) {
	bool value_changed = false;

	struct s {
		float h, s, v;
		float r, g, b;
		bool init;
		char buf[7];
		char alpha_buf[7];
	}; auto& obj = scoped_anim_obj( label, 2323321, s{ } );

	// col may have been rewritten from outside ( config load, presets ) since the last draw
	if ( !obj.init || obj.r != col[0] || obj.g != col[1] || obj.b != col[2] ) {
		ColorConvertRGBtoHSV( col[0], col[1], col[2], obj.h, obj.s, obj.v );
		ImFormatString( obj.buf, sizeof( obj.buf ), "%02X%02X%02X", int( col[0] * 255 ), int( col[1] * 255 ), int( col[2] * 255 ) );
		ImFormatString( obj.alpha_buf, sizeof( obj.alpha_buf ), "%d%%", int( col[3] * 100 ) );
		obj.init = true;
	}

	value_changed |= square( col, obj.h, obj.s, obj.v );

	SameLine( 0, 10.f );

	value_changed |= hue_bar( obj.h, obj.s, obj.v );

	if ( value_changed ) {
		ColorConvertHSVtoRGB( obj.h, obj.s, obj.v, col[0], col[1], col[2] );
	}

	PushStyleColor( ImGuiCol_FrameBg, GetColorU32( ImGuiCol_FrameBgHovered, 0 ) );
	PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1 );
	PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2{ 12, 10 } );
	PushItemFlag( ImGuiItemFlags_NoNav, true );
	bool hex_changed = widgets_manager::get( ).text_field( xorstr_( "##hex_input" ), obj.buf, sizeof( obj.buf ), ImVec2{ 188 - 90, 0 }, 0, i_hash_01 );
	bool hex_active = IsItemActive( );

	SameLine( );

	bool alpha_changed = widgets_manager::get( ).text_field( xorstr_( "##alpha_input" ), obj.alpha_buf, sizeof( obj.alpha_buf ), ImVec2{ 80, 0 }, 0, i_percent_01 );
	bool alpha_active = IsItemActive( );
	PopItemFlag( );

	SameLine( 0, GImGui->Style.ItemSpacing.x + 14.f );

	GetWindowDrawList( )->AddCircleFilled( GetCurrentWindow( )->DC.CursorPos + ImVec2{ 0, GetFrameHeight( ) / 2 }, 8.f, ImColor{ col[0], col[1], col[2], GImGui->Style.Alpha }, 36 );
	Dummy( { 1, 1 } );
	PopStyleVar( 2 );
	PopStyleColor( );

	int i[4] = { };
	unsigned int u[4] = { };

	if ( hex_changed && sscanf( obj.buf, "%02X%02X%02X", &u[0], &u[1], &u[2] ) == 3 ) {
		i[0] = int( u[0] );
		i[1] = int( u[1] );
		i[2] = int( u[2] );

		col[0] = ImClamp( i[0] / 255.f, 0.f, 1.f );
		col[1] = ImClamp( i[1] / 255.f, 0.f, 1.f );
		col[2] = ImClamp( i[2] / 255.f, 0.f, 1.f );

		ColorConvertRGBtoHSV( col[0], col[1], col[2], obj.h, obj.s, obj.v );
		value_changed = true;
	}

	if ( alpha_changed && sscanf( obj.alpha_buf, "%u%%", &u[3] ) == 1 ) {
		i[3] = int( u[3] );

		col[3] = ImClamp( i[3] / 100.f, 0.f, 1.f );
		value_changed = true;
	}

	if ( !hex_active )
		ImFormatString( obj.buf, sizeof( obj.buf ), "%02X%02X%02X", int( col[0] * 255 ), int( col[1] * 255 ), int( col[2] * 255 ) );

	if ( !alpha_active )
		ImFormatString( obj.alpha_buf, sizeof( obj.alpha_buf ), "%d%%", int( col[3] * 100 ) );

	BeginGroup( );
	{
		if ( comp_builder::get( ).button( xorstr_( "add_color" ), ImVec2{ 14, 14 }, [&]( comp_builder::button_env_t env ) {
			auto col = col_anim( col_anim( GetColorU32( ImGuiCol_FrameBgHovered ), GetColorU32( ImGuiCol_FrameBgActive ), env.anim.hover ), GetColorU32( ImGuiCol_FrameBgActive, 0.6f ), env.anim.held );
			auto icon_col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_Text, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Text ), env.anim.held );
			GetWindowDrawList( )->AddRectFilled( env.bb.Min, env.bb.Max, col, 2.f );

			ImFont* icon_font = fonts[icons].get( 12.f );
			GetWindowDrawList( )->AddText( icon_font, icon_font ? icon_font->FontSize : 12.f, env.bb.GetCenter( ) - ImVec2{ 6, 6.f }, icon_col, i_plus );
			} ) ) {
			ImColor new_col{ col[0], col[1], col[2], col[3] };

			bool duplicate = false;
			for ( size_t color_idx = 0; color_idx < m_saved_colors.size( ); ++color_idx )
				if ( m_saved_colors[color_idx].Value.x == new_col.Value.x && m_saved_colors[color_idx].Value.y == new_col.Value.y && m_saved_colors[color_idx].Value.z == new_col.Value.z && m_saved_colors[color_idx].Value.w == new_col.Value.w )
					duplicate = true;

			if ( !duplicate && m_saved_colors.size( ) < max_saved_colors )
				m_saved_colors.push_back( new_col );
		}

		SameLine( 0, 7.f );

		for ( size_t color_idx = 0; color_idx < m_saved_colors.size( ); ++color_idx ) {
			if ( comp_builder::get( ).button( std::to_string( color_idx ).append( "color" ).c_str( ), ImVec2{ 14, 14 }, [&]( comp_builder::button_env_t env ) {
				ImColor col{ m_saved_colors[color_idx].Value.x, m_saved_colors[color_idx].Value.y, m_saved_colors[color_idx].Value.z, GImGui->Style.Alpha };

				GetWindowDrawList( )->AddRectFilled( env.bb.Min, { env.bb.GetCenter( ).x, env.bb.Max.y }, col, 1.f, ImDrawFlags_RoundCornersLeft );

				GetWindowDrawList( )->PushClipRect( { env.bb.GetCenter( ).x, env.bb.Min.y }, { env.bb.Max.x, env.bb.GetCenter( ).y } );
				GetWindowDrawList( )->AddRectFilled( env.bb.Min, env.bb.Max, ImColor{ 0.6f, 0.6f, 0.6f, GImGui->Style.Alpha }, 1.f );
				GetWindowDrawList( )->PopClipRect( );
				GetWindowDrawList( )->PushClipRect( env.bb.GetCenter( ), env.bb.Max );
				GetWindowDrawList( )->AddRectFilled( env.bb.Min, env.bb.Max, ImColor{ 0.9f, 0.9f, 0.9f, GImGui->Style.Alpha }, 1.f );
				GetWindowDrawList( )->PopClipRect( );

				col.Value.w *= m_saved_colors[color_idx].Value.w;
				GetWindowDrawList( )->AddRectFilled( { env.bb.GetCenter( ).x, env.bb.Min.y }, env.bb.Max, col, 1.f, ImDrawFlags_RoundCornersRight );
				} ) ) {
				col[0] = m_saved_colors[color_idx].Value.x;
				col[1] = m_saved_colors[color_idx].Value.y;
				col[2] = m_saved_colors[color_idx].Value.z;
				col[3] = m_saved_colors[color_idx].Value.w;

				ColorConvertRGBtoHSV( col[0], col[1], col[2], obj.h, obj.s, obj.v );
				value_changed = true;
			}

			if ( color_idx < saved_colors_columns ) {
				if ( ( color_idx + 1 ) % ( saved_colors_columns - 1 ) != 0 )
					SameLine( 0, 7.f );
			} else {
				if ( ( color_idx - ( saved_colors_columns - 2 ) ) % saved_colors_columns != 0 )
					SameLine( 0, 7.f );
			}
		}
	}
	EndGroup( );

	obj.r = col[0];
	obj.g = col[1];
	obj.b = col[2];

	return value_changed;
}

bool color_picker::hue_bar( float& h, float s, float v ) {
	float h_values[] {
		0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f
	};
	int h_size = IM_ARRAYSIZE( h_values );

	ImVec2 size{ 28.f, 188.f };
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + size };

	for ( int i = 0; i < h_size - 1; ++i ) {
		ImColor col1, col2;

		ColorConvertHSVtoRGB( h_values[i], ImClamp( s, 0.6f, 1.f ), ImClamp( v, 0.6f, 1.f ), col1.Value.x, col1.Value.y, col1.Value.z );
		ColorConvertHSVtoRGB( h_values[i + 1], ImClamp( s, 0.6f, 1.f ), ImClamp( v, 0.6f, 1.f ), col2.Value.x, col2.Value.y, col2.Value.z );
		col1.Value.w = col2.Value.w = GImGui->Style.Alpha;

		GetWindowDrawList( )->AddRectFilledMultiColor( { bb.Min.x, bb.Min.y + ( size.y / ( h_size - 1 ) ) * i }, { bb.Max.x, bb.Min.y + ( size.y / ( h_size - 1 ) ) * ( i + 1 ) }, col1, col1, col2, col2 );
	}

	GetWindowDrawList( )->AddRect( { bb.Min.x, bb.Min.y + size.y * h - 1.5f }, { bb.Max.x, bb.Min.y + size.y * h + 1.5f }, ImColor{ 1.f, 1.f, 1.f, GImGui->Style.Alpha }, 1.f );

	InvisibleButton( xorstr_( "hue" ), size );
	if ( IsItemActive( ) ) {
		h = ImSaturate( ( GetIO( ).MousePos.y - bb.Min.y ) / size.y );

		return true;
	}

	return false;
}

bool color_picker::square( float col[4], float h, float& s, float& v ) {
	ImVec2 size{ 188.f, 188.f };
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + size };

	ImColor col_white{ 1.f, 1.f, 1.f, GImGui->Style.Alpha };
	ImColor col_black{ 0.f, 0.f, 0.f, GImGui->Style.Alpha };
	ImColor col_hue;

	ColorConvertHSVtoRGB( h, 1, 1, col_hue.Value.x, col_hue.Value.y, col_hue.Value.z );
	col_hue.Value.w = GImGui->Style.Alpha;

	GetWindowDrawList( )->AddRectFilledMultiColor( bb.Min, bb.Max, col_white, col_hue, col_hue, col_white );
    GetWindowDrawList( )->AddRectFilledMultiColor( bb.Min, bb.Max, 0, 0, col_black, col_black );

	GetWindowDrawList( )->AddCircle( bb.Min + size * ImVec2{ s, 1.f - v }, 3.f, col_white, 36 );

	InvisibleButton( xorstr_( "sv" ), size );
    if ( IsItemActive( ) )
    {
        s = ImSaturate( ( GetIO( ).MousePos.x - bb.Min.x ) / size.x );
        v = 1.f - ImSaturate( ( GetIO( ).MousePos.y - bb.Min.y ) / size.y );

        return true;
    }

	return false;
}
