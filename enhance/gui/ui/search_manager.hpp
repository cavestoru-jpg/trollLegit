#pragma once
#include <string>
#include <vector>
#include <functional>
#include <utility>
#include <mutex>

#include <algorithm>
#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unicodes.hpp>
#include "font_manager.hpp"
#include "lang_manager.hpp"
#include "tabs_manager.hpp"
#include "child_manager.hpp"
#include "widgets_manager.hpp"

using namespace ImGui;

struct search_item {
	std::string label;
	std::string lowered;
	int lowered_lang;
	int tab;
	int subtab;
	std::string child;
	std::function< void( ) > code;
};

class search_manager {
	float m_anim = 0.f, m_anim_dest = 1.f;
	std::vector< search_item > m_items;
public:
	char search_buf[64]{ };

	template < typename F >
	void add_item( const char* label, F&& code ) {
		if ( std::find_if( m_items.begin( ), m_items.end( ), [&]( const search_item& it ) { return it.label == label; } ) != m_items.end( ) || ( GImGui->CurrentItemFlags & ImGuiItemFlags_NoNav ) )
			return;

		m_items.push_back( search_item{ label, lowered_label( label ), lang_manager::get( ).get_lang( ), tabs_manager::get( ).current, tabs_manager::get( ).get_tab( ).cur, child_manager::get( ).get_current( ), std::forward< F >( code ) } );
	}

	std::string to_lower( const char* str ) {
		if ( !str || !*str ) return {};

		int wlen = MultiByteToWideChar( CP_UTF8, 0, str, -1, nullptr, 0 );
		if ( wlen <= 0 ) return str;

		std::wstring wstr( wlen, L'\0' );
		MultiByteToWideChar( CP_UTF8, 0, str, -1, wstr.data( ), wlen );
		wstr.resize( wcslen( wstr.c_str( ) ) );

		DWORD lowered_len = LCMapStringW( LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, wstr.c_str( ), (int)wstr.size( ), nullptr, 0 );
		if ( lowered_len <= 0 ) return str;

		std::wstring lowered( lowered_len, L'\0' );
		LCMapStringW( LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, wstr.c_str( ), (int)wstr.size( ), lowered.data( ), lowered_len );

		int blen = WideCharToMultiByte( CP_UTF8, 0, lowered.c_str( ), (int)lowered.size( ), nullptr, 0, nullptr, nullptr );
		if ( blen <= 0 ) return str;

		std::string result( blen, '\0' );
		WideCharToMultiByte( CP_UTF8, 0, lowered.c_str( ), (int)lowered.size( ), result.data( ), blen, nullptr, nullptr );
		return result;
	}

	// the "##ID" part is never rendered, so it must not take part in matching either
	std::string lowered_label( const char* label ) {
		const char* translated = lang_manager::get( ).translate( label );
		const char* id = strstr( translated, "##" );

		return to_lower( id ? std::string( translated, id ).c_str( ) : translated );
	}

	bool compare( const char* str, const std::string& lowered_substr ) {
		return strstr( to_lower( str ).c_str( ), lowered_substr.c_str( ) );
	}

	void update( ) {
		m_anim_dest = 0.f;
	}

	float& get_anim( ) {
		return m_anim;
	}

	void draw( ) {
		std::vector< search_item* > res;

		static std::string buf;

		const std::string lowered_buf = to_lower( buf.c_str( ) );
		const int lang = lang_manager::get( ).get_lang( );

		for ( auto& item : m_items ) {
			if ( item.lowered_lang != lang ) {
				item.lowered = lowered_label( item.label.c_str( ) );
				item.lowered_lang = lang;
			}

			if ( strstr( item.lowered.c_str( ), lowered_buf.c_str( ) ) ) {
				res.push_back( &item );
			}
		}

		m_anim = anim_lerp( m_anim, m_anim_dest, 28.f );

		if ( m_anim < 0.05f ) {
			if ( strlen( search_buf ) > 0 ) m_anim_dest = 1.f;
			buf = search_buf;
		}

		tabs_manager::get( ).get_tabs_anim( ) = 0.f;

		PushStyleVar( ImGuiStyleVar_Alpha, m_anim * GImGui->Style.Alpha );
		BeginGroup( );
		{
			PushItemWidth( GetWindowWidth( ) - 28.f );

			for ( int i = 0; i < res.size( ); ++i ) {
				auto& item = *res[i];

				TextDisabled( lang_manager::get( ).translate( tabs_manager::get( ).get_tab( item.tab ).label.c_str( ) ) );
				SameLine( 0, 10.f );
				PushFont( fonts[icons].get( 14 ) );
				TextDisabled( i_chevron_right );
				PopFont( );
				SameLine( 0, 24.f );
				auto& subtabs = tabs_manager::get( ).get_tab( item.tab ).subtabs;
				if ( item.subtab >= 0 && item.subtab < ( int )subtabs.size( ) ) {
					TextDisabled( subtabs[item.subtab].c_str( ) );
					SameLine( 0, 10.f );
					PushFont( fonts[icons].get( 14 ) );
					TextDisabled( i_chevron_right );
					PopFont( );
					SameLine( 0, 24.f );
				}
				TextEx( item.child.c_str( ), FindRenderedTextEnd( item.child.c_str( ) ) );

				item.code( );

				if ( i < res.size( ) - 1 )
					widgets_manager::get( ).separator( );
			}

			PopItemWidth( );
		}
		EndGroup( );
		PopStyleVar( );
	}

	static search_manager& get( ) {
		static search_manager s;
		return s;
	}
};
