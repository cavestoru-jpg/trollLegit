#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstring>
#include <imgui.h>

// Repairs a font whose hhea vertical metrics are nonsense, in place, on a
// private copy of the file.
//
// stb_truetype — and therefore ImGui — sizes a font by hhea's ascent-to-descent
// span, not by the em box, and it also positions every glyph relative to the
// ascent it derives from those same numbers. A normal font has a span of about
// 1.2 em and everything lines up.
//
// The embedded icon font does not. Glyphter exported it with unitsPerEm 1000
// but an hhea span of 3762 — nearly four times the em box, and flatly
// contradicted by its own OS/2 metrics of 800/-200. That single bad number
// causes BOTH symptoms: glyphs rasterise at roughly a fifth of the requested
// size, and because the derived ascent is ~47 px at a nominal 14 px, they are
// also flung far below where they were asked to be.
//
// Scaling the bake size fixes the first and worsens the second, so the metrics
// themselves have to be corrected. OS/2's typo values are the font's own honest
// numbers; failing that, fall back to the usual 0.8/-0.2 em.
// Returns true when it changed something.
inline bool ttf_repair_vmetrics( std::vector< unsigned char >& d ) {
	const size_t n = d.size( );
	auto u16 = [&]( size_t o ) -> unsigned { return ( d[o] << 8 ) | d[o + 1]; };
	auto s16 = [&]( size_t o ) -> int { int v = (int)u16( o ); return v >= 0x8000 ? v - 0x10000 : v; };
	auto u32 = [&]( size_t o ) -> unsigned {
		return ( (unsigned)d[o] << 24 ) | ( d[o + 1] << 16 ) | ( d[o + 2] << 8 ) | d[o + 3];
	};
	auto w16 = [&]( size_t o, int v ) {
		const unsigned uv = (unsigned)( v < 0 ? v + 0x10000 : v );
		d[o] = (unsigned char)( ( uv >> 8 ) & 0xFF );
		d[o + 1] = (unsigned char)( uv & 0xFF );
	};

	if ( n < 12 )
		return false;

	const unsigned num_tables = u16( 4 );
	size_t head = 0, hhea = 0, os2 = 0;

	for ( unsigned i = 0; i < num_tables; ++i ) {
		const size_t rec = 12 + i * 16;
		if ( rec + 16 > n )
			return false;
		const unsigned off = u32( rec + 8 );
		if ( !memcmp( d.data( ) + rec, "head", 4 ) ) head = off;
		else if ( !memcmp( d.data( ) + rec, "hhea", 4 ) ) hhea = off;
		else if ( !memcmp( d.data( ) + rec, "OS/2", 4 ) ) os2 = off;
	}

	if ( !head || !hhea || head + 20 > n || hhea + 10 > n )
		return false;

	const int upem = (int)u16( head + 18 );
	const int ascent = s16( hhea + 4 );
	const int descent = s16( hhea + 6 );
	const int span = ascent - descent;

	if ( upem <= 0 || span <= 0 )
		return false;

	// Anything up to twice the em box is a normal, if generous, font. Leave it.
	if ( span <= upem * 2 )
		return false;

	int new_asc = ( upem * 4 ) / 5;      // 0.8 em
	int new_desc = -( upem / 5 );        // -0.2 em

	if ( os2 && os2 + 74 <= n ) {
		const int typo_asc = s16( os2 + 68 );
		const int typo_desc = s16( os2 + 70 );
		if ( typo_asc > 0 && typo_asc - typo_desc > 0 && typo_asc - typo_desc <= upem * 2 ) {
			new_asc = typo_asc;
			new_desc = typo_desc;
		}
	}

	w16( hhea + 4, new_asc );
	w16( hhea + 6, new_desc );
	w16( hhea + 8, 0 );                  // lineGap
	return true;
}

struct font_t {
	std::vector< ImFont* > fonts;
	std::vector< float >   sizes;
	// The atlas is told it does not own the data, so this copy has to outlive
	// it. font_manager is a singleton, so it does.
	std::vector< unsigned char > data_copy;

	void setup( unsigned char* data, size_t data_size, std::vector< float > px, const ImWchar* ranges ) {
		data_copy.assign( data, data + data_size );
		ttf_repair_vmetrics( data_copy );

		auto config = ImFontConfig( );
		config.FontDataOwnedByAtlas = false;
		config.FontBuilderFlags = 0;

		for ( auto& sz : px ) {
			auto new_font = ImGui::GetIO( ).Fonts->AddFontFromMemoryTTF(
				data_copy.data( ), (int)data_copy.size( ), sz, &config, ranges );
			if ( new_font ) {
				fonts.push_back( new_font );
				sizes.push_back( sz );
			}
		}
	}

	// Nearest baked size, preferring one at or above the request. Drawing a 12px
	// atlas at 30px is what made scaled ESP text mushy; picking a bigger atlas
	// and letting ImGui scale it down stays sharp.
	ImFont* get( float size ) {
		if ( fonts.empty( ) )
			return nullptr;

		int best = -1;
		float best_score = FLT_MAX;
		for ( size_t i = 0; i < fonts.size( ); ++i ) {
			if ( !fonts[i] )
				continue;
			const float d = sizes[i] - size;
			const float score = d >= 0.f ? d : -d * 4.f;   // upscaling costs more
			if ( score < best_score ) {
				best_score = score;
				best = static_cast<int>( i );
			}
		}

		return best >= 0 ? fonts[best] : nullptr;
	}
};

class font_manager {
	std::vector< font_t > m_fonts;
public:
	static font_manager& get( ) {
		static font_manager s{ };
		return s;
	}

	auto& get_fonts( ) {
		return m_fonts;
	}
};

enum fonts_e {
	font,
	icons,
	fonts_size
};

static std::vector< font_t >& fonts = font_manager::get( ).get_fonts( );
