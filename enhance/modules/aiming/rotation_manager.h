#pragma once

#include "rotation.h"
#include "processors.h"
#include <mutex>

// Ported from LiquidBounce's RotationManager. The single owner of the silent
// rotation: modules ask for a rotation, this decides what actually happens.
//
// Deliberately JNI-free. It is fed the player's real angles and returns the
// angles to hold; the caller does the JNI on both sides. That keeps the part
// with the interesting logic testable without launching the game, which after
// three wrong guesses about the hook point is worth the indirection.

namespace enhance::modules::aiming
{
	// Who is asking. One live request per provider -- a second request from
	// the same module replaces its first rather than queueing behind it.
	enum class provider_t
	{
		killaura = 0,
		aimassist,
		triggerbot,
		pearl_catch,
		count
	};

	// Higher wins. Values follow LiquidBounce's Priority so the relative
	// ordering is the one its modules were tuned against.
	namespace priority
	{
		constexpr int not_important = -20;
		constexpr int normal        = 0;
		constexpr int usage_1       = 20;
		constexpr int killaura      = 30;   // IMPORTANT_FOR_USAGE_2
		constexpr int usage_3       = 35;
		constexpr int player_life   = 40;
		constexpr int user_safety   = 60;
	}

	enum class movement_correction_t
	{
		// Silent rotation only: the server is told a different rotation than
		// the body moves along. Feels best, and is exactly the mismatch a
		// simulation check looks for.
		off = 0,

		// The movement is computed against the reported rotation too, so the
		// two agree. What the tick hook implements.
		strict = 1,
	};

	struct rotation_target_t
	{
		rotation_t rotation;

		// How long the request stays alive without being renewed. Modules
		// re-request every tick while they have a target; this is the grace
		// period after they stop, during which the rotation is walked back
		// instead of snapping.
		int ticks_until_reset = 5;

		// How close to the real rotation the walk-back must get before the
		// override is dropped. Too small and it never converges; too large
		// and the release is visible as a jump.
		float reset_threshold = 2.0f;

		movement_correction_t movement_correction = movement_correction_t::strict;
	};

	class rotation_manager_t
	{
	public:
		// Submit or renew this provider's request. Cheap; call every tick.
		void request(provider_t provider, const rotation_target_t& target, int priority);

		// Withdraw immediately rather than waiting for the expiry. Use when a
		// module is switched off, not when it merely lost its target -- the
		// expiry path is what produces the smooth walk-back.
		void withdraw(provider_t provider);

		// Advance one game tick. `player_rotation` is the player's real
		// angles, `server_rotation` what we last put on the wire.
		//
		// Returns true if a rotation should be held this tick, writing it to
		// `out`. False means no override at all -- the caller must then leave
		// the game entirely alone rather than holding the last value.
		bool update(const rotation_t& player_rotation,
		            const rotation_t& server_rotation,
		            rotation_t& out);

		// The correction mode of whichever request is currently in charge.
		movement_correction_t active_movement_correction() const;

		// True while walking back to the player's real rotation after the
		// request expired.
		bool resetting() const { return m_resetting; }

		bool has_rotation() const { return m_has_rotation; }
		const rotation_t& current() const { return m_current; }
		const rotation_t& previous() const { return m_previous; }

		// Drop everything, including the walk-back. For unload and world
		// change, where there is nothing left to be smooth about.
		void reset();

		// Last tick's inputs and output, for the diagnostic log. Plain floats,
		// written only inside update() on the tick thread and read only by the
		// logger on the same thread.
		struct debug_state_t
		{
			float requested = 0.0f;   // what the module asked for, verbatim
			float anchored  = 0.0f;   // after re-expressing in `from`'s winding
			float from      = 0.0f;   // where the step started
			float next      = 0.0f;   // what came out of the processor chain
			int   mode      = 0;      // smooth_mode_t actually in use
		};
		const debug_state_t& debug_state() const { return m_debug; }

		processor_chain_t processors;

	private:
		struct request_t
		{
			bool              active = false;
			int               priority = 0;
			// Wall-clock deadline, not a tick count.
			//
			// Requests are renewed by the enhance worker on its own timer,
			// while ticks are counted by the JVM tick thread. Counted in
			// ticks, any worker iteration that overran -- one slow entity
			// scan, a GC pause, a chunk load -- let the request lapse. The
			// manager then walked the rotation back to the player, the next
			// request restarted it, and the aim flipped between the target and
			// the player's real angle about once a second. That is the shake.
			unsigned long long expires_at_ms = 0;
			rotation_target_t target;
		};

		// Requests come from the enhance worker; update() runs on the JVM tick
		// thread. Contention is negligible -- a handful of writes and one read
		// per tick -- and nothing holds this across a JNI call, so there is
		// nothing to deadlock against inside the hook.
		mutable std::mutex m_mutex;

		request_t m_requests[static_cast<int>(provider_t::count)];

		// The winning request this tick, or none.
		bool              m_have_target = false;
		rotation_target_t m_target;

		// Kept after the request expires so the walk-back still knows which
		// settings to use. LiquidBounce's previousRotationTarget.
		bool              m_have_previous_target = false;
		rotation_target_t m_previous_target;

		bool       m_has_rotation = false;
		bool       m_resetting = false;
		rotation_t m_current;
		rotation_t m_previous;

		debug_state_t m_debug;

		bool pick_target();
	};

	// The one instance. Modules reach it through here rather than owning one.
	rotation_manager_t& manager();

	// Copy the user's settings into the processor chain. Cheap; call once a
	// tick before update() so a change in the menu takes effect immediately
	// rather than at the next world load.
	void sync_settings_from_globals();
}
