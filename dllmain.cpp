#include <sdk/includes.h>
#include <enhance/enhance.h>
#include <enhance/hooks/Hook.h>
#include <enhance/utils/logger.h>
#include <enhance/globals/globals.h>

static HMODULE g_hModule = nullptr;

// Returns true when the module should be unmapped.
static bool run_client()
{
    logger::init(g_hModule);

    enhance::instance = std::make_unique<enhance::enhance_client>();

    if (!enhance::instance->attach())
    {
        logger::shutdown();
        return true;
    }

    // Returns when the unload hotkey is pressed.
    enhance::instance->run();

    // Detaches the JNI hooks, puts the scoreboard back, removes the MinHook
    // trampolines and restores the window procedure. Hook::shutdown waits for
    // the render thread to actually leave our code before pulling anything out.
    bool safe = false;
    try
    {
        enhance::instance->unload();
        safe = enhance::instance->safe_to_free();
    }
    catch (...)
    {
        safe = false;
    }

    // Destroying the client object invalidates enhance::instance, which the
    // hooks dereference. Only safe once they are actually gone.
    if (globals::unload_level >= 2)
        enhance::instance.reset();

    if (safe && globals::unload_level < 4)
    {
        logger::log("[unload] teardown complete; module left mapped. "
                    "Re-injecting works anyway - the injector writes a "
                    "uniquely named copy each run.");
        logger::shutdown();
        return false;
    }

    if (!safe)
    {
        // Hooks are off and the client is inert, but something could not be
        // fully undone. Unmapping would turn that into a crash on Minecraft's
        // next frame, so the module stays resident.
        logger::log_error("[unload] teardown incomplete - the DLL must stay loaded");
        logger::shutdown();
        return false;
    }

    logger::log("[unload] complete, freeing the library");
    logger::shutdown();
    return true;
}

void __stdcall enhance_thread(HINSTANCE instance)
{
    g_hModule = instance;

    bool free_module = false;

    // A teardown thread must never let an exception escape. This one is started
    // with a raw CreateThread, so there is no CRT entry wrapper and no handler
    // above it: anything thrown here goes straight to std::terminate -> abort
    // -> __fastfail, which surfaces as exit code 0xC0000409. That is the same
    // code Control Flow Guard uses, which is exactly what made a plain
    // recursive-mutex throw inside logger::shutdown look for a long time like a
    // stale function pointer surviving FreeLibrary.
    try
    {
        free_module = run_client();
    }
    catch (...)
    {
        // Nothing useful can be logged here — the logger is the most likely
        // thing to have thrown. Stay mapped and let the user restart.
        free_module = false;
    }

    if (!free_module)
        return;

    // Must be the last thing this thread does: the code it is executing is
    // about to be unmapped, so there is no returning from here.
    FreeLibraryAndExitThread(g_hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE h_module, DWORD ul_reason_for_call, LPVOID lp_reserved)
{
    if (ul_reason_for_call != DLL_PROCESS_ATTACH)
        return FALSE;

    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)enhance_thread, h_module, 0, 0);

    return TRUE;
}
