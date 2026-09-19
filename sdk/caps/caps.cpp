#include "caps.h"

#include <sdk/mappings/mappings.hpp>
#include <sdk/version/version.h>
#include <enhance/utils/logger.h>

#include <string>
#include <vector>

namespace sdk
{
	namespace caps
	{
		namespace
		{
			struct requirement
			{
				feature     which;
				const char* symbols[4];   // spec ids, nullptr-terminated
				const char* human;        // what the symbol is, in words
			};

			// The symbols are spec ids from tools/symbols/symbols.json, not C++
			// identifiers: this asks the table "does this version have it", which is
			// the same question the binder answered at startup.
			const requirement k_requirements[] = {
				// Either renderer hook will do: the render state from 1.21.2 on,
				// LivingEntityRenderer.render before it.
				{ feature::model_pitch,   { "update_render_state", nullptr },
				  "the render-state hook that carries the model's pitch (1.21.2+)" },
				{ feature::model_pitch,   { "living_renderer_render", nullptr },
				  "LivingEntityRenderer.render in a shape this client can hook" },
				// Not a deficiency: the server did not send a rotation-only
				// teleport before 1.21.2, so there is no echo to intercept. The
				// position-look hook already covers what those versions do send.
				{ feature::rotation_echo, { "on_player_rotation", nullptr },
				  "a rotation-only teleport packet, which the server only sends from 1.21.2 on" },
				{ feature::riptide,       { "trident_on_stopped_using", nullptr },
				  "TridentItem.onStoppedUsing in a shape this client can hook" },
				{ feature::reach,         { "get_entity_interaction_range", nullptr },
				  "the entity interaction range attribute (1.20.5+)" },
				// Either way of writing the slot will do. sdk/minecraft/player
				// already falls back to the field when the setter is absent, so
				// reporting this unavailable before 1.21.2 was simply wrong: it
				// greyed out a feature that works on every supported version.
				{ feature::slot_switch,   { "inventory_set_selected_slot", nullptr },
				  "Inventory.setSelectedSlot (1.21.2+)" },
				{ feature::slot_switch,   { "inventory_selected_slot", nullptr },
				  "the selected-slot field" },
				// What the module actually uses: it scans block states around the
				// player rather than reading the world's block-entity set, so the
				// 1.21.9 set it used to be gated on was never a requirement.
				{ feature::storage_esp,   { "world_get_block_state", "block_state_get_block",
				                            "block_pos_class", nullptr },
				  "the block-state lookup storage ESP scans with" },
				{ feature::team_colours,  { "dyed_color_get_color", "dyed_color_component_class", nullptr },
				  "the dyed-colour item component (1.20.5+)" },
				// Two shapes, either will do: the impulse fields (wherever they
				// live) or the movement vector that replaced them in 1.21.5.
				{ feature::input_write,   { "client_player_input_field", "input_forward",
				                            "input_sideways", nullptr },
				  "the movement input fields" },
				{ feature::input_write,   { "client_player_input_field", "input_movement_vector",
				                            "vec2_x", nullptr },
				  "ClientInput.moveVector" },
			};

			struct resolved
			{
				bool        computed = false;
				bool        available = false;
				std::string reason;
			};

			resolved g_state[(int)feature::count];

			const resolved& state_of(feature f)
			{
				resolved& r = g_state[(int)f];
				if (r.computed)
					return r;

				r.computed = true;
				r.available = true;

				// Rows for one feature are ALTERNATIVES: a feature the game exposes
				// two different ways (impulse fields, or the vector that replaced
				// them) is available when either way resolves. Within a row, every
				// symbol is required.
				bool saw_row = false;
				for (const requirement& req : k_requirements)
				{
					if (req.which != f)
						continue;
					saw_row = true;

					bool row_ok = true;
					for (int i = 0; i < 4 && req.symbols[i]; ++i)
					{
						// A symbol with no owner on this version is one the table
						// reports absent -- the same "" the constants bind to.
						if (!sdk::mappings::have(sdk::mappings::owner_of(req.symbols[i])))
						{
							row_ok = false;
							break;
						}
					}

					if (row_ok)
					{
						r.available = true;
						r.reason.clear();
						return r;
					}

					r.available = false;
					r.reason = std::string("needs ") + req.human +
					           " -- absent on " + sdk::version::name();
				}

				if (!saw_row)
					r.available = true;
				return r;
			}
		}

		bool available(feature f)
		{
			if ((int)f < 0 || f >= feature::count)
				return false;
			// Before binding, nothing is known; claiming availability would put a
			// live control in front of a client that has resolved nothing.
			if (!sdk::mappings::bound())
				return false;
			return state_of(f).available;
		}

		const char* why_not(feature f)
		{
			if ((int)f < 0 || f >= feature::count)
				return "";
			if (!sdk::mappings::bound())
				return "mappings are not bound";
			return state_of(f).reason.c_str();
		}

		// Feature names for the log, in enum order.
		static const char* const k_names[] = {
			"model pitch", "rotation echo", "riptide", "reach",
			"slot switch", "storage esp", "team colours", "input write",
		};

		void log_summary()
		{
			std::string gated;
			for (int i = 0; i < (int)feature::count; ++i)
			{
				if (available((feature)i))
					continue;
				if (!gated.empty())
					gated += ", ";
				gated += k_names[i];
			}

			if (gated.empty())
				logger::log(std::string("[caps] every feature is supported on ") +
				            sdk::version::name());
			else
				logger::log("[caps] unavailable on " + std::string(sdk::version::name()) +
				            ": " + gated);
		}
	}
}
