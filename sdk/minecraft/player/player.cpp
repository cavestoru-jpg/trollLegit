#include <enhance/enhance.h>
#include "../minecraft.h"
#include "player.h"
#include <sdk/classloader.h>

// Helper to check exceptions after JNI calls
static void check_jni_exception(JNIEnv* env, const char* operation)
{
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
	}
}

player_client::player_client()
{
	player = nullptr;
}

player_client::player_client(jobject player)
{
	this->player = player;
}

player_client::~player_client()
{
}

jobject player_client::get_player()
{
	return sdk::instance->get_player();
}

jobject player_client::get_capabilities()
{
	jobject current_player = get_player();
	if (!current_player) return nullptr;
	
	auto env = enhance::instance->get_env();
	if (!env)
	{
		return nullptr;
	}

	jclass player_class = env->GetObjectClass(current_player);
	if (!player_class)
	{
		env->DeleteLocalRef(current_player);
		return nullptr;
	}

	jfieldID fid = env->GetFieldID(player_class, sdk::mappings::abilities_name, sdk::mappings::abilities_sig);
	check_jni_exception(env, "GetFieldID abilities");
	if (!fid)
	{
		env->DeleteLocalRef(player_class);
		env->DeleteLocalRef(current_player);
		return nullptr;
	}

	jobject ret = env->GetObjectField(current_player, fid);

	env->DeleteLocalRef(player_class);
	env->DeleteLocalRef(current_player);

	return ret;
}

void player_client::set_flying(bool state)
{
	jobject capabilities = get_capabilities();
	if (!capabilities)
	{ 
		return;
	}
		
	auto env = enhance::instance->get_env();
	if (!env) return;

	jclass capabilities_class = env->GetObjectClass(capabilities);
	if (!capabilities_class)
	{
		env->DeleteLocalRef(capabilities);
		return;
	}

	jfieldID fid = env->GetFieldID(capabilities_class, sdk::mappings::fly_name, sdk::mappings::fly_sig);
	check_jni_exception(env, "GetFieldID fly");
	if (fid)
	{
		env->SetBooleanField(capabilities, fid, state);
	}
	
	env->DeleteLocalRef(capabilities_class);
	env->DeleteLocalRef(capabilities);
}

void player_client::set_sprinting(bool state)
{
	jobject current_player = get_player();
	if (!current_player) return;
	
	auto env = enhance::instance->get_env();
	if (!env) return;

	jclass player_class = env->GetObjectClass(current_player);
	if (!player_class)
	{
		env->DeleteLocalRef(current_player);
		return;
	}

	jmethodID mid = env->GetMethodID(player_class, sdk::mappings::set_sprinting_name, sdk::mappings::set_sprinting_sig);
	check_jni_exception(env, "GetMethodID set_sprinting");
	if (mid)
	{
		env->CallVoidMethod(current_player, mid, state);
	}

	env->DeleteLocalRef(player_class);
	env->DeleteLocalRef(current_player);
}

void player_client::swap_selected_slot(int slot)
{
	if (slot < 0 || slot > 8) return;

	auto env = enhance::instance->get_env();
	if (!env) return;

	jobject player = sdk::instance->get_player();
	if (!player) return;

	// --- 1. mutate inventory.selectedSlot ---
	// Prefer the setter (method_61496). In 1.21.x the setter ALSO marks
	// the slot as dirty so MC's own tick sends UpdateSelectedSlotC2SPacket
	// on the next sendMovementPackets — meaning we must NOT also push the
	// packet manually below, otherwise the server sees a duplicate and
	// Grim's BadPacketA fires.
	// Fall back to a direct field write only on older builds that don't
	// expose the setter; in that case we DO need to push the packet
	// manually because the field write alone doesn't propagate.
	bool used_setter = false;
	{
		jclass player_class = env->GetObjectClass(player);
		if (!player_class) { env->DeleteLocalRef(player); return; }

		jfieldID inv_fid = env->GetFieldID(player_class,
			sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig);
		if (!inv_fid)
		{
			env->DeleteLocalRef(player_class);
			env->DeleteLocalRef(player);
			return;
		}

		jobject inventory = env->GetObjectField(player, inv_fid);
		env->DeleteLocalRef(player_class);
		if (!inventory) { env->DeleteLocalRef(player); return; }

		jclass inv_class = env->GetObjectClass(inventory);
		if (inv_class)
		{
			jmethodID set_mid = env->GetMethodID(inv_class,
				sdk::mappings::inventory_set_selected_slot_name,
				sdk::mappings::inventory_set_selected_slot_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (set_mid)
			{
				env->CallVoidMethod(inventory, set_mid, slot);
				if (env->ExceptionCheck()) env->ExceptionClear();
				used_setter = true;
			}
			else
			{
				jfieldID slot_fid = env->GetFieldID(inv_class,
					sdk::mappings::inventory_selected_slot_name,
					sdk::mappings::inventory_selected_slot_sig);
				if (slot_fid) env->SetIntField(inventory, slot_fid, slot);
			}
			env->DeleteLocalRef(inv_class);
		}
		env->DeleteLocalRef(inventory);
	}

	// --- 2. only push UpdateSelectedSlotC2SPacket manually if the setter
	//        wasn't available; otherwise MC will emit the packet itself
	//        on the next tick and a duplicate trips Grim's BadPacketA. ---
	if (!used_setter)
	{
		jclass mc_class = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
		if (!mc_class) { env->DeleteLocalRef(player); return; }

		jfieldID instance_fid = env->GetStaticFieldID(mc_class,
			sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
		if (!instance_fid) { env->DeleteLocalRef(mc_class); env->DeleteLocalRef(player); return; }

		jobject mc = env->GetStaticObjectField(mc_class, instance_fid);
		if (!mc) { env->DeleteLocalRef(mc_class); env->DeleteLocalRef(player); return; }

		jmethodID get_nh_mid = env->GetMethodID(mc_class,
			sdk::mappings::network_handler_name, sdk::mappings::network_handler_sig);
		env->DeleteLocalRef(mc_class);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (!get_nh_mid) { env->DeleteLocalRef(mc); env->DeleteLocalRef(player); return; }

		jobject nh = env->CallObjectMethod(mc, get_nh_mid);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(mc);
		if (!nh) { env->DeleteLocalRef(player); return; }

		jclass pkt_class = sdk::classloader::find_class(env,
			sdk::mappings::update_selected_slot_c2s_packet_class_sig);
		if (!pkt_class) { env->DeleteLocalRef(nh); env->DeleteLocalRef(player); return; }

		jmethodID ctor = env->GetMethodID(pkt_class, "<init>", "(I)V");
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (!ctor) { env->DeleteLocalRef(pkt_class); env->DeleteLocalRef(nh); env->DeleteLocalRef(player); return; }

		jobject pkt = env->NewObject(pkt_class, ctor, (jint)slot);
		env->DeleteLocalRef(pkt_class);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (!pkt) { env->DeleteLocalRef(nh); env->DeleteLocalRef(player); return; }

		jclass nh_class = env->GetObjectClass(nh);
		if (nh_class)
		{
			jmethodID send_mid = env->GetMethodID(nh_class,
				sdk::mappings::send_packet_name, sdk::mappings::send_packet_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (send_mid)
			{
				env->CallVoidMethod(nh, send_mid, pkt);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
			env->DeleteLocalRef(nh_class);
		}

		env->DeleteLocalRef(pkt);
		env->DeleteLocalRef(nh);
	}

	env->DeleteLocalRef(player);
}

float player_client::get_attack_cooldown_progress(float base_time)
{
	jobject current_player = get_player();
	if (!current_player) return 0.0f;
	
	auto env = enhance::instance->get_env();
	if (!env) return 0.0f;

	jclass player_class = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
	if (!player_class)
	{
		env->DeleteLocalRef(current_player);
		return 0.0f;
	}

	jmethodID mid = env->GetMethodID(player_class, sdk::mappings::get_attack_cooldown_progress_name, sdk::mappings::get_attack_cooldown_progress_sig);
	check_jni_exception(env, "GetMethodID get_attack_cooldown_progress");
	if (!mid)
	{
		env->DeleteLocalRef(player_class);
		env->DeleteLocalRef(current_player);
		return 0.0f;
	}

	jfloat progress = env->CallFloatMethod(current_player, mid, base_time);

	env->DeleteLocalRef(player_class);
	env->DeleteLocalRef(current_player);

	return progress;
}

// Presses or releases the sprint key binding instead of calling
// LivingEntity.setSprinting.
//
// setSprinting looks harmless but adds/removes the sprint speed modifier on the
// player's movement-speed attribute, and that modifier map is a plain
// Object2ObjectArrayMap with no synchronisation. Driving it from the client's
// 10 ms worker while Minecraft's tick thread does the same inside tickMovement
// corrupts the map: it crashed the game with
//   ArrayIndexOutOfBoundsException: Index -1 out of bounds for length 4
//   at Object2ObjectArrayMap.remove <- EntityAttributeInstance <- setSprinting
// Writing the key binding's boolean instead leaves the attribute work on the
// thread that owns it.
void player_client::set_sprint_key(bool pressed)
{
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return;

	jclass mc_cls = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
	if (!mc_cls) return;

	jobject mc = nullptr;
	jobject options = nullptr;
	jobject key = nullptr;

	do
	{
		jfieldID inst_fid = env->GetStaticFieldID(mc_cls, sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
		if (!inst_fid) break;

		mc = env->GetStaticObjectField(mc_cls, inst_fid);
		if (!mc) break;

		jfieldID options_fid = env->GetFieldID(mc_cls, sdk::mappings::mc_options_field_name, sdk::mappings::mc_options_field_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
		if (!options_fid) break;

		options = env->GetObjectField(mc, options_fid);
		if (!options) break;

		jclass opts_cls = env->GetObjectClass(options);
		if (!opts_cls) break;

		jfieldID sprint_fid = env->GetFieldID(opts_cls, sdk::mappings::gameoptions_sprint_key_name, sdk::mappings::gameoptions_sprint_key_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(opts_cls);
		if (!sprint_fid) break;

		key = env->GetObjectField(options, sprint_fid);
		if (!key) break;

		jclass kb_cls = env->GetObjectClass(key);
		if (!kb_cls) break;

		jmethodID set_pressed = env->GetMethodID(kb_cls, sdk::mappings::keybinding_set_pressed_name, sdk::mappings::keybinding_set_pressed_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(kb_cls);
		if (!set_pressed) break;

		env->CallVoidMethod(key, set_pressed, pressed ? JNI_TRUE : JNI_FALSE);
		if (env->ExceptionCheck()) env->ExceptionClear();
	} while (false);

	if (key) env->DeleteLocalRef(key);
	if (options) env->DeleteLocalRef(options);
	if (mc) env->DeleteLocalRef(mc);
	env->DeleteLocalRef(mc_cls);
}
