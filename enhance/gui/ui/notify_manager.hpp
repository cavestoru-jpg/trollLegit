#pragma once
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <mutex>
#include <imgui.h>
#include <imgui_internal.h>
#include <unicodes.hpp>
#include <xorstr.hpp>
#include "font_manager.hpp"
#include "lang_manager.hpp"
#include "../core/blur.h"

class notify_manager {
public:
	enum notify_status {
		notify_success,
		notify_error,
		notify_info,
	};

	struct c_notify {
		std::string message;
		notify_status status;
		float duration = 3.f;
		float time = 0.f;
		ImVec2 pos{ 0, 0 };
		float fade_time = 0.25f;
	};

	std::vector< c_notify > notifications;
	std::recursive_mutex notifications_mutex;

	void erase_notifies( ) {
		std::scoped_lock lock( notifications_mutex );
		notifications.erase(
			std::remove_if( notifications.begin( ), notifications.end( ), []( const c_notify& n ) {
				return n.time >= n.duration;
			} ),
			notifications.end( )
		);
	}

	void add( const std::string& message, notify_status status, float duration = 3.f ) {
		std::scoped_lock lock( notifications_mutex );
		notifications.emplace_back( c_notify{ message, status, duration } );
	}

	void draw( ) {
		std::scoped_lock lock( notifications_mutex );
		auto draw_list = ImGui::GetBackgroundDrawList( );

		float offset = 0.f;
		for ( int i = 0; i < notifications.size( ); ++i ) {
			auto& n = notifications[i];
			float alpha = n.time <= n.fade_time ? n.time / n.fade_time : n.time >= n.duration - n.fade_time ? ( n.duration - n.time ) / n.fade_time : 1.f;

			const auto status = std::clamp( n.status, notify_success, notify_info );

			const char* titles[] {
				"SUCCESS",
				"ERROR",
				"INFO",
			};

			const char* n_icons[] = {
				i_check_circle,
				i_alert_circle,
				i_info_circle,
			};

			ImColor colors[3];

			colors[0] = ImGui::GetColorU32( ImGui::GetStyleColorVec4( ImGuiCol_Scheme ) );
			colors[0].Value.w = alpha;

			float h, s, v;
			ImGui::ColorConvertRGBtoHSV( ImGui::GetStyleColorVec4( ImGuiCol_Scheme ).x, ImGui::GetStyleColorVec4( ImGuiCol_Scheme ).y, ImGui::GetStyleColorVec4( ImGuiCol_Scheme ).z, h, s, v );
			ImGui::ColorConvertHSVtoRGB( 0.f, s, v, colors[1].Value.x, colors[1].Value.y, colors[1].Value.z );
			colors[1].Value.w = alpha;

			ImGui::ColorConvertRGBtoHSV( ImGui::GetStyleColorVec4( ImGuiCol_Scheme ).x, ImGui::GetStyleColorVec4( ImGuiCol_Scheme ).y, ImGui::GetStyleColorVec4( ImGuiCol_Scheme ).z, h, s, v );
			ImGui::ColorConvertHSVtoRGB( 0.63f, s, v, colors[2].Value.x, colors[2].Value.y, colors[2].Value.z );
			colors[2].Value.w = alpha;

			ImVec2 size{ ImMax( ImGui::CalcTextSize( lang_manager::get( ).translate( n.message.c_str( ) ) ).x, ImGui::CalcTextSize( lang_manager::get( ).translate( titles[status] ) ).x + 24.f ) + 28.f, GImGui->FontSize * 2 + 38.f };

			if ( n.pos.x == 0 ) n.pos = ImGui::GetIO( ).DisplaySize - ImVec2{ 0, 20.f + offset + size.y };

			n.pos.x = anim_lerp( n.pos.x, ImGui::GetIO( ).DisplaySize.x - 20.f - size.x, 14.f );
			n.pos.y = anim_lerp( n.pos.y, ImGui::GetIO( ).DisplaySize.y - 20.f - offset - size.y, 14.f );

			ImBlur::AddBlurToDrawList( draw_list, n.pos, size, 3.f, alpha );
			draw_list->AddRectFilled( n.pos, n.pos + size, ImGui::GetColorU32( ImGuiCol_WindowBg, alpha * 0.65f ), 3.f );

			draw_list->AddText( fonts[icons].get( 14 ), 14.f, n.pos + ImVec2{ 14.f, 14.f }, colors[status], n_icons[status] );
			draw_list->AddText( n.pos + ImVec2{ 38.f, 14.f }, ImGui::GetColorU32( ImGuiCol_Text, alpha ), lang_manager::get( ).translate( titles[status] ) );
			draw_list->AddText( n.pos + ImVec2{ 14.f, 24.f + GImGui->FontSize }, ImGui::GetColorU32( ImGuiCol_TextDisabled, alpha ), lang_manager::get( ).translate( n.message.c_str( ) ) );

			n.time += ImGui::GetIO( ).DeltaTime;
			offset += size.y + 12.f;
		}

		erase_notifies( );
	}

	static notify_manager& get( ) {
		static notify_manager s{ };
		return s;
	}
};
