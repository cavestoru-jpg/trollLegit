#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <xorstr.hpp>
#include "font_manager.hpp"

class lang_manager {
	int m_lang = 0;

	struct lang_t {
		std::string label;
		int font;
		std::unordered_map< std::string, std::string > dict{ };
	};

	std::vector< lang_t > m_languages;

	int clamped_lang( ) const {
		return m_languages.empty( ) ? 0 : std::clamp( m_lang, 0, ( int )m_languages.size( ) - 1 );
	}
public:
	void add_language( const char* label, int font, std::unordered_map< std::string, std::string > dict ) {
		m_languages.push_back( lang_t{ label, font, std::move( dict ) } );
	}

	void add_language( const char* label, int font, std::initializer_list<std::pair<std::string, std::string>> dict ) {
		std::unordered_map< std::string, std::string > map;
		for ( const auto& p : dict )
			map[ p.first ] = p.second;
		m_languages.push_back( lang_t{ label, font, std::move( map ) } );
	}

	int& get_lang( ) {
		return m_lang;
	}

	const char* get_lang_name( ) {
		if ( m_languages.empty( ) )
			return "";
		return m_languages[ clamped_lang( ) ].label.c_str( );
	}

	std::vector< lang_t >& get_langs( ) {
		return m_languages;
	}

	int get_font( ) {
		if ( m_languages.empty( ) )
			return 0;
		return ( int )m_languages[ clamped_lang( ) ].font;
	}

	ImFont* get_font( int sz ) {
		if ( m_languages.empty( ) )
			return nullptr;
		int fnt = m_languages[ clamped_lang( ) ].font;
		if ( fnt < 0 || fnt >= ( int )fonts.size( ) )
			return nullptr;
		return fonts[ fnt ].get( sz );
	}

	void set_lang( int i ) {
		m_lang = m_languages.empty( ) ? 0 : std::clamp( i, 0, ( int )m_languages.size( ) - 1 );
	}

	static lang_manager& get( ) {
		static lang_manager s;
		return s;
	}

	const char* translate( const char* str ) {
		if ( !str ) return "";
		if ( m_lang == 0 || m_languages.empty( ) )
			return str;

		auto it = m_languages[ clamped_lang( ) ].dict.find( str );
		if ( it == m_languages[ clamped_lang( ) ].dict.end( ) ) return str;

		return it->second.c_str( );
	}

	void init( ) {
		add_language( xorstr_( "English" ), font, { } );
		add_language( xorstr_( "Russian" ), font, {
			{ xorstr_( "Enable" ), xorstr_( "Включить" ) },
			{ xorstr_( "Search..." ), xorstr_( "Поиск..." ) },
			{ xorstr_( "Settings" ), xorstr_( "Настройки" ) },
			{ xorstr_( "SUCCESS" ), xorstr_( "Успех" ) },
			{ xorstr_( "ERROR" ), xorstr_( "Ошибка" ) },
			{ xorstr_( "INFO" ), xorstr_( "Информация" ) },
			{ xorstr_( "Visualization" ), xorstr_( "Визуализация" ) },
			{ xorstr_( "Configs" ), xorstr_( "Конфигурации" ) },
			{ xorstr_( "General" ), xorstr_( "Основное" ) },
			{ xorstr_( "Are you sure u want to enable it?" ), xorstr_( "Вы уверены, что хотите включить это?" ) },
			{ xorstr_( "This function is " ), xorstr_( "Эта функция " ) },
			{ xorstr_( "dangerous!" ), xorstr_( "опасна!" ) },
			{ xorstr_( "YES" ), xorstr_( "Да" ) },
			{ xorstr_( "NO" ), xorstr_( "Нет" ) },
		} );
	}
};
