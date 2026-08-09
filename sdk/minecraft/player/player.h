#pragma once

#include <sdk/includes.h>

class player_client
{
private:
	jobject player;
public:
	player_client();
	player_client(jobject player);
	~player_client();

	jobject get_capabilities();
	void set_flying(bool state);

	// Calls LivingEntity.setSprinting directly. UNSAFE from the worker thread:
	// it mutates the entity's attribute-modifier map, which the tick thread
	// mutates too, and the map is not thread-safe. Prefer set_sprint_key.
	void set_sprinting(bool state);

	// Holds or releases the sprint key binding, letting Minecraft flip
	// sprinting on its own thread. Safe to call from the worker.
	void set_sprint_key(bool pressed);
	float get_attack_cooldown_progress(float base_time = 0.5f);

	jobject get_player();

	// Swap selected hotbar slot AND push UpdateSelectedSlotC2SPacket
	// immediately. setSelectedSlot alone only mutates inventory.selectedSlot;
	// the C2S packet that tells the server about it normally waits for the
	// next tickMovement(), which is too slow when we attack in the same frame.
	static void swap_selected_slot(int slot);
};
