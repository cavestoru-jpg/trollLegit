#pragma once

#include <unordered_map>
#include <cmath>
#include "imgui.h"
#include "imgui_internal.h"

inline float get_anim_dt( ) {
	return ImClamp( ImGui::GetIO( ).DeltaTime, 0.004f, 0.033f );
}

inline float anim_lerp( float v, float target, float speed ) {
	float dt = get_anim_dt( );
	float diff = target - v;
	float t = 1.f - expf( -speed * dt );
	float result = v + diff * t;
	if ( ImAbs( target - result ) < 0.5f )
		result = target;
	return result;
}

inline ImVec2 anim_lerp( ImVec2 v, ImVec2 target, float speed ) {
	float dt = get_anim_dt( );
	float t = 1.f - expf( -speed * dt );
	ImVec2 result{ v.x + ( target.x - v.x ) * t, v.y + ( target.y - v.y ) * t };
	if ( ImAbs( result.x - target.x ) < ImAbs( target.x - v.x ) * 0.15f + 0.001f ) result.x = target.x;
	if ( ImAbs( result.y - target.y ) < ImAbs( target.y - v.y ) * 0.15f + 0.001f ) result.y = target.y;
	return result;
}

template < typename T >
inline T& anim_obj( const char* id, int seed, T arg ) {
	ImGuiID im_id = ImHashStr( id, 0, seed );

	static std::unordered_map< ImGuiID, T > map;
	auto result = map.find( im_id );

	if ( result == map.end( ) ) {
		map.insert( { im_id, arg } );
		result = map.find( im_id );
	}

	return result->second;
}

template < typename T >
inline T anim( T v, T min, T max, bool state, float speed = 14.f ) {
	T target = state ? max : min;
	float dt = get_anim_dt( );
	float diff = target - v;
	float t = 1.f - expf( -speed * dt );
	T result = v + diff * (T)t;
	if ( ImAbs( (float)( target - result ) ) < 0.01f )
		result = target;
	return result;
}

inline ImColor col_anim( ImColor inactive, ImColor active, float anim ) {
	return ImGui::ColorConvertFloat4ToU32( ImLerp( inactive.Value, active.Value, anim ) );
}
