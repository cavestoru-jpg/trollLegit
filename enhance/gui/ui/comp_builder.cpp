#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#define _CRT_SECURE_NO_WARNINGS
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <string>
#include <functional>
#include <cmath>
#include <type_traits>
#include <animations.hpp>
#include <xorstr.hpp>
#include "comp_builder.hpp"
#include <windows.h>
#include "widgets_manager.hpp"
#include "font_manager.hpp"
#include "popup_manager.hpp"
#include "lang_manager.hpp"
#include <unicodes.hpp>

using namespace ImGui;

namespace {

std::vector< std::string > keys {
		xorstr_( "NONE" ),
		xorstr_( "M1" ),
		xorstr_( "M2" ),
		xorstr_( "CN" ),
		xorstr_( "M3" ),
		xorstr_( "M4" ),
		xorstr_( "M5" ),
		xorstr_( "NONE" ),
		xorstr_( "BACK" ),
		xorstr_( "TAB" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "BACK" ),
		xorstr_( "ENT" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "SHIFT" ),
		xorstr_( "CTRL" ),
		xorstr_( "ALT" ),
		xorstr_( "PAUSE" ),
		xorstr_( "CAPS" ),
		xorstr_( "KAN" ),
		xorstr_( "NONE" ),
		xorstr_( "JUN" ),
		xorstr_( "FIN" ),
		xorstr_( "KAN" ),
		xorstr_( "NONE" ),
		xorstr_( "ESC" ),
		xorstr_( "CON" ),
		xorstr_( "NCO" ),
		xorstr_( "ACC" ),
		xorstr_( "MAD" ),
		xorstr_( "SPACE" ),
		xorstr_( "PGU" ),
		xorstr_( "PGD" ),
		xorstr_( "END" ),
		xorstr_( "HOME" ),
		xorstr_( "LEFT" ),
		xorstr_( "UP" ),
		xorstr_( "RIGH" ),
		xorstr_( "DOWN" ),
		xorstr_( "SEL" ),
		xorstr_( "PRINT" ),
		xorstr_( "EXE" ),
		xorstr_( "PRINT" ),
		xorstr_( "INS" ),
		xorstr_( "DEL" ),
		xorstr_( "HEL" ),
		xorstr_( "0" ),
		xorstr_( "1" ),
		xorstr_( "2" ),
		xorstr_( "3" ),
		xorstr_( "4" ),
		xorstr_( "5" ),
		xorstr_( "6" ),
		xorstr_( "7" ),
		xorstr_( "8" ),
		xorstr_( "9" ),
		xorstr_( "..." ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "A" ),
		xorstr_( "B" ),
		xorstr_( "C" ),
		xorstr_( "D" ),
		xorstr_( "E" ),
		xorstr_( "F" ),
		xorstr_( "G" ),
		xorstr_( "H" ),
		xorstr_( "I" ),
		xorstr_( "J" ),
		xorstr_( "K" ),
		xorstr_( "L" ),
		xorstr_( "M" ),
		xorstr_( "N" ),
		xorstr_( "O" ),
		xorstr_( "P" ),
		xorstr_( "Q" ),
		xorstr_( "R" ),
		xorstr_( "S" ),
		xorstr_( "T" ),
		xorstr_( "U" ),
		xorstr_( "V" ),
		xorstr_( "W" ),
		xorstr_( "X" ),
		xorstr_( "Y" ),
		xorstr_( "Z" ),
		xorstr_( "WIN" ),
		xorstr_( "WIN" ),
		xorstr_( "APP" ),
		xorstr_( "NONE" ),
		xorstr_( "SLE" ),
		xorstr_( "NUM0" ),
		xorstr_( "NUM1" ),
		xorstr_( "NUM2" ),
		xorstr_( "NUM3" ),
		xorstr_( "NUM4" ),
		xorstr_( "NUM5" ),
		xorstr_( "NUM6" ),
		xorstr_( "NUM7" ),
		xorstr_( "NUM8" ),
		xorstr_( "NUM9" ),
		xorstr_( "*" ),
		xorstr_( "+" ),
		xorstr_( "|" ),
		xorstr_( "-" ),
		xorstr_( "." ),
		xorstr_( "/" ),
		xorstr_( "F1" ),
		xorstr_( "F2" ),
		xorstr_( "F3" ),
		xorstr_( "F4" ),
		xorstr_( "F5" ),
		xorstr_( "F6" ),
		xorstr_( "F7" ),
		xorstr_( "F8" ),
		xorstr_( "F9" ),
		xorstr_( "F10" ),
		xorstr_( "F11" ),
		xorstr_( "F12" ),
		xorstr_( "F13" ),
		xorstr_( "F14" ),
		xorstr_( "F15" ),
		xorstr_( "F16" ),
		xorstr_( "F17" ),
		xorstr_( "F18" ),
		xorstr_( "F19" ),
		xorstr_( "F20" ),
		xorstr_( "F21" ),
		xorstr_( "F22" ),
		xorstr_( "F23" ),
		xorstr_( "F24" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NUMLOCK" ),
		xorstr_( "SCR" ),
		xorstr_( "=" ),
		xorstr_( "MAS" ),
		xorstr_( "TOY" ),
		xorstr_( "OYA" ),
		xorstr_( "OYA" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "NONE" ),
		xorstr_( "SHIFT" ),
		xorstr_( "SHIFT" ),
		xorstr_( "CTRL" ),
		xorstr_( "CTRL" ),
		xorstr_( "ALT" ),
		xorstr_( "ALT" )
	};

bool is_modifier_key( int vk ) {
	return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN
		|| vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU;
}

// only one binder captures at a time, so a single snapshot of the previous key state is enough
bool* binder_prev_down( ) {
	static bool prev[256]{ };
	return prev;
}

}

void comp_builder::empty( const char* label, ImRect total_bb, ImRect bb, std::function< void( const empty_env_t& ) > code ) {
	ImGuiWindow* window = GetCurrentWindow( );
	auto id = window->GetID( label );

	ItemSize( total_bb );
	ItemAdd( total_bb, id );

	bool hovered, held;
	bool pressed = ButtonBehavior( bb, id, &hovered, &held );

	if ( code ) {
		code( empty_env_t {
			id,
			hovered,
			held,
			pressed,
			lang_manager::get( ).translate( label )
		} );
	}
}

bool comp_builder::button( const char* label, ImVec2 size, std::function< void( const button_env_t& ) > code ) {
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + CalcItemSize( size, CalcItemWidth( ), GetFrameHeight( ) ) };
	bool pressed = false;

	empty( label, bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
		}; auto& obj = scoped_anim_obj( label, 4431423, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || env.held );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );

		if ( code ) {
			code( button_env_t {
				bb,
				env.hovered,
				env.held,
				env.label,
				button_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.anim
				}
			} );
		}

		pressed = env.pressed;
	} );

	return pressed;
}

template < typename T >
bool comp_builder::slider( const char* label, T* v, T min, T max, const char* format, ImRect total_bb, ImRect bb, std::function< void( slider_env_t ) > code ) {
	bool result = false;

	empty( label, total_bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
			float val_anim;
			char buf[64];
		}; auto& obj = scoped_anim_obj( label, 32, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || env.held );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );

		const float range = float( max - min );
		if ( range > 0.f )
			obj.val_anim = anim_lerp( obj.val_anim, ( ImClamp( *v, min, max ) - min * 1.f ) / range * bb.GetWidth( ), 17.f );

		if ( env.held ) {
			float ratio = bb.GetWidth( ) > 0.f ? ( GetIO( ).MousePos.x - bb.Min.x ) / bb.GetWidth( ) : 0.f;
			ratio = ImClamp( ratio, 0.f, 1.f );
			float raw = min + ratio * ( max - min );
			if constexpr ( std::is_integral_v<T> )
				*v = ImClamp( T( std::round( raw ) ), min, max );
			else
				*v = ImClamp( T( raw ), min, max );
			result = true;
		}

		ImFormatString( obj.buf, sizeof( obj.buf ), format, *v );

		if ( code ) {
			code( slider_env_t {
				env.hovered,
				env.held,
				obj.buf,
				env.label,
				slider_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.anim,
					obj.val_anim,
				}
			} );
		}
	} );

	return result;
}

template bool comp_builder::slider<int>( const char*, int*, int, int, const char*, ImRect, ImRect, std::function< void( slider_env_t ) > );
template bool comp_builder::slider<float>( const char*, float*, float, float, const char*, ImRect, ImRect, std::function< void( slider_env_t ) > );

bool comp_builder::checkbox( const char* label, bool* v, int* key, float* col, std::function< void( ) > options, bool warning, ImRect total_bb, ImRect bb, ImVec2 options_pos, std::function< void( checkbox_env_t ) > code, float* col2 ) {
	bool pressed = false;

	empty( label, total_bb, total_bb, [&]( const empty_env_t& env ) {
		float h, s1, v1;
		ImColor warning_col;
		ColorConvertRGBtoHSV( GetStyleColorVec4( ImGuiCol_Scheme ).x, GetStyleColorVec4( ImGuiCol_Scheme ).y, GetStyleColorVec4( ImGuiCol_Scheme ).z, h, s1, v1 );
		ColorConvertHSVtoRGB( 0.f, s1, v1, warning_col.Value.x, warning_col.Value.y, warning_col.Value.z );
		warning_col.Value.w = GImGui->Style.Alpha;

		if ( env.pressed ) {
			if ( warning && !*v ) {
				popup_manager::get( ).open_popup( [=]( ) {
					SetCursorPos( GetWindowSize( ) / 2 - ImVec2{ 300, 150 } / 2 );
					BeginChild( xorstr_( "##popup_inner" ), ImVec2{ 300, 150 } );
					{
						ImFont* fnt = fonts[icons].get( 14.f );
						if ( !fnt )
							fnt = GetFont( );
						GetWindowDrawList( )->AddText( fnt, fnt->FontSize, GetWindowPos( ) + GetCursorPos( ) + ImVec2{ 20, 20 }, ImColor{ warning_col.Value.x, warning_col.Value.y, warning_col.Value.z, GImGui->Style.Alpha }, i_alert_triangle );
						GetWindowDrawList( )->AddText( GetWindowPos( ) + GetCursorPos( ) + ImVec2{ 44, 20 }, GetColorU32( ImGuiCol_Text ), lang_manager::get( ).translate( xorstr_( "Are you sure u want to enable it?" ) ) );
						GetWindowDrawList( )->AddText( GetWindowPos( ) + GetCursorPos( ) + ImVec2{ 20, 46 }, GetColorU32( ImGuiCol_TextDisabled ), lang_manager::get( ).translate( xorstr_( "This function is " ) ) );
						GetWindowDrawList( )->AddText( GetWindowPos( ) + GetCursorPos( ) + ImVec2{ 20.f + CalcTextSize( lang_manager::get( ).translate( xorstr_( "This function is " ) ) ).x, 46.f }, GetColorU32( ImGuiCol_Text ), lang_manager::get( ).translate( xorstr_( "dangerous!" ) ) );

						SetCursorPos( ImVec2{ 20.f, 150.f - 20.f - GetFrameHeight( ) } );
						if ( widgets_manager::get( ).button( xorstr_( "YES" ), ImVec2{ 100.f, GetFrameHeight( ) } ) ) {
							*v = true;
							popup_manager::get( ).close_popup( );
						}

						SameLine( 0.f, 10.f );

						PushStyleColor( ImGuiCol_Button, GetColorU32( ImGuiCol_FrameBg ) );
						PushStyleColor( ImGuiCol_ButtonHovered, GetColorU32( ImGuiCol_FrameBgHovered ) );
						PushStyleColor( ImGuiCol_ButtonActive, GetColorU32( ImGuiCol_FrameBgActive ) );
						PushStyleColor( ImGuiCol_TextButton, GetColorU32( ImGuiCol_Text ) );
						if ( widgets_manager::get( ).button( xorstr_( "NO" ), ImVec2{ 260.f - 110.f, GetFrameHeight( ) } ) ) {
							popup_manager::get( ).close_popup( );
						}
						PopStyleColor( 4 );
					}
					EndChild( );
				} );
			} else {
				*v = !*v;
			}
		}

		struct s {
			float anim;
			float hover;
			float held;
			float enabled;
			float key_size;
		}; auto& obj = scoped_anim_obj( label, 443143223, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || *v );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );
		obj.enabled = anim( obj.enabled, 0.f, 1.f, *v );

		if ( code ) {
			code( checkbox_env_t {
				env.hovered,
				env.held,
				env.pressed,
				env.label,
				checkbox_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.enabled,
					obj.anim,
				}
			} );
		}

		if ( warning ) {
			ImFont* fnt = fonts[icons].get( 14.f );
			if ( !fnt )
				fnt = GetFont( );
			GetWindowDrawList( )->AddText( fnt, fnt->FontSize, ImVec2{ total_bb.Min.x - 24.f, total_bb.GetCenter( ).y - 7.f }, warning_col, i_alert_triangle );
		}

		PushItemFlag( ImGuiItemFlags_NoNav, true );

		if ( options ) {
			GetCurrentWindow( )->DC.CursorPos = ImVec2{ options_pos.x - 14.f - ( obj.key_size + 10.f ) * ( bool )key - 24.f * ( bool )col - 24.f * ( bool )col2, bb.GetCenter( ).y - 7.f };
			char temp[256];
			ImFormatString( temp, sizeof( temp ), "##%s options", label );

			open_button( temp, ImVec2{ 14, 14 }, [&]( open_button_env_t env ) {
				ImColor color = col_anim( col_anim( GetColorU32( ImGuiCol_TextDisabled ), GetColorU32( ImGuiCol_TextHovered ), env.anim.hover ), GetColorU32( ImGuiCol_Scheme ), env.anim.open );
				ImFont* fnt = fonts[icons].get( 14.f );
				if ( !fnt )
					fnt = GetFont( );
				GetWindowDrawList( )->AddText( fnt, fnt->FontSize, env.bb.Min, color, i_settings_01 );

				if ( env.anim.open > 0.05f ) {
					PushStyleVar( ImGuiStyleVar_Alpha, env.anim.open );
					PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 12 } );
					PushStyleVar( ImGuiStyleVar_WindowRounding, GImGui->Style.FrameRounding );
					PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 14, 14 } );
					PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.f );
					PushStyleColor( ImGuiCol_WindowBg, GetColorU32( ImGuiCol_FrameBg ) );
					Begin( label, 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove );
					{
						SetWindowPos( ImVec2{ env.bb.Max.x - GetWindowWidth( ), env.bb.Max.y + 20.f - int( 10.f * env.anim.open ) } );
						SetWindowSize( ImVec2{ 200.f, GetCurrentWindow( )->ContentSize.y + GImGui->Style.WindowPadding.y * 2 } );

						BringWindowToDisplayFront( GetCurrentWindow( ) );
						BringWindowToFocusFront( GetCurrentWindow( ) );

						if ( !IsWindowHovered( ImGuiHoveredFlags_AnyWindow ) && IsMouseClicked( 0 ) && !env.hovered ) {
							env.open = false;
						}

						PushStyleColor( ImGuiCol_FrameBg, GetColorU32( ImGuiCol_FrameBgHovered ) );
						PushItemWidth( GetWindowWidth( ) - GImGui->Style.WindowPadding.x * 2 );
						PushItemFlag( ImGuiItemFlags_NoNav, true );

						options( );

						PopItemFlag( );
						PopItemWidth( );
						PopStyleColor( );
					}
					End( );
					PopStyleColor( );
					PopStyleVar( 5 );
				}
			} );
		}

		if ( col ) {
			GetCurrentWindow( )->DC.CursorPos = ImVec2{ options_pos.x - 14.f - ( obj.key_size + 10.f ) * ( bool )key - 24.f * ( bool )col2, bb.GetCenter( ).y - 7.f };
			char color_label[256];
			ImFormatString( color_label, sizeof( color_label ), "##%s color", label );
			widgets_manager::get( ).color_edit( color_label, col );
		}

		if ( col2 ) {
			GetCurrentWindow( )->DC.CursorPos = ImVec2{ options_pos.x - 14.f - ( obj.key_size + 10.f ) * ( bool )key, bb.GetCenter( ).y - 7.f };
			char color2_label[256];
			ImFormatString( color2_label, sizeof( color2_label ), "##%s color2", label );
			widgets_manager::get( ).color_edit( color2_label, col2 );
		}

		if ( key ) {
			const char* key_text = ( *key >= 0 && *key < ( int )keys.size( ) ) ? keys[*key].c_str( ) : keys[0].c_str( );
			ImFont* key_fnt = fonts[font].get( 12.f );
			if ( !key_fnt )
				key_fnt = GetFont( );
			obj.key_size = anim_lerp( obj.key_size, key_fnt->CalcTextSizeA( key_fnt->FontSize, FLT_MAX, -1, key_text ).x, 24.f );

			GetCurrentWindow( )->DC.CursorPos = ImVec2{ options_pos.x - 32.f - obj.key_size, bb.GetCenter( ).y - 12.f };
			char bind_label[256];
			ImFormatString( bind_label, sizeof( bind_label ), "##%s bind", label );
			widgets_manager::get( ).binder( bind_label, key );
		}

		PopItemFlag( );

		pressed = env.pressed;
	} );

	return pressed;
}

void comp_builder::combo( const char* label, ImRect total_bb, ImRect bb, std::function< void( combo_env_t ) > code ) {
	empty( label, total_bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
			float open_anim;
			bool open;
		}; auto& obj = scoped_anim_obj( label, 21231, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || obj.open );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );
		obj.open_anim = anim( obj.open_anim, 0.f, 1.f, obj.open );

		if ( env.pressed ) {
			obj.open = !obj.open;
		}

		if ( code ) {
			code( combo_env_t {
				env.hovered,
				env.held,
				env.pressed,
				obj.open,
				env.label,
				combo_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.open_anim,
					obj.anim
				}
			} );
		}
	} );
}

bool comp_builder::selectable( const char* label, bool selected, ImRect bb, std::function< void( const selectable_env_t& ) > code ) {
	bool pressed = false;

	empty( label, bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
			float selected;
		}; auto& obj = scoped_anim_obj( label, 231, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || selected );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );
		obj.selected = anim( obj.selected, 0.f, 1.f, selected );

		if ( code ) {
			code( selectable_env_t {
				env.hovered,
				env.held,
				env.pressed,
				env.label,
				selectable_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.selected,
					obj.anim
				}
			} );
		}

		pressed = env.pressed;
	} );

	return pressed;
}

void comp_builder::color_edit( const char* label, ImRect total_bb, ImRect bb, float col[4], std::function< void( color_edit_env_t ) > code ) {
	empty( label, total_bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
			float open_anim;
			bool open;
		}; auto& obj = scoped_anim_obj( label, 23321, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || obj.open );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );
		obj.open_anim = anim( obj.open_anim, 0.f, 1.f, obj.open );

		if ( env.pressed ) {
			obj.open = !obj.open;
		}

		if ( code ) {
			code( color_edit_env_t {
				env.hovered,
				env.held,
				env.pressed,
				obj.open,
				env.label,
				color_edit_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.open_anim,
					obj.anim
				}
			} );
		}
	} );
}

void comp_builder::binder( const char* label, int* key, std::function< void( const binder_env_t& ) > code ) {
	struct s {
		float val_anim;
	}; auto& obj2 = scoped_anim_obj( label, 21131, s{ } );

	ImFont* fnt = fonts[fonts_e::font].get( 12.f );
	if ( !fnt )
		fnt = GetFont( );
	const char* key_text = ( *key >= 0 && *key < ( int )keys.size( ) ) ? keys[*key].c_str( ) : keys[0].c_str( );
	obj2.val_anim = anim_lerp( obj2.val_anim, fnt->CalcTextSizeA( 12.f, FLT_MAX, -1, key_text ).x, 24.f );

	ImRect total_bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + ImVec2{ CalcTextSize( label, 0, 1 ).x > 0 ? CalcItemWidth( ) : 32.f + obj2.val_anim, 24.f } };
	ImRect bb{ total_bb.Max - ImVec2{ 32.f + obj2.val_anim, 24.f }, total_bb.Max };

	empty( label, total_bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
			float active;
			bool is_active;
		}; auto& obj = scoped_anim_obj( label, 21132, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || IsItemActive( ) );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );
		obj.active = anim( obj.active, 0.f, 1.f, obj.is_active );

		const bool SHOULD_EDIT = env.hovered && GetIO( ).MouseClicked[0] && !obj.is_active;

		if ( SHOULD_EDIT ) {
			memset( GetIO( ).MouseDown, 0, sizeof( GetIO( ).MouseDown ) );
			*key = 58;
			obj.is_active = true;

			bool* prev_down = binder_prev_down( );
			for ( auto i = VK_BACK; i <= VK_RMENU; i++ )
				prev_down[i] = ( GetAsyncKeyState( i ) & 0x8000 ) != 0;
		}

		bool value_changed = false;
		int k = *key;

		if ( obj.is_active ) {
			// esc / del clear the bind instead of being bound themselves ( 0 == "NONE" == no key required )
			if ( ( GetAsyncKeyState( VK_ESCAPE ) & 0x8000 ) || ( GetAsyncKeyState( VK_DELETE ) & 0x8000 ) ) {
				k = 0;
				value_changed = true;
				obj.is_active = false;
			}

			for ( auto i = 0; i < 5 && !value_changed; i++ ) {
				if ( GetIO( ).MouseDown[i] ) {
					switch ( i ) {
					case 0:
						k = VK_LBUTTON;
						break;
					case 1:
						k = VK_RBUTTON;
						break;
					case 2:
						k = VK_MBUTTON;
						break;
					case 3:
						k = VK_XBUTTON1;
						break;
					case 4:
						k = VK_XBUTTON2;
					}
					value_changed = true;
					obj.is_active = false;
				}
			}

			if ( !value_changed ) {
				bool* prev_down = binder_prev_down( );
				int pressed_key = 0, pressed_mod = 0;

				for ( auto i = VK_BACK; i <= VK_RMENU; i++ ) {
					// esc / del clear the bind above; never let them be bound
					if ( i == VK_ESCAPE || i == VK_DELETE )
						continue;

					const bool down = ( GetAsyncKeyState( i ) & 0x8000 ) != 0;
					const bool went_down = down && !prev_down[i];
					prev_down[i] = down;

					if ( !went_down )
						continue;

					if ( is_modifier_key( i ) ) {
						if ( !pressed_mod )
							pressed_mod = i;
					} else if ( !pressed_key ) {
						pressed_key = i;
					}
				}

				if ( pressed_key || pressed_mod ) {
					k = pressed_key ? pressed_key : pressed_mod;
					value_changed = true;
					obj.is_active = false;
				}
			}

		}

		*key = k;

		if ( code ) {
			code( binder_env_t {
				total_bb,
				bb,

				env.hovered,
				env.held,
				env.pressed,
				obj.is_active,
				env.label,

				keys,

				binder_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.active,
					obj.anim
				}
			} );
		}
	} );
}

bool comp_builder::open_button( const char* str_id, ImVec2 size, std::function< void( open_button_env_t ) > code ) {
	ImRect bb{ GetCurrentWindow( )->DC.CursorPos, GetCurrentWindow( )->DC.CursorPos + size };
	bool pressed = false;

	empty( str_id, bb, bb, [&]( const empty_env_t& env ) {
		struct s {
			float anim;
			float hover;
			float held;
			float open_anim;
			bool open;
		}; auto& obj = scoped_anim_obj( str_id, 4431423123, s{ } );

		obj.anim = anim( obj.anim, 0.f, 1.f, env.hovered || env.held );
		obj.hover = anim( obj.hover, 0.f, 1.f, env.hovered );
		obj.held = anim( obj.held, 0.f, 1.f, env.held );
		obj.open_anim = anim( obj.open_anim, 0.f, 1.f, obj.open );

		if ( env.pressed ) {
			obj.open = !obj.open;
		}

		if ( code ) {
			code( open_button_env_t {
				bb,
				env.hovered,
				env.held,
				env.pressed,
				obj.open,
				open_button_env_t::anim_t {
					obj.hover,
					obj.held,
					obj.anim,
					obj.open_anim
				}
			} );
		}

		pressed = env.pressed;
	} );

	return pressed;
}
