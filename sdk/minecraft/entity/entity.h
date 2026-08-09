#pragma once

#include <sdk/includes.h>
#include <string>

namespace sdk
{
	class entity_client
	{
	private:
		jobject entity;

	public:
		entity_client(jobject entity);
		~entity_client();

		jobject get_entity();
		jobject get_bounding_box();
		void set_bounding_box(jobject box);
		bool is_same_object(jobject other);
		double get_x();
		double get_y();
		double get_z();
		float get_yaw();
		float get_pitch();
		void set_yaw(float yaw);
		void set_pitch(float pitch);
		bool is_on_ground();
		double get_fall_distance();
		jobject get_velocity();

		// === Killaura extensions ===

		// LivingEntity
		float get_health();
		float get_max_health();
		bool is_alive();
		bool is_using_item();
		bool is_blocking();
		bool is_sprinting();
		int get_hurt_time();

		// Body / head yaw (LivingEntity field_6283 / field_6241)
		float get_body_yaw();
		void set_body_yaw(float v);
		float get_head_yaw();
		void set_head_yaw(float v);

		// Type checks via IsInstanceOf — fast filters for target selector
		bool is_player_class();
		bool is_mob_class();
		bool is_animal_class();

		// Name resolved through getName().getString()
		std::string get_name_string();
	};
}

