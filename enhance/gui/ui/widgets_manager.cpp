#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <string>
#include <functional>
#include <animations.hpp>

#include "comp_builder.hpp"
#include "widgets_manager.hpp"
#include "color_picker.hpp"
#include "font_manager.hpp"
#include <unicodes.hpp>
#include "child_manager.hpp"
#include "tabs_manager.hpp"
#include "lang_manager.hpp"
#include "search_manager.hpp"

using namespace ImGui;

bool widgets_manager::checkbox( const char* label, bool* v, int* key, float* col, std::function< void( ) > options, bool warning, float* col2, bool add_to_search ) {
	if ( add_to_search )
		search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, v, key, col, options, warning, col2]( ) { checkbox( label_copy.c_str( ), v, key, col, options, warning, col2 ); } );

	float square_sz = 16;

	ImRect total_bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + ImVec2{ CalcTextSize( label, 0, 1 ).x + GImGui->Style.ItemSpacing.x + square_sz, square_sz } };

	if ( warning ) {
		total_bb.Min.x += 24;
		total_bb.Max.x += 24;
	}

	ImRect bb{ total_bb.Min, total_bb.Min + ImVec2{ square_sz, square_sz } };
	ImVec2 options_pos{ bb.Min.x + CalcItemWidth( ) - 24.f * warning, bb.Min.y };

	return comp_builder::get( ).checkbox( label, v, key, col, options, warning, total_bb, bb, options_pos, [&]( comp_builder::checkbox_env_t env ) {
		ImColor col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextDisabled, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Text ), env.anim.enabled );

		GetWindowDrawList( )->AddRect( bb.Min, bb.Max, GetColorU32( ImGuiCol_Border ), 2 );
		GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, GetColorU32( ImGuiCol_Scheme, env.anim.enabled ), 2 );
		RenderCheckMark( GetWindowDrawList( ), bb.GetCenter( ) - ImVec2{ 4, 4 }, GetColorU32( ImGuiCol_ChildBg, env.anim.enabled ), 8 );

		GetWindowDrawList( )->AddText( { total_bb.Min.x + GImGui->Style.ItemInnerSpacing.x + square_sz, total_bb.GetCenter( ).y - GImGui->FontSize / 2 - 0.5f }, col, env.label, FindRenderedTextEnd( env.label ) );
	}, col2 );
}

bool widgets_manager::checkbox( const char* label, std::atomic<bool>* v ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, v]( ) { checkbox( label_copy.c_str( ), v ); } );

	bool b = v->load( );
	if ( checkbox( label, &b, nullptr, nullptr, nullptr, false, nullptr, false ) ) {
		v->store( b );
		return true;
	}
	return false;
}

bool widgets_manager::checkbox( const char* label, std::atomic<bool>* v, int* key, float* col, std::function< void( ) > options ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, v, key, col, options]( ) { checkbox( label_copy.c_str( ), v, key, col, options ); } );

	bool b = v->load( );
	if ( checkbox( label, &b, key, col, options, false, nullptr, false ) ) {
		v->store( b );
		return true;
	}
	return false;
}

void widgets_manager::register_search_item( const char* label, std::function< void( ) > code ) {
	search_manager::get( ).add_item( label, code );
}

bool widgets_manager::slider_int( const char* label, int* v, int min, int max, const char* format ) {
	return slider( label, v, min, max, format );
}

bool widgets_manager::slider_float( const char* label, float* v, float min, float max, const char* format ) {
	return slider( label, v, min, max, format );
}

void widgets_manager::combo_ex( const char* label, const char* preview_value, std::function< void( comp_builder::combo_env_t env ) > code ) {
	ImRect total_bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + ImVec2{ CalcItemWidth( ), GetFrameHeight( ) + GImGui->FontSize + GImGui->Style.ItemInnerSpacing.y } };
	ImRect bb{ total_bb.Max - ImVec2{ CalcItemWidth( ), GetFrameHeight( ) }, total_bb.Max };

	comp_builder::get( ).combo( label, total_bb, bb, [&]( const comp_builder::combo_env_t& env ) {
		ImColor col = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextDisabled, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Scheme ), env.anim.open );

		GetWindowDrawList( )->AddText( total_bb.Min, GetColorU32( ImGuiCol_Text ), env.label, FindRenderedTextEnd( env.label ) );

		GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, GetColorU32( ImGuiCol_FrameBg ), GImGui->Style.FrameRounding, env.open ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll );
		GetWindowDrawList( )->AddText( bb.Min + GImGui->Style.FramePadding, GetColorU32( ImGuiCol_Text ), lang_manager::get( ).translate( preview_value ), FindRenderedTextEnd( lang_manager::get( ).translate( preview_value ) ) );
		ImFont* icon_fnt = fonts[icons].get( 14.f );
		if ( !icon_fnt )
			icon_fnt = GetFont( );
		GetWindowDrawList( )->AddText( icon_fnt, icon_fnt->FontSize, { bb.Max.x - GImGui->Style.FramePadding.x - 14, bb.GetCenter( ).y - 7.5f }, col, i_chevron_selector_vertical );

		if ( env.anim.open > 0.05f ) {
			SetNextWindowPos( { bb.Min.x, bb.Max.y } );
			PushStyleVar( ImGuiStyleVar_Alpha, env.anim.open );
			PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0, 0 } );
			PushStyleVar( ImGuiStyleVar_WindowRounding, GImGui->Style.FrameRounding );
			PushStyleVar( ImGuiStyleVar_WindowPadding, { 0, 0 } );
			PushStyleColor( ImGuiCol_WindowBg, GetColorU32( ImGuiCol_FrameBg ) );
			Begin( label, 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground );
			{
				SetWindowSize( { bb.GetWidth( ), ( GetCurrentWindow( )->ContentSize.y + GImGui->Style.WindowRounding ) * env.anim.open } );

				BringWindowToDisplayFront( GetCurrentWindow( ) );
				BringWindowToFocusFront( GetCurrentWindow( ) );

				GetWindowDrawList( )->AddRectFilled( GetWindowPos( ), GetWindowPos( ) + GetWindowSize( ), GetColorU32( ImGuiCol_WindowBg ), GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom );
				GetWindowDrawList( )->AddRect( GetWindowPos( ), GetWindowPos( ) + GetWindowSize( ), GetColorU32( ImGuiCol_Border ), GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom );

				if ( !IsWindowHovered( ImGuiHoveredFlags_AnyWindow ) && IsMouseClicked( 0 ) && !env.hovered ) {
					env.open = false;
				}

				SetCursorPosY( GImGui->Style.WindowRounding );
				PushStyleColor( ImGuiCol_FrameBg, GetColorU32( ImGuiCol_FrameBgHovered ) );

				code( env );

				PopStyleColor( );
			}
			End( );
			PopStyleColor( );
			PopStyleVar( 4 );
		}
	} );
}

void widgets_manager::combo( const char* label, int* v, const std::vector< std::string >& items ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, v, items]( ) { combo( label_copy.c_str( ), v, items ); } );

	if ( items.empty( ) )
		return;

	if ( *v < 0 || *v >= ( int )items.size( ) )
		*v = 0;

	combo_ex( label, items[*v].c_str( ), [&]( comp_builder::combo_env_t env ) {
		for ( int i = 0; i < items.size( ); ++i ) {
			if ( selectable( items[i].c_str( ), *v == i ) ) {
				*v = i;
				env.open = !env.open;
			}
		}
	} );
}

void widgets_manager::combo( const char* label, std::atomic<int>* v, const std::vector< std::string >& items ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, v, items]( ) { combo( label_copy.c_str( ), v, items ); } );

	int i = v->load( );
	combo( label, &i, items );
	if ( i != v->load( ) )
		v->store( i );
}

void widgets_manager::multi_combo( const char* label, bool* v, const std::vector< std::string >& items, bool add_to_search ) {
	if ( add_to_search )
		search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, v, items]( ) { multi_combo( label_copy.c_str( ), v, items ); } );

	auto& style = GetStyle( );

	std::string buf;

	for ( size_t i = 0; i < items.size( ); ++i ) {
		if ( v[i] ) {
			buf += lang_manager::get( ).translate( items[i].c_str( ) );
			buf += ", ";
		}
	}

	if ( !buf.empty( ) ) {
		buf.resize( buf.size( ) - 2 );
	}

	if ( CalcTextSize( buf.c_str( ) ).x > 160 - style.FramePadding.x - 10 ) {
		const char* cut = buf.c_str( );
		GImGui->Font->CalcTextSizeA( GImGui->FontSize, 160 - style.FramePadding.x - 10, 0.0f, buf.c_str( ), FindRenderedTextEnd( buf.c_str( ) ), &cut );
		buf.resize( cut - buf.c_str( ) );
		if ( !buf.empty( ) && buf[buf.size( ) - 1] == ',' ) {
			buf.resize( buf.size( ) - 1 );
		}
		buf.append( ".." );
	}

	combo_ex( label, buf.c_str( ), [&]( comp_builder::combo_env_t env ) {
		for ( int i = 0; i < items.size( ); ++i ) {
			if ( selectable( items[i].c_str( ), v[i] ) ) {
				v[i] = !v[i];
			}
		}
	} );
}

void widgets_manager::binder( const char* label, int* key, bool add_to_search ) {
	if ( add_to_search )
		search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, key]( ) { binder( label_copy.c_str( ), key ); } );

	comp_builder::get( ).binder( label, key, [&]( const comp_builder::binder_env_t& env ) {
		GetWindowDrawList( )->AddText( { env.total_bb.Min.x, env.total_bb.GetCenter( ).y - GImGui->FontSize / 2 }, GetColorU32( ImGuiCol_Text ), env.label, FindRenderedTextEnd( env.label ) );

		GetWindowDrawList( )->AddRectFilled( env.bb.Min, env.bb.Max, GetColorU32( ImGuiCol_FrameBg ), 2 );
		int key_idx = *key;
		const char* key_str = ( key_idx >= 0 && key_idx < ( int )env.keys.size( ) ) ? env.keys[key_idx].c_str( ) : env.keys[0].c_str( );
		ImFont* key_fnt = fonts[font].get( 12.f );
		if ( !key_fnt )
			key_fnt = GetFont( );
		ImFont* icon_fnt = fonts[icons].get( 12.f );
		if ( !icon_fnt )
			icon_fnt = GetFont( );
		GetWindowDrawList( )->AddText( key_fnt, key_fnt->FontSize, env.bb.Min + ImVec2{ 6.f, env.bb.GetHeight( ) / 2 - 6.f }, GetColorU32( ImGuiCol_Text ), key_str );
		GetWindowDrawList( )->AddText( icon_fnt, icon_fnt->FontSize, env.bb.Min + ImVec2{ env.bb.GetWidth( ) - 18.f, env.bb.GetHeight( ) / 2 - 6.f }, GetColorU32( ImGuiCol_Scheme ), i_keyboard_02 );
	} );
}

void widgets_manager::binder( const char* label, std::atomic<int>* key ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, key]( ) { binder( label_copy.c_str( ), key ); } );

	int k = key->load( );
	binder( label, &k, false );
	if ( k != key->load( ) )
		key->store( k );
}

bool widgets_manager::text_field( const char* label, char* buf, size_t buf_size, ImVec2 size, const char* hint, const char* icon ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, buf, buf_size, size, hint_copy = hint ? std::string( hint ) : std::string( ), icon_copy = icon ? std::string( icon ) : std::string( ), has_hint = hint != nullptr, has_icon = icon != nullptr]( ) { text_field( label_copy.c_str( ), buf, buf_size, size, has_hint ? hint_copy.c_str( ) : nullptr, has_icon ? icon_copy.c_str( ) : nullptr ); } );

	char str_id[256];
	ImFormatString( str_id, sizeof( str_id ), "##%s", label );

	ImVec2 pos = GetCursorPos( );

	bool value_changed = false;

	if ( CalcTextSize( label, 0, 1 ).x > 0 ) {
		PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0, GImGui->Style.ItemInnerSpacing.y } );
		TextEx( label, FindRenderedTextEnd( label ) );
		PopStyleVar( );
	}

	if ( icon ) {
		ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + CalcItemSize( size, CalcItemWidth( ), GetFrameHeight( ) ) };
		GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, GetColorU32( ImGuiCol_FrameBg ), GImGui->Style.FrameRounding );

		if ( GImGui->Style.FrameBorderSize != 0 ) {
			GetWindowDrawList( )->AddRect( bb.Min, bb.Max, GetColorU32( ImGuiCol_Border ), GImGui->Style.FrameRounding );
		}

		ImFont* icon_font = font_manager::get( ).get_fonts( ).at( icons ).get( 14.f );
		if ( !icon_font )
			icon_font = GetFont( );
		GetWindowDrawList( )->AddText( icon_font, icon_font->FontSize, bb.Min + ImVec2{ GImGui->Style.FramePadding.x, bb.GetHeight( ) / 2.f - icon_font->FontSize / 2.f }, GetColorU32( ImGuiCol_TextDisabled ), icon );

		PushStyleColor( ImGuiCol_FrameBg, GetColorU32( ImGuiCol_FrameBg, 0 ) );
		PushStyleColor( ImGuiCol_Border, GetColorU32( ImGuiCol_Border, 0 ) );
		SetCursorPosX( pos.x + 24.f );
		SetCursorPosY( pos.y );
		value_changed = InputTextEx( str_id, lang_manager::get( ).translate( hint ), buf, buf_size, size - ImVec2{ 24, 0 }, 0 );
		PopStyleColor( 2 );
	} else {
		value_changed = InputTextEx( str_id, lang_manager::get( ).translate( hint ), buf, buf_size, size, 0 );
	}

	return value_changed;
}

bool widgets_manager::button( const char* label, ImVec2 size, std::function< void( ) > on_click ) {
	if ( on_click )
		search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, size, on_click]( ) { button( label_copy.c_str( ), size, on_click ); } );

	bool pressed = comp_builder::get( ).button( label, size, [&]( const comp_builder::button_env_t& env ) {
		auto col = col_anim( col_anim( GetColorU32( ImGuiCol_Button ), GetColorU32( ImGuiCol_ButtonHovered ), env.anim.hover ), GetColorU32( ImGuiCol_ButtonActive ), env.anim.held );

		GetWindowDrawList( )->AddRectFilled( env.bb.Min, env.bb.Max, col, GImGui->Style.FrameRounding );
		GetWindowDrawList( )->AddText( env.bb.GetCenter( ) - CalcTextSize( env.label, 0, 1 ) / 2, GetColorU32( ImGuiCol_TextButton ), env.label, FindRenderedTextEnd( env.label ) );
	} );

	if ( pressed && on_click )
		on_click( );

	return pressed;
}

bool widgets_manager::color_edit( const char* label, float col[4] ) {
	search_manager::get( ).add_item( label, [label_copy = std::string( label ), this, col]( ) { color_edit( label_copy.c_str( ), col ); } );

	float square_sz = 14;

	ImRect total_bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + ImVec2{ CalcTextSize( label, 0, 1 ).x > 0 ? CalcItemWidth( ) : square_sz, square_sz } };
	ImRect bb{ total_bb.Max - ImVec2{ square_sz, square_sz }, total_bb.Max };

	bool value_changed = false;

	comp_builder::get( ).color_edit( label, total_bb, bb, col, [&]( comp_builder::color_edit_env_t env ) {
		GetWindowDrawList( )->AddText( { total_bb.Min.x, total_bb.GetCenter( ).y - GImGui->FontSize / 2 }, GetColorU32( ImGuiCol_Text ), env.label, FindRenderedTextEnd( env.label ) );

		GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, ImColor{ col[0], col[1], col[2], GImGui->Style.Alpha }, GImGui->Style.FrameRounding );

		if ( env.anim.open > 0.05f ) {
			SetNextWindowPos( { bb.Min.x, bb.Max.y + 5 } );
			PushStyleVar( ImGuiStyleVar_Alpha, env.anim.open );
			PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 10, 10 } );
			PushStyleVar( ImGuiStyleVar_WindowRounding, GImGui->Style.FrameRounding );
			PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 10, 10 } );
			PushStyleColor( ImGuiCol_WindowBg, GetColorU32( ImGuiCol_FrameBg ) );
			Begin( label, 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize );
			{
				BringWindowToDisplayFront( GetCurrentWindow( ) );
				BringWindowToFocusFront( GetCurrentWindow( ) );

				if ( !IsWindowHovered( ImGuiHoveredFlags_AnyWindow ) && IsMouseClicked( 0 ) && !env.hovered ) {
					env.open = false;
				}

				value_changed = color_picker::get( ).draw( env.label, col );
			}
			End( );
			PopStyleColor( );
			PopStyleVar( 4 );
		}
	} );

	return value_changed;
}

bool widgets_manager::selectable( const char* label, bool selected, ImVec2 size ) {
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + CalcItemSize( size, GetWindowWidth( ), GetFrameHeight( ) ) };
	return comp_builder::get( ).selectable( label, selected, bb, [&]( const comp_builder::selectable_env_t& env ) {
		ImColor col = col_anim( col_anim( GetColorU32( ImGuiCol_Text ), GetColorU32( ImGuiCol_Text, 0.6f ), env.anim.hover ), GetColorU32( ImGuiCol_Scheme ), env.anim.selected );

		GetWindowDrawList( )->AddRectFilled( bb.Min, bb.Max, GetColorU32( ImGuiCol_FrameBg, env.anim.selected ) );
		GetWindowDrawList( )->AddText( { bb.Min.x + GImGui->Style.FramePadding.x, bb.GetCenter( ).y - GImGui->FontSize / 2 }, col, env.label, FindRenderedTextEnd( env.label ) );
	} );
}

void widgets_manager::separator( ) {
	GetWindowDrawList( )->AddRectFilled( { GetWindowPos( ).x, GetCurrentWindow( )->DC.CursorPos.y }, { GetWindowPos( ).x + GetWindowWidth( ), GetCurrentWindow( )->DC.CursorPos.y + 1 }, GetColorU32( ImGuiCol_Separator ) );
	Dummy( { GetWindowWidth( ), 1 } );
}
