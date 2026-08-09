#pragma once

namespace enhance
{
	namespace modules
	{
		namespace vanilla_nametags
		{
			// Suppresses Minecraft's own floating player names so only the
			// client's tags remain. Without this the two overlap almost exactly
			// and enabling name tags looks like nothing happened.
			//
			// The technique is the one the reference uses: every visible
			// player's scoreboard team is forced to nameTagVisibility = NEVER,
			// and players with no team are parked in a throwaway team that
			// carries that rule. This is a client-side scoreboard mutation, so
			// it is invisible to the server — but it MUST be undone, or the
			// player's own scoreboard stays wrong for the rest of the session.
			//
			// Call once per frame from the render thread with the toggle state;
			// it applies and restores itself as the toggle changes.
			void tick(bool want_hidden);

			// Puts every touched team back the way it was found. Safe to call
			// when nothing was ever hidden.
			void restore();
		}
	}
}
