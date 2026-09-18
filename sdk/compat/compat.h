#pragma once

#include <sdk/includes.h>

// Calls whose SHAPE changed across the supported range, not just their name.
//
// The symbol table handles renames and moves on its own: a call site asks for a
// name and gets the right one for the running version. What it cannot paper over
// is a method that gained an argument, lost one, or changed its return type --
// there the caller itself has to change, and binding the symbol without changing
// it would read a double as a float or push arguments a method does not take.
//
// Each helper here picks its shape from the descriptor the symbol actually bound
// to, not from a version number. That way a version nobody has tested yet behaves
// correctly as long as its descriptor matches one of the known shapes, and an
// unknown shape degrades to "unavailable" instead of corrupting a stack frame.
namespace sdk
{
	namespace compat
	{
		// The camera's field of view in degrees, or 0 when this version's shape is
		// not one we know how to call.
		//
		// Three shapes so far:
		//   1.20   - 1.21.1   GameRenderer.getFov(Camera, float, boolean) -> double
		//   1.21.2 - 1.21.11  GameRenderer.getFov(Camera, float, boolean) -> float
		//   26.1+             Camera.getFov() -> float
		//
		// Both objects are the caller's local references and are left alone.
		float field_of_view(JNIEnv* env, jobject game_renderer, jobject camera,
		                    float tick_progress);

		// The player's movement input: sideways and forward impulses, as the game
		// itself stores them.
		//
		// Three shapes, all of them the same two numbers:
		//   1.20   - 1.21.1  Input.leftImpulse / forwardImpulse
		//   1.21.2 - 1.21.4  the same fields, moved onto ClientInput
		//   1.21.5+          ClientInput.moveVector, a Vec2 that has to be rebuilt
		//                    to be written
		//
		// Writing this is how movement can be corrected silently: the server
		// recomputes the direction from the rotation it was told, so rotating the
		// input by the same angle in reverse leaves the real direction unchanged.
		//
		// MUST be called from the tick thread. Input is game state, and law 2 in
		// AGENT.md exists because writing game state from the worker corrupted a
		// non-thread-safe map and crashed the game.
		struct movement_input
		{
			float sideways = 0.0f;   // + is left
			float forward = 0.0f;
			bool  valid = false;
		};

		// Reads the live input off the player. `valid` is false when this version's
		// shape is not one we know.
		movement_input read_input(JNIEnv* env, jobject player);

		// Writes both impulses back. Returns false when nothing was written.
		bool write_input(JNIEnv* env, jobject player, const movement_input& in);

		// Whether this version exposes input in a shape that can be written at all.
		bool input_writable();

		// Plays the arm swing. Returns false when this version's shape is unknown,
		// in which case the attack still lands but without an animation or a swing
		// packet.
		//
		// Two shapes:
		//   1.20 - 26.2   LivingEntity.swing(Hand)
		//   26.3+         LivingEntity.swing(Hand, SwingAnimation, boolean),
		//                 passed SwingAnimation.DEFAULT
		//
		// `entity` is the player, `hand` the Hand enum constant; both stay the
		// caller's.
		bool swing_hand(JNIEnv* env, jobject entity, jobject hand);
	}
}
