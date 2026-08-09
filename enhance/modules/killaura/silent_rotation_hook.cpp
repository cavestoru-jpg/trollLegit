#include "silent_rotation_hook.h"
#include "../../enhance.h"
#include "../../utils/logger.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <sdk/render/render_view.h>
#include <atomic>
#include <string>

// Hooked method: ClientPlayerEntity.sendMovementPackets (class_746.method_3136)
// runs once per tick from tickMovement(); inside, Minecraft builds the
// PlayerMoveC2SPacket variants from the player's yaw/pitch fields and pushes
// them through the network handler. By briefly swapping the angles around
// the original call, outgoing look packets carry fake angles while the
// client camera / input layer never see them.
//
// CRITICAL: this callback runs on the JVM tick thread. JNIEnv is thread-
// local — every JNI call here MUST go through the `env` parameter JNIHook
// gives us, NOT through enhance::instance->get_env() (which is bound to the
// enhance worker thread; using it cross-thread crashes the JVM). For that
// reason we cache method IDs once at init() (on the attach thread) and call
// the setters via the passed env.
static jmethodID ORIG_sendMovementPackets = nullptr;
// The method we redefined. Without it we cannot undo the redefinition, and a
// class left pointing at our native code becomes an access violation the
// moment the DLL is unloaded.
static jmethodID g_hooked_mid = nullptr;
static bool      g_detached_cleanly = true;
static jclass    g_client_player_class    = nullptr;
static bool      g_attached               = false;

// Cached method IDs — resolved once, safe to use from any attached thread.
static jmethodID g_mid_get_yaw     = nullptr;
static jmethodID g_mid_get_pitch   = nullptr;
static jmethodID g_mid_set_yaw     = nullptr;
static jmethodID g_mid_set_pitch   = nullptr;

// LivingEntity body/head yaw — backed by fields, not methods. Field IDs are
// also thread-safe once resolved. Optional: if the mappings don't match this
// Yarn version, we just skip the body/head sync.
static jclass    g_living_entity_class = nullptr;
static jfieldID  g_fid_body_yaw   = nullptr;
static jfieldID  g_fid_head_yaw   = nullptr;

// Interaction manager + attack method — used for the in-hook attack so we
// can guarantee Look(fake) -> Attack packet ordering at the network layer.
static jclass    g_interaction_mgr_class = nullptr;
static jmethodID g_mid_attack_entity     = nullptr;
static jmethodID g_mid_get_interaction   = nullptr;  // MC.getInteractionManager via field

static std::atomic<bool> g_fake_active{false};
static float g_fake_yaw   = 0.0f;
static float g_fake_pitch = 0.0f;

// Pending attack target — GlobalRef promoted on the worker thread so the
// JVM tick thread can safely consume it inside the hook. nullptr = none.
static std::atomic<jobject> g_pending_target{nullptr};
static std::atomic<int>     g_pending_owner{0};

static float g_saved_yaw       = 0.0f;
static float g_saved_pitch     = 0.0f;
static float g_saved_body_yaw  = 0.0f;
static float g_saved_head_yaw  = 0.0f;
static bool  g_swap_living     = false;

static void hkSendMovementPackets(JNIEnv* env, jobject thiz)
{
	// NESTING: tickMovement calls this method, and aiming::tick_movement_hook
	// wraps tickMovement with a yaw swap of its own -- so when movement
	// correction is on, this runs *inside* that swap and the yaw we read below
	// is already the fake one.
	//
	// That is harmless, and deliberately left alone. Save and restore here are
	// symmetric: we store whatever is current, overwrite it, then put the same
	// value back, and the outer hook restores the player's real yaw when
	// tickMovement returns. Both hooks aim at the same rotation anyway --
	// movement correction exists precisely so the yaw we move with matches the
	// yaw we report.
	//
	// Worth knowing if this ever needs changing: with the outer swap active
	// and this hook idle, the look packet still carries the outer yaw. That is
	// correct for STRICT correction rather than a leak. Use
	// aiming::tick_movement_hook::is_swapping() to detect the nested case.
	const bool active = g_fake_active.load(std::memory_order_acquire);

	if (active && thiz && g_mid_get_yaw && g_mid_set_yaw && g_mid_get_pitch && g_mid_set_pitch)
	{
		g_saved_yaw   = env->CallFloatMethod(thiz, g_mid_get_yaw);
		if (env->ExceptionCheck()) { env->ExceptionClear(); }
		g_saved_pitch = env->CallFloatMethod(thiz, g_mid_get_pitch);
		if (env->ExceptionCheck()) { env->ExceptionClear(); }

		env->CallVoidMethod(thiz, g_mid_set_yaw, g_fake_yaw);
		if (env->ExceptionCheck()) { env->ExceptionClear(); }
		env->CallVoidMethod(thiz, g_mid_set_pitch, g_fake_pitch);
		if (env->ExceptionCheck()) { env->ExceptionClear(); }

		// Best-effort body/head sync via direct field access.
		if (g_fid_body_yaw && g_fid_head_yaw)
		{
			g_saved_body_yaw = env->GetFloatField(thiz, g_fid_body_yaw);
			g_saved_head_yaw = env->GetFloatField(thiz, g_fid_head_yaw);
			env->SetFloatField(thiz, g_fid_body_yaw, g_fake_yaw);
			env->SetFloatField(thiz, g_fid_head_yaw, g_fake_yaw);
			g_swap_living = true;
		}
		else
		{
			g_swap_living = false;
		}
	}

	if (ORIG_sendMovementPackets && g_client_player_class && thiz)
	{
		env->CallNonvirtualVoidMethod(thiz, g_client_player_class, ORIG_sendMovementPackets);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	// Fire any pending attack RIGHT HERE, on the JVM tick thread, while the
	// fake rotation is still applied. The Look packet was just queued by
	// sendMovementPackets above; sending the Attack packet now guarantees
	// channel order Look(fake) -> Attack. interactionManager.attackEntity
	// will push InteractEntityC2SPacket on the same network handler.
	// Fire the queued attack whenever one is queued, NOT only while this hook
	// is holding a fake rotation. The rotation now comes from
	// aiming::tick_movement_hook, which wraps the whole of tick() -- so by the
	// time we get here the fake angles are already applied and the look packet
	// above already carried them. Gating on this hook's own `active` flag
	// would mean no attack ever fired once killaura moved to the manager.
	if (thiz && !g_mid_attack_entity && g_pending_target.load(std::memory_order_acquire))
	{
		// Drain it anyway, or the GlobalRef leaks once per queued attack.
		jobject stuck = g_pending_target.exchange(nullptr, std::memory_order_acq_rel);
		if (stuck) env->DeleteGlobalRef(stuck);
		logger::log_error("[attack] queued attack dropped: attackEntity is unresolved");
	}

	if (thiz && g_mid_attack_entity)
	{
		jobject target = g_pending_target.exchange(nullptr, std::memory_order_acq_rel);
		if (target)
		{
			// Need MinecraftClient.interactionManager — fetch via the static
			// MC instance + field.
			jclass mc_cls = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
			if (mc_cls)
			{
				jfieldID mc_instance_fid = env->GetStaticFieldID(mc_cls,
					sdk::mappings::minecraftclient_name, sdk::mappings::minecraftclient_sig);
				if (mc_instance_fid)
				{
					jobject mc = env->GetStaticObjectField(mc_cls, mc_instance_fid);
					if (mc)
					{
						jfieldID im_fid = env->GetFieldID(mc_cls,
							sdk::mappings::interaction_manager_name,
							sdk::mappings::interaction_manager_sig);
						if (im_fid)
						{
							jobject im = env->GetObjectField(mc, im_fid);
							if (im)
							{
								env->CallVoidMethod(im, g_mid_attack_entity, thiz, target);
								// Report what actually happened. Queued and executed are
								// different events, and an exception here used to be
								// swallowed without trace -- attackEntity refusing the
								// target looked identical to the attack never being
								// drained at all.
								if (env->ExceptionCheck())
								{
									env->ExceptionClear();
									logger::log_error("[attack] attackEntity threw - target refused client-side");
								}
								else
								{
									logger::log("[attack] attackEntity executed on tick thread");
								}
								env->DeleteLocalRef(im);
							}
							else
							{
								logger::log_error("[attack] interactionManager was null");
							}
						}
						env->DeleteLocalRef(mc);
					}
				}
				env->DeleteLocalRef(mc_cls);
			}
			env->DeleteGlobalRef(target);
		}
	}

	if (active && thiz && g_mid_set_yaw && g_mid_set_pitch)
	{
		env->CallVoidMethod(thiz, g_mid_set_yaw, g_saved_yaw);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->CallVoidMethod(thiz, g_mid_set_pitch, g_saved_pitch);
		if (env->ExceptionCheck()) env->ExceptionClear();

		if (g_swap_living && g_fid_body_yaw && g_fid_head_yaw)
		{
			env->SetFloatField(thiz, g_fid_body_yaw, g_saved_body_yaw);
			env->SetFloatField(thiz, g_fid_head_yaw, g_saved_head_yaw);
		}
		g_swap_living = false;
		g_fake_active.store(false, std::memory_order_release);
	}
}

bool enhance::modules::silent_rotation_hook::init()
{
	if (g_attached) return true;

	// The attach may run on the client thread (posted there so it does
	// not need can_suspend), and JNIEnv is thread-local -- the cached one
	// belongs to the enhance worker and is not valid here.
	auto env = sdk::render::current_thread_env();
	auto jvm = enhance::instance ? enhance::instance->get_java_vm() : nullptr;
	if (!env || !jvm) return false;

	jnihook_result_t init_res = JNIHook_Init(jvm);
	// 3 is ERR_ADD_JVMTI_CAPS, not "already initialized" -- treating it as
	// benign left g_jnihook null and turned every later attach into a
	// misleading ERR_JVMTI_OPERATION. Init is idempotent now.
	if (init_res != JNIHOOK_OK)
	{
		logger::log(std::string("[silent] JNIHook_Init failed=") + std::to_string((int)init_res) +
		            " jvmti_err=" + std::to_string(JNIHook_LastJvmtiError()));
		return false;
	}

	jclass cp_cls = sdk::classloader::find_class(env, sdk::mappings::clientplayerentity_class_sig);
	if (!cp_cls) return false;

	jmethodID mid = env->GetMethodID(cp_cls,
		sdk::mappings::send_movement_packets_name,
		sdk::mappings::send_movement_packets_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!mid)
	{
		env->DeleteLocalRef(cp_cls);
		return false;
	}

	// Resolve yaw/pitch getter/setter method IDs on the Entity base class.
	jclass entity_cls = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
	if (entity_cls)
	{
		g_mid_get_yaw   = env->GetMethodID(entity_cls, sdk::mappings::entity_get_yaw_name, sdk::mappings::entity_get_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_get_yaw = nullptr; }
		// NOTE: mappings.hpp's entity_get_pitch_sig is "(F)F" which would be
		// `getPitch(float)` — that's actually the tick-delta variant. The
		// no-arg getPitch() in 1.21 is method_36455. We try that first; if
		// it fails, fall back to method_5695()F. Both can't coexist so the
		// fallback path is just defensive.
		g_mid_get_pitch = env->GetMethodID(entity_cls, "method_36455", "()F");
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_get_pitch = nullptr; }
		if (!g_mid_get_pitch)
		{
			g_mid_get_pitch = env->GetMethodID(entity_cls, "method_5695", "()F");
			if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_get_pitch = nullptr; }
		}

		g_mid_set_yaw   = env->GetMethodID(entity_cls, sdk::mappings::entity_set_yaw_name, sdk::mappings::entity_set_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_set_yaw = nullptr; }
		g_mid_set_pitch = env->GetMethodID(entity_cls, sdk::mappings::entity_set_pitch_name, sdk::mappings::entity_set_pitch_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_set_pitch = nullptr; }
		env->DeleteLocalRef(entity_cls);
	}

	// LivingEntity body/head yaw (optional — keep going if missing).
	jclass living_cls = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
	if (living_cls)
	{
		g_fid_body_yaw = env->GetFieldID(living_cls, sdk::mappings::living_entity_body_yaw_name, sdk::mappings::living_entity_body_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_body_yaw = nullptr; }
		g_fid_head_yaw = env->GetFieldID(living_cls, sdk::mappings::living_entity_head_yaw_name, sdk::mappings::living_entity_head_yaw_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_fid_head_yaw = nullptr; }
		env->DeleteLocalRef(living_cls);
	}

	// Resolve interactionManager.attackEntity (optional — if missing, the
	// in-hook attack path is disabled; killaura falls back to the worker-
	// thread do_attack() with the usual packet-order risk).
	{
		jclass im_cls = sdk::classloader::find_class(env, "net/minecraft/class_636"); // ClientPlayerInteractionManager
		if (im_cls)
		{
			g_mid_attack_entity = env->GetMethodID(im_cls,
				sdk::mappings::attack_entity_name,
				sdk::mappings::attack_entity_sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); g_mid_attack_entity = nullptr; }
			env->DeleteLocalRef(im_cls);
		}
	}

	// Refuse to install the hook if the core (yaw/pitch) accessors aren't
	// available — running with broken accessors would crash inside the hook.
	if (!g_mid_get_yaw || !g_mid_set_yaw || !g_mid_get_pitch || !g_mid_set_pitch)
	{
		env->DeleteLocalRef(cp_cls);
		return false;
	}

	jnihook_result_t r = JNIHook_Attach(mid, reinterpret_cast<void*>(hkSendMovementPackets), &ORIG_sendMovementPackets);
	if (r == JNIHOOK_OK) g_hooked_mid = mid;
	if (r != JNIHOOK_OK)
	{
		env->DeleteLocalRef(cp_cls);
		return false;
	}

	g_client_player_class = reinterpret_cast<jclass>(env->NewGlobalRef(cp_cls));
	env->DeleteLocalRef(cp_cls);
	if (!g_client_player_class) return false;

	g_attached = true;
	// The attack path is optional to attaching but decides whether a queued
	// attack can ever run: the hook body is gated on this id, so an
	// unresolved mapping means attacks are dropped in silence.
	logger::log(std::string("[attack] hook attached, attackEntity=") +
	            (g_mid_attack_entity ? "resolved" : "UNRESOLVED - queued attacks will never fire"));
	return true;
}

void enhance::modules::silent_rotation_hook::shutdown()
{
	// Undo the class redefinition FIRST. Everything below is bookkeeping; this
	// is the part that decides whether unloading the DLL is survivable.
	if (g_hooked_mid)
	{
		const jnihook_result_t r = JNIHook_Detach(g_hooked_mid);
		g_detached_cleanly = (r == JNIHOOK_OK);
		logger::log(std::string("[unload] silent detach result=") + std::to_string((int)r));
		g_hooked_mid = nullptr;
	}
	else
	{
		logger::log("[unload] silent was never attached");
	}

	g_fake_active.store(false, std::memory_order_release);
	g_swap_living = false;
	ORIG_sendMovementPackets = nullptr;
	g_mid_get_yaw = nullptr;
	g_mid_get_pitch = nullptr;
	g_mid_set_yaw = nullptr;
	g_mid_set_pitch = nullptr;
	g_fid_body_yaw = nullptr;
	g_fid_head_yaw = nullptr;

	if (g_client_player_class && enhance::instance)
	{
		try
		{
			auto env = enhance::instance->get_env();
			if (env) env->DeleteGlobalRef(g_client_player_class);
		}
		catch (...) {}
	}
	g_client_player_class = nullptr;
	g_attached = false;
}

bool enhance::modules::silent_rotation_hook::detached_cleanly()
{
	return g_detached_cleanly;
}

void enhance::modules::silent_rotation_hook::set_fake_rotation(float yaw, float pitch)
{
	g_fake_yaw = yaw;
	g_fake_pitch = pitch;
	g_fake_active.store(true, std::memory_order_release);
}

void enhance::modules::silent_rotation_hook::clear()
{
	// Rotation only. This used to drop the queued attack as well, which was
	// right when killaura was the only user of the slot and wrong the moment
	// the triggerbot started using it: the worker runs the triggerbot first,
	// so an idle killaura clearing every tick ate the attack before the JVM
	// tick could drain it. Cancelling an attack is now explicit, and says
	// whose it is.
	g_fake_active.store(false, std::memory_order_release);
}

void enhance::modules::silent_rotation_hook::cancel_attack(attack_owner owner)
{
	// Only the module that queued it may withdraw it. Attacking a target that
	// is no longer current is exactly what trips the AC checks, so the owner
	// bailing out must still drop its own attack.
	if (g_pending_owner.load(std::memory_order_acquire) != static_cast<int>(owner))
		return;

	jobject old = g_pending_target.exchange(nullptr, std::memory_order_acq_rel);
	if (old && enhance::instance)
	{
		try { auto env = enhance::instance->get_env(); if (env) env->DeleteGlobalRef(old); } catch (...) {}
	}
}

void enhance::modules::silent_rotation_hook::queue_attack(jobject target, attack_owner owner)
{
	if (!target) return;
	auto env = enhance::instance ? enhance::instance->get_env() : nullptr;
	if (!env) return;
	jobject global = env->NewGlobalRef(target);
	if (!global) return;
	g_pending_owner.store(static_cast<int>(owner), std::memory_order_release);
	// Swap in the new target; if there was a pending one we drop it.
	jobject old = g_pending_target.exchange(global, std::memory_order_acq_rel);
	if (old)
	{
		try { env->DeleteGlobalRef(old); } catch (...) {}
	}
}

bool enhance::modules::silent_rotation_hook::is_active()
{
	return g_fake_active.load(std::memory_order_acquire);
}

float enhance::modules::silent_rotation_hook::current_fake_yaw()   { return g_fake_yaw; }
float enhance::modules::silent_rotation_hook::current_fake_pitch() { return g_fake_pitch; }
