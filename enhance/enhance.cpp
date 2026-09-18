#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/player/player.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/classloader.h>
#include <sdk/version/version.h>
#include "enhance.h"
#include "globals/globals.h"
#include "hooks/Hook.h"
#include "utils/logger.h"
#include "utils/config.h"
#include <sdk/java/jvmti_env.h>
#include <jnihook.h>
#include "gui/GUI.h"
#include "modules/hitbox/hitbox.h"
#include "modules/world_render/world_render_hook.h"
#include "modules/aimassist/aimassist.h"
#include "modules/triggerbot/triggerbot.h"
#include "modules/pearl_catch/pearl_catch.h"
#include "modules/reach/reach.h"
#include "modules/reach/reach_hook.h"
#include "modules/esp/esp.h"
#include "modules/mace/mace.h"
#include "modules/shield_breaker/shield_breaker.h"
#include "modules/auto_anchor/auto_anchor.h"
#include "modules/stun_slam/stun_slam.h"
#include "modules/server_rotation/server_rotation.h"
#include "modules/stap/stap.h"
#include "modules/wtap/wtap.h"
#include "modules/anchor_macro/anchor_macro.h"
#include "modules/storage_esp/storage_esp.h"
#include "modules/autocrystal/autocrystal.h"
#include "modules/autototem/autototem.h"
#include "modules/autojumpreset/autojumpreset.h"
#include "modules/killaura/killaura.h"
#include "modules/aiming/silent_aim.h"
#include "modules/killaura/silent_rotation_hook.h"
#include "modules/aiming/tick_movement_hook.h"
#include <sdk/minecraft/entity/entity.h>
#include "modules/autoclicker/autoclicker.h"
#include "modules/eagle/eagle.h"
#include "modules/teams/teams.h"
// #include "modules/backtrack/backtrack.h" // Disabled
// #include "modules/backtrack/backtrack_hook.h" // Disabled
#include <chrono>

bool enhance::enhance_client::attach()
{
	HMODULE jvm = GetModuleHandleA("jvm.dll");
	if (!jvm)
	{
		return false;
	}

	using t_createdvms = jint(__stdcall*)(JavaVM**, jsize, jsize*);

	FARPROC process_address = GetProcAddress(reinterpret_cast<HMODULE>(jvm), "JNI_GetCreatedJavaVMs");
	if (!process_address)
	{
		return false;
	}
	
	t_createdvms created_java_vms = reinterpret_cast<t_createdvms>(process_address);

	auto ret = created_java_vms(&vm, 1, nullptr);

	if (ret != JNI_OK)
	{
		return false;
	}

	ret = vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr);

	if (ret != JNI_OK)
	{
		return false;
	}

	if (!sdk::classloader::init(env))
	{
		return false;
	}

	if (sdk::classloader::is_fabric())
	{
		printf("[ENHANCE] Detected: Fabric\n");
	}
	else
	{
		printf("[ENHANCE] Detected: Vanilla\n");
	}

	// Every mapping constant is empty until this runs, so it has to happen before
	// any module or hook looks a symbol up -- and it needs the class loader, which
	// is why it sits here and not in run().
	if (!sdk::mappings::bind(env))
	{
		logger::log_error("[ENHANCE] mapping bind failed; the client cannot see the game");
	}
	printf("[ENHANCE] Minecraft: %s\n", sdk::version::describe());

	if (Hook::init())
	{
		return false;
	}

	// Load before the worker starts, so modules never observe defaults that the
	// user had already changed.
	try { enhance::config::load(); } catch (...) {}

	// Killaura hooks are initialised lazily by killaura::run() the first time
	// the user enables the module. Doing it here would crash the game if any
	// of the Yarn mappings turned out to be wrong for the live MC version —
	// we'd rather have killaura silently fail to attach than break startup.

	return true;
}

void enhance::enhance_client::run()
{
	sdk::instance = std::make_unique<sdk::minecraft_client>();
	auto local_player = std::make_unique<player_client>();

	bool unload_key_was_down = false;

	while (true)
	{
		// Unload hotkey. Edge-triggered so holding the key does not fire twice,
		// and checked before anything else so a wedged module cannot stop the
		// client from being removed.
		{
			const bool down = globals::unload_keybind != 0 &&
			                  (GetAsyncKeyState(globals::unload_keybind) & 0x8000) != 0;
			if (down && !unload_key_was_down)
			{
				logger::log(std::string("[unload] requested by hotkey, level=") +
				            std::to_string(globals::unload_level));

				// Level 0 leaves every hook live and only stops the worker, so
				// the render thread must not be asked to tear anything down.
				if (globals::unload_level >= 1)
				{
					Hook::request_unload();

					// Let the render thread destroy the GL and ImGui objects it
					// owns; only it can. If it never gets there (window
					// minimised, game frozen) give up waiting and carry on —
					// leaking a GL context beats never unloading.
					for (int i = 0; i < 300 && !Hook::render_teardown_done(); ++i)
						std::this_thread::sleep_for(std::chrono::milliseconds(10));

					if (!Hook::render_teardown_done())
						logger::log_error("[unload] render thread did not tear down in time");
				}

				break;
			}
			unload_key_was_down = down;
		}

		if (globals::flight_enabled)
		{
			jobject player = local_player->get_player();
			if (player)
			{
				local_player->set_flying(true);
				// local_player->get_player() returns a fresh JNI local ref
				// each call; if we don't free it, the frame slowly fills up
				// (10ms loop → ~100 leaks/sec) and eventually the JVM crashes
				// inside CheckForkedJVM or similar. set_flying internally
				// re-fetches its own player ref + cleans up, so we just need
				// to drop the one we got for the null-check above.
				auto env = enhance::instance->get_env();
				if (env) env->DeleteLocalRef(player);
			}
		}

		// Sprint is driven through the key binding, not LivingEntity.setSprinting.
		// The direct call mutates the player's attribute-modifier map from this
		// thread while the tick thread mutates the same map inside tickMovement,
		// and that map is not thread-safe — it crashed the game with
		// ArrayIndexOutOfBoundsException(-1) inside Object2ObjectArrayMap.remove.
		//
		// Edge-triggered so the key is released exactly once when the module is
		// switched off, instead of being written every 10 ms forever.
		{
			static bool sprint_key_held = false;
			if (globals::sprint_enabled != sprint_key_held)
			{
				local_player->set_sprint_key(globals::sprint_enabled);
				sprint_key_held = globals::sprint_enabled;
			}
			else if (globals::sprint_enabled)
			{
				// Minecraft clears the binding itself in places (menus, respawn),
				// so re-assert it periodically rather than only on the edge.
				static uint64_t last_assert = 0;
				const uint64_t now_ms = GetTickCount64();
				if (now_ms - last_assert > 250)
				{
					last_assert = now_ms;
					local_player->set_sprint_key(true);
				}
			}
		}

		// Process module keybinds before running modules — a Hold/Toggle bind
		// flips the module's *_enabled flag so the bind activates the module
		// without the row toggle needing to be on first.
		tick_module_binds();

		// Rotation rework: the aiming hooks.
		//
		// Opt-in and attempted once, for the same reason as the in-world
		// renderer below — attaching redefines a live class, so it is not
		// something to do speculatively or to retry in a 10 ms loop. Failures
		// are reported by init() and then left alone until the next injection.
		//
		// One hook on ClientPlayerEntity.tick, which contains both halves —
		// sendMovementPackets (what the server is told we are looking at) and,
		// via super.tick, tickMovement -> travel (how the body actually moves).
		// Holding the fake angles across it keeps the two in agreement, which
		// is the whole point; wrapping tickMovement alone moved the body but
		// still reported the real yaw, and that mismatch is what flagged.
		{
			static bool s_tried = false;

			if (globals::silent_rotation_enabled && !s_tried)
			{
				s_tried = true;
				// One-shot probe before anything is hooked. Decides whether a
				// Mixin-instrumented class can be redefined without destroying
				// the other agent's work -- and therefore whether a single hook
				// on Entity.getYaw could serve every consumer of the rotation,
				// or whether each consumer needs wrapping separately.
				{
					// JNIHook must be up first: the probe borrows its JVMTI
					// environment and its ClassFileLoadHook callback. Running
					// before Init just returned "failed" with no error code,
					// which is what the first attempt did.
					if (auto jvm = enhance::instance ? enhance::instance->get_java_vm() : nullptr)
					{
						const jnihook_result_t ir = JNIHook_Init(jvm);
						if (ir != JNIHOOK_OK)
							logger::log_error(std::string("[probe] JNIHook_Init failed=") +
							                  std::to_string((int)ir) + " jvmti_err=" +
							                  std::to_string(JNIHook_LastJvmtiError()));
					}

					auto probe = [](const char* sig, const char* label)
					{
						auto e = enhance::instance ? enhance::instance->get_env() : nullptr;
						if (!e) return;
						jclass c = sdk::classloader::find_class(e, sig);
						if (!c) { logger::log(std::string("[probe] ") + label + ": class not found"); return; }

						char sample[128] = {};
						int  size = 0;
						const int r = JNIHook_ProbeClassMixins(c, sample, sizeof(sample), &size);
						e->DeleteLocalRef(c);

						std::string msg = std::string("[probe] ") + label + " (" + sig + "): ";
						if (r == 1)       msg += "MIXINS PRESENT in the bytes we would redefine from -- sample: " + std::string(sample);
						else if (r == 0)  msg += "no mixin members in those bytes";
						else if (r == -2) msg += "retransform produced no bytes";
						else              msg += "probe failed, jvmti_err=" + std::to_string(JNIHook_LastJvmtiError());
						msg += " | bytes=" + std::to_string(size);
						logger::log(msg);
					};

					probe(sdk::mappings::entity_class_sig,            "Entity");
					probe(sdk::mappings::clientplayerentity_class_sig, "ClientPlayerEntity");
					probe(sdk::mappings::gamerenderer_class_sig,      "GameRenderer");
				}

				JNIHook_AllowUnsafeAttach(globals::silent_rotation_force_attach ? 1 : 0);
				logger::log(std::string("[silent] attaching the tick hook, force=") +
				            (globals::silent_rotation_force_attach ? "yes" : "no"));
				bool ok = false;
				try { ok = enhance::modules::aiming::tick_movement_hook::init(); } catch (...) {}
				if (!ok)
				{
					// Almost always JVMTI can_suspend being unavailable. It is
					// solo per JVM and stays held by any environment that leaked
					// when a session crashed instead of unloading, so it cannot
					// be reclaimed from a fresh injection -- only a game restart
					// frees it. Attaching without it is what crashed the render
					// thread, so refusing is deliberate.
					logger::log_error("[silent] tick hook did NOT attach - the rotation will do "
					                  "nothing. If the reason above is jvmti_err=98, JVMTI "
					                  "can_suspend is held by a leaked environment from an "
					                  "earlier crash in this game session: restart Minecraft.");
				}
			}

			// Nothing to feed from here. The hook reads the player's real
			// angles and drives the rotation manager itself, on the tick
			// thread — the manager has to advance exactly once per game tick,
			// and this loop runs every 10 ms, five times faster. Driving it
			// from here would age requests out five times too quickly and run
			// the processors at five times their intended rate.
		}

		// In-world renderer.
		//
		// The mapping dump is read-only, so it runs once as soon as a world
		// exists. Arming the renderer stays opt-in and waits for the user to
		// turn the feature on — it defines a class into the JVM and takes a
		// JVMTI capability, neither of which belongs in the startup path of a
		// feature nobody asked for.
		//
		// It no longer hooks anything: it subscribes to Fabric's
		// WorldRenderEvents, so no Minecraft class is redefined and no mod's
		// Mixin work is disturbed.
		{
			static bool s_dumped = false;
			static bool s_hook_tried = false;

			if ((!s_dumped || (!s_hook_tried && globals::esp_world_render_enabled)) && sdk::instance)
			{
				jobject world_probe = sdk::instance->get_world();
				if (world_probe)
				{
					auto env = enhance::instance->get_env();
					if (env) env->DeleteLocalRef(world_probe);

					if (!s_dumped)
					{
						s_dumped = true;
						try { enhance::modules::world_render_hook::dump_mappings(); } catch (...) {}
					}

					if (!s_hook_tried && globals::esp_world_render_enabled)
					{
						s_hook_tried = true;
						bool ok = false;
						try { ok = enhance::modules::world_render_hook::init(); } catch (...) {}
						// Untick it rather than leave the menu claiming a
						// feature that refused to install.
						if (!ok)
						{
							globals::esp_world_render_enabled = false;
							// Let the next tick try again. Latching this on a
							// failure meant one bad attempt — a world that was
							// still loading, a class not yet defined — disabled
							// the feature for the rest of the session, with the
							// checkbox silently doing nothing when reticked.
							s_hook_tried = false;
						}
					}
				}
			}
		}

		enhance::modules::hitbox_expander::run();
		enhance::modules::aimassist::run();
		enhance::modules::triggerbot::run();
		enhance::modules::pearl_catch::run();
		enhance::modules::reach::run();
		enhance::modules::esp::run();
		enhance::modules::mace::run();
		enhance::modules::shield_breaker::run();
		enhance::modules::stun_slam::run();
		enhance::modules::stap::run();
		enhance::modules::wtap::run();
		enhance::modules::anchor_macro::run();
		enhance::modules::auto_anchor::run();
		enhance::modules::server_rotation::run();
		enhance::modules::storage_esp::run();
		enhance::modules::autocrystal::run();
		enhance::modules::autototem::run();
		enhance::modules::autojumpreset::run();
		// Before killaura: both walk the entity list, and the aim angles are
		// what the attack should be aligned with.
		enhance::modules::aiming::silent_aim::run();
		enhance::modules::killaura::run();
		enhance::modules::autoclicker::run();
		enhance::modules::eagle::run();
		enhance::modules::teams::run();
		// enhance::modules::backtrack::run(); // Disabled

		// Debounced: writes at most once every 1.5 s, and only when something
		// actually changed.
		try { enhance::config::tick_autosave(); } catch (...) {}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

void enhance::enhance_client::unload()
{
	// Last chance to persist; everything below starts dismantling the client.
	try { enhance::config::save(); } catch (...) {}

	// STOP OUR CODE FROM RUNNING BEFORE DISMANTLING WHAT IT RUNS ON.
	//
	// This used to come last, after sdk::instance was reset, the class loader
	// was released and the thread was detached from the JVM. The swap hook and
	// the window procedure were still installed the whole time, so the render
	// thread kept calling GUI::draw every frame straight into state that was
	// being pulled out from under it. Bisecting the unload made it obvious:
	// level 0 leaves every hook live and still crashed, because the teardown
	// below ran regardless.
	if (globals::unload_level >= 2)
	{
		logger::log("[unload] removing hooks");
		try
		{
			Hook::shutdown();
		}
		catch (...)
		{
		}

		if (!Hook::safe_to_unmap())
		{
			logger::log_error("[unload] hooks could not be fully removed - staying loaded");
			m_safe_to_free = false;
		}
	}
	else
	{
		logger::log("[unload] level<2: hooks stay installed, so nothing else is torn down");
		m_safe_to_free = false;
		logger::log("[unload] done (partial)");
		return;
	}

	sdk::instance.reset();

	if (globals::unload_level < 3)
	{
		logger::log("[unload] level<3: leaving JNI hooks installed");
	}
	else
	try
	{
		enhance::modules::reach::reset_hook_state();
	}
	catch (...)
	{
	}
	
	try
	{
		enhance::modules::reach_hook::shutdown();
	}
	catch (...)
	{
	}

	// Order matters: tick_movement_hook's callback calls into silent_rotation_hook
	// (fire_pending_attack, which reads its cached method ids and the MAIN_HAND
	// global ref). Detach the tick hook FIRST -- JNIHook_Detach takes a safepoint,
	// so once it returns no tick callback can still be running -- and only then
	// tear down silent_rotation_hook and free that global ref. The reverse order
	// let a tick fire between the two and call into half-freed state, which is the
	// unload crash.
	try { enhance::modules::aiming::tick_movement_hook::shutdown(); } catch (...) {}
	try { enhance::modules::silent_rotation_hook::shutdown(); } catch (...) {}
	try { enhance::modules::world_render_hook::shutdown(); } catch (...) {}

	// Every JNIHook rewrites its declaring class to call into this DLL. If any
	// of those rewrites is still in place, freeing the library hands Minecraft
	// a pointer into unmapped memory the next time it calls that method.
	{
		const bool sr = enhance::modules::silent_rotation_hook::detached_cleanly();
		const bool rh = enhance::modules::reach_hook::detached_cleanly();
		const bool tm = enhance::modules::aiming::tick_movement_hook::detached_cleanly();

		m_safe_to_free = sr && rh && tm;

		logger::log(std::string("[unload] jni detach: silent=") + (sr ? "ok" : "FAILED") +
		            " reach=" + (rh ? "ok" : "FAILED") +
		            " tick=" + (tm ? "ok" : "FAILED"));

		if (!m_safe_to_free)
		{
			logger::log_error("[unload] a JNIHook did not detach - staying loaded, "
			                  "unmapping now would crash the game");
		}
	}
	
	// Backtrack module disabled
	/*
	try
	{
		enhance::modules::backtrack::reset_hook_state();
	}
	catch (...)
	{
	}
	
	try
	{
		enhance::modules::backtrack_hook::shutdown();
	}
	catch (...)
	{
	}
	*/
	
	// Everything below severs this module's connection to the JVM, so it only
	// belongs at the level that also detaches the JNI hooks. At lower levels
	// the hooks are gone but the client stays attached, which is harmless.
	if (globals::unload_level >= 3)
	{
		// Release the class-loader global ref while the env is still valid.
		// Detaching first left `env` dangling and cleanup ran against a dead
		// pointer — which is also why the loader fell back to the
		// AppClassLoader and produced NoClassDefFoundError for net/minecraft/*
		// on the way out.
		try
		{
			if (env)
			{
				sdk::classloader::cleanup(env);
			}
		}
		catch (...)
		{
		}

		if (vm)
		{
			try
			{
				vm->DetachCurrentThread();
			}
			catch (...)
			{
			}
		}

		env = nullptr;
		vm = nullptr;

		// Every class is restored by now. Release the JVMTI environments LAST:
		// JNIHook registers ClassFileLoadHook as a raw function pointer into
		// this module and the JVM invokes it on every class load, so leaving it
		// set is a guaranteed crash the moment Minecraft loads anything.
		try { JNIHook_ReleaseEnvironment(); } catch (...) {}
		try { sdk::java::dispose_jvmti(); } catch (...) {}
		logger::log("[unload] jvmti environments released");
	}

	logger::log(std::string("[unload] safe_to_free=") + (m_safe_to_free ? "yes" : "no"));
}

std::unique_ptr<enhance::enhance_client> enhance::instance;
