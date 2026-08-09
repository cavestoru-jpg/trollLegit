#ifndef HOOK_H_
#define HOOK_H_

#include <Windows.h>

namespace Hook
{
	bool init();
	void shutdown();

	bool get_is_init();
	HWND get_window();

	// --- Clean unload -------------------------------------------------------
	// Tearing the client down has to happen in two stages on two threads. The
	// GL and ImGui objects belong to the context the swap hook created, so they
	// can only be destroyed from inside that hook; everything else (JNI hooks,
	// MinHook itself, the window procedure) has to be undone from the worker
	// once the render thread is provably out of our code.
	void request_unload();
	bool unload_requested();

	// True once the render thread has finished its half and stopped entering
	// our drawing path.
	bool render_teardown_done();

	// Blocks until no thread is executing inside the swap hook, or the timeout
	// expires. Removing a hook while a thread sits in its trampoline is how you
	// get a crash seconds after unloading.
	bool wait_until_hook_idle(unsigned timeout_ms);

	// True only when every entry point into this module has been removed and
	// nothing is executing inside it. Freeing the library otherwise is a
	// guaranteed crash on the next frame or window message.
	bool safe_to_unmap();
}

#endif

