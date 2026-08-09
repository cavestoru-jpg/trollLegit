#pragma once
#include <functional>
#include <vector>
#include <string>
#include <animations.hpp>

// anim_obj keys state by label alone, so same-label widgets in different
// windows would share it; scope the key to the current window's ID stack
template < typename T >
inline T& scoped_anim_obj( const char* id, int seed, T arg ) {
	return anim_obj( id, seed ^ ( int )ImGui::GetCurrentWindow( )->GetID( id ), arg );
}

class comp_builder {
public:
	struct empty_env_t {
		ImGuiID& id;
		bool hovered;
		bool held;
		bool pressed;
		const char* label;
	};

	void empty( const char* label, ImRect total_bb, ImRect bb, std::function< void( const empty_env_t& ) > code );

	struct button_env_t {
		ImRect bb;

		bool hovered;
		bool held;
		const char* label;

		struct anim_t {
			float hover;
			float held;
			float anim;
		} anim;
	};

	bool button( const char* label, ImVec2 size, std::function< void( const button_env_t& ) > code );

	struct slider_env_t {
		bool hovered;
		bool held;
		char* buf;
		const char* label;

		struct anim_t {
			float hover;
			float held;
			float anim;
			float val_anim;
		} anim;
	};

	template < typename T >
	bool slider( const char* label, T* v, T min, T max, const char* format, ImRect total_bb, ImRect bb, std::function< void( slider_env_t ) > code );

	struct checkbox_env_t {
		bool hovered;
		bool held;
		bool pressed;
		const char* label;

		struct anim_t {
			float hover;
			float held;
			float enabled;
			float anim;
		} anim;
	};

	bool checkbox( const char* label, bool* v, int* key, float* col, std::function< void( ) > options, bool warning, ImRect total_bb, ImRect bb, ImVec2 options_pos, std::function< void( checkbox_env_t ) > code, float* col2 = nullptr );

	struct combo_env_t {
		bool hovered;
		bool held;
		bool pressed;
		bool& open;
		const char* label;

		struct anim_t {
			float hover;
			float held;
			float open;
			float anim;
		} anim;
	};

	void combo( const char* label, ImRect total_bb, ImRect bb, std::function< void( combo_env_t ) > code );

	struct selectable_env_t {
		bool hovered;
		bool held;
		bool pressed;
		const char* label;

		struct anim_t {
			float hover;
			float held;
			float selected;
			float anim;
		} anim;
	};

	bool selectable( const char* label, bool selected, ImRect bb, std::function< void( const selectable_env_t& ) > code );

	struct color_edit_env_t {
		bool hovered;
		bool held;
		bool pressed;
		bool& open;
		const char* label;

		struct anim_t {
			float hover;
			float held;
			float open;
			float anim;
		} anim;
	};

	void color_edit( const char* label, ImRect total_bb, ImRect bb, float col[4], std::function< void( color_edit_env_t ) > code );

	struct binder_env_t {
		ImRect total_bb;
		ImRect bb;

		bool hovered;
		bool held;
		bool pressed;
		bool active;
		const char* label;

		const std::vector< std::string >& keys;

		struct anim_t {
			float hover;
			float held;
			float active;
			float anim;
		} anim;
	};

	void binder( const char* label, int* key, std::function< void( const binder_env_t& ) > code );

	struct open_button_env_t {
		ImRect bb;
		bool hovered;
		bool held;
		bool pressed;
		bool& open;

		struct anim_t {
			float hover;
			float held;
			float anim;
			float open;
		} anim;
	};

	bool open_button( const char* str_id, ImVec2 size, std::function< void( open_button_env_t ) > code );

	static comp_builder& get( ) {
		static comp_builder s{ };
		return s;
	}
};
