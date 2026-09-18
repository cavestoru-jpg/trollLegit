#include "compat.h"

#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <enhance/utils/logger.h>

#include <cstring>
#include <string>

namespace sdk
{
	namespace compat
	{
		static void clear_exception(JNIEnv* env)
		{
			if (env->ExceptionCheck())
				env->ExceptionClear();
		}


		// SwingAnimation.DEFAULT, cached as a global reference. Resolved lazily
		// because it does not exist before 26.3 and nothing should fail at startup
		// over a symbol most versions do not have.
		static jobject default_swing_animation(JNIEnv* env)
		{
			static jobject cached = nullptr;
			if (cached)
				return cached;

			if (!sdk::mappings::have(sdk::mappings::swing_animation_class_sig) ||
			    !sdk::mappings::have(sdk::mappings::swing_animation_default_name))
				return nullptr;

			jclass klass = sdk::classloader::find_class(env, sdk::mappings::swing_animation_class_sig);
			if (!klass)
			{
				clear_exception(env);
				return nullptr;
			}

			jfieldID fid = env->GetStaticFieldID(klass, sdk::mappings::swing_animation_default_name,
			                                     sdk::mappings::swing_animation_default_sig);
			clear_exception(env);
			jobject value = fid ? env->GetStaticObjectField(klass, fid) : nullptr;
			clear_exception(env);
			env->DeleteLocalRef(klass);

			if (!value)
				return nullptr;

			cached = env->NewGlobalRef(value);
			env->DeleteLocalRef(value);
			return cached;
		}

		float field_of_view(JNIEnv* env, jobject game_renderer, jobject camera,
		                    float tick_progress)
		{
			if (!env)
				return 0.0f;

			const char* name = sdk::mappings::get_fov_name;
			const char* sig = sdk::mappings::get_fov_sig;
			if (!sdk::mappings::have(name) || !sdk::mappings::have(sig))
				return 0.0f;

			// The last character of a method descriptor is its return type.
			const char returns = sig[std::strlen(sig) - 1];
			if (returns != 'F' && returns != 'D')
				return 0.0f;

			// "()..." is the 26.1+ getter that lives on Camera and takes nothing;
			// anything else is the older GameRenderer form that is handed the
			// camera, the tick fraction and whether the FOV is allowed to move.
			const bool no_args = sig[0] == '(' && sig[1] == ')';
			jobject receiver = no_args ? camera : game_renderer;
			if (!receiver)
				return 0.0f;

			jclass receiver_class = env->GetObjectClass(receiver);
			if (!receiver_class)
			{
				clear_exception(env);
				return 0.0f;
			}

			jmethodID mid = env->GetMethodID(receiver_class, name, sig);
			clear_exception(env);
			env->DeleteLocalRef(receiver_class);
			if (!mid)
				return 0.0f;

			// Once, so a version whose shape was guessed wrong can be told apart
			// from one where the projection simply never ran.
			static bool reported = false;
			if (!reported)
			{
				reported = true;
				logger::log(std::string("[compat] fov: ") + sdk::mappings::owner_of("get_fov") +
					"." + name + sig + (no_args ? " (no-arg form)" : " (camera form)"));
			}

			double fov = 0.0;
			if (no_args)
			{
				fov = (returns == 'D')
					? (double)env->CallDoubleMethod(receiver, mid)
					: (double)env->CallFloatMethod(receiver, mid);
			}
			else
			{
				if (!camera)
					return 0.0f;
				fov = (returns == 'D')
					? (double)env->CallDoubleMethod(receiver, mid, camera,
					                                (jfloat)tick_progress, JNI_TRUE)
					: (double)env->CallFloatMethod(receiver, mid, camera,
					                               (jfloat)tick_progress, JNI_TRUE);
			}
			clear_exception(env);

			// A plausible field of view. Anything outside this is a misread rather
			// than a setting, and feeding it to the projection scales every box.
			if (fov <= 1.0 || fov >= 179.0)
				return 0.0f;

			return (float)fov;
		}

		// Counts the arguments in a method descriptor. Only needs to tell "one" from
		// "three" here, so it walks types rather than parsing them.
		static int descriptor_arity(const char* sig)
		{
			if (!sig || sig[0] != '(')
				return -1;

			int count = 0;
			for (const char* p = sig + 1; *p && *p != ')'; )
			{
				while (*p == '[')
					++p;
				if (*p == 'L')
				{
					while (*p && *p != ';')
						++p;
					if (*p == ';')
						++p;
				}
				else if (*p)
				{
					++p;
				}
				++count;
			}
			return count;
		}

		bool swing_hand(JNIEnv* env, jobject entity, jobject hand)
		{
			if (!env || !entity || !hand)
				return false;

			const char* name = sdk::mappings::swing_hand_name;
			const char* sig = sdk::mappings::swing_hand_sig;
			if (!sdk::mappings::have(name) || !sdk::mappings::have(sig))
				return false;

			jclass entity_class = env->GetObjectClass(entity);
			if (!entity_class)
			{
				clear_exception(env);
				return false;
			}

			jmethodID mid = env->GetMethodID(entity_class, name, sig);
			clear_exception(env);
			env->DeleteLocalRef(entity_class);
			if (!mid)
				return false;

			const int arity = descriptor_arity(sig);

			if (arity == 1)
			{
				env->CallVoidMethod(entity, mid, hand);
				clear_exception(env);
				return true;
			}

			if (arity != 3)
				return false;

			// 26.3: swing(Hand, SwingAnimation, boolean). The animation is a data
			// component now; DEFAULT is the stock swing every item without one of
			// its own uses, and is what vanilla passes for a bare attack.
			jobject animation = default_swing_animation(env);
			if (!animation)
				return false;

			const bool returns_bool = sig[std::strlen(sig) - 1] == 'Z';
			if (returns_bool)
				env->CallBooleanMethod(entity, mid, hand, animation, JNI_FALSE);
			else
				env->CallVoidMethod(entity, mid, hand, animation, JNI_FALSE);
			clear_exception(env);
			return true;
		}

		// --- movement input ---------------------------------------------------
		//
		// The same two numbers under three different skins. Which one this version
		// wears is read off the symbol table rather than a version number: the
		// impulse fields exist (on Input, then on ClientInput) until 1.21.5
		// replaces them with a Vec2.

		// LocalPlayer.input -- the ClientInput (or, before 1.21.2, the Input).
		static jobject fetch_input_object(JNIEnv* env, jobject player)
		{
			if (!sdk::mappings::have(sdk::mappings::client_player_input_field_name))
				return nullptr;

			jclass player_class = env->GetObjectClass(player);
			if (!player_class)
			{
				clear_exception(env);
				return nullptr;
			}

			jfieldID fid = env->GetFieldID(player_class,
				sdk::mappings::client_player_input_field_name,
				sdk::mappings::client_player_input_field_sig);
			clear_exception(env);
			env->DeleteLocalRef(player_class);
			if (!fid)
				return nullptr;

			jobject input = env->GetObjectField(player, fid);
			clear_exception(env);
			return input;
		}

		// Both impulse fields, when this version still has them.
		static bool impulse_fields(JNIEnv* env, jobject input, jfieldID& forward, jfieldID& sideways)
		{
			forward = nullptr;
			sideways = nullptr;
			if (!sdk::mappings::have(sdk::mappings::input_forward_name) ||
			    !sdk::mappings::have(sdk::mappings::input_sideways_name))
				return false;

			jclass input_class = env->GetObjectClass(input);
			if (!input_class)
			{
				clear_exception(env);
				return false;
			}

			forward = env->GetFieldID(input_class, sdk::mappings::input_forward_name,
			                          sdk::mappings::input_forward_sig);
			clear_exception(env);
			sideways = env->GetFieldID(input_class, sdk::mappings::input_sideways_name,
			                           sdk::mappings::input_sideways_sig);
			clear_exception(env);
			env->DeleteLocalRef(input_class);
			return forward && sideways;
		}

		// ClientInput.moveVector, from 1.21.5 on.
		static jfieldID move_vector_field(JNIEnv* env, jobject input)
		{
			if (!sdk::mappings::have(sdk::mappings::input_movement_vector_name))
				return nullptr;

			jclass input_class = env->GetObjectClass(input);
			if (!input_class)
			{
				clear_exception(env);
				return nullptr;
			}

			jfieldID fid = env->GetFieldID(input_class,
				sdk::mappings::input_movement_vector_name,
				sdk::mappings::input_movement_vector_sig);
			clear_exception(env);
			env->DeleteLocalRef(input_class);
			return fid;
		}

		bool input_writable()
		{
			return sdk::mappings::have(sdk::mappings::client_player_input_field_name) &&
			       ((sdk::mappings::have(sdk::mappings::input_forward_name) &&
			         sdk::mappings::have(sdk::mappings::input_sideways_name)) ||
			        (sdk::mappings::have(sdk::mappings::input_movement_vector_name) &&
			         sdk::mappings::have(sdk::mappings::vec2_x_name)));
		}

		movement_input read_input(JNIEnv* env, jobject player)
		{
			movement_input out;
			if (!env || !player)
				return out;

			jobject input = fetch_input_object(env, player);
			if (!input)
				return out;

			jfieldID forward = nullptr, sideways = nullptr;
			if (impulse_fields(env, input, forward, sideways))
			{
				out.forward = env->GetFloatField(input, forward);
				out.sideways = env->GetFloatField(input, sideways);
				clear_exception(env);
				out.valid = true;
				env->DeleteLocalRef(input);
				return out;
			}

			jfieldID vec_fid = move_vector_field(env, input);
			jobject vec = vec_fid ? env->GetObjectField(input, vec_fid) : nullptr;
			clear_exception(env);
			if (vec)
			{
				jclass vec_class = env->GetObjectClass(vec);
				jfieldID x = vec_class ? env->GetFieldID(vec_class, sdk::mappings::vec2_x_name,
				                                         sdk::mappings::vec2_x_sig) : nullptr;
				clear_exception(env);
				jfieldID y = vec_class ? env->GetFieldID(vec_class, sdk::mappings::vec2_y_name,
				                                         sdk::mappings::vec2_y_sig) : nullptr;
				clear_exception(env);
				if (x && y)
				{
					out.sideways = env->GetFloatField(vec, x);
					out.forward = env->GetFloatField(vec, y);
					clear_exception(env);
					out.valid = true;
				}
				if (vec_class)
					env->DeleteLocalRef(vec_class);
				env->DeleteLocalRef(vec);
			}

			env->DeleteLocalRef(input);
			return out;
		}

		bool write_input(JNIEnv* env, jobject player, const movement_input& in)
		{
			if (!env || !player || !in.valid)
				return false;

			jobject input = fetch_input_object(env, player);
			if (!input)
				return false;

			bool wrote = false;

			jfieldID forward = nullptr, sideways = nullptr;
			if (impulse_fields(env, input, forward, sideways))
			{
				env->SetFloatField(input, forward, in.forward);
				env->SetFloatField(input, sideways, in.sideways);
				clear_exception(env);
				wrote = true;
			}
			else
			{
				// 1.21.5+: the vector is a record-like Vec2, so writing means
				// putting a new one in place rather than editing the old.
				jfieldID vec_fid = move_vector_field(env, input);
				if (vec_fid && sdk::mappings::have(sdk::mappings::vec2_class_sig))
				{
					jclass vec_class = sdk::classloader::find_class(env, sdk::mappings::vec2_class_sig);
					clear_exception(env);
					jmethodID ctor = vec_class ? env->GetMethodID(vec_class, "<init>", "(FF)V") : nullptr;
					clear_exception(env);
					if (ctor)
					{
						jobject fresh = env->NewObject(vec_class, ctor, (jfloat)in.sideways,
						                               (jfloat)in.forward);
						clear_exception(env);
						if (fresh)
						{
							env->SetObjectField(input, vec_fid, fresh);
							clear_exception(env);
							env->DeleteLocalRef(fresh);
							wrote = true;
						}
					}
					if (vec_class)
						env->DeleteLocalRef(vec_class);
				}
			}

			env->DeleteLocalRef(input);
			return wrote;
		}
	}
}
