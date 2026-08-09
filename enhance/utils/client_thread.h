#pragma once

#include <functional>

// A place to run work on Minecraft's tick/render thread.
//
// Modules live on the enhance worker, but installing a JNI hook is not safe
// from there. JNIHook makes the target method native with RedefineClasses and
// only then binds it with RegisterNatives; a thread calling the method in
// between gets UnsatisfiedLinkError, which for something Minecraft calls every
// frame is an instant crash. JVMTI's can_suspend closes that window, but it is
// a solo capability -- one environment per JVM -- and stays held by any
// environment that leaked when a session crashed, so after a re-injection it is
// routinely unavailable.
//
// Running the attach on the client thread removes the need for it: the only
// thread that calls the hooked methods is the one performing the attach, and it
// cannot be inside one of them while it is here.

namespace enhance::client_thread
{
	// Queue `fn` to run once, on the client thread, at the start of the next
	// frame. Safe to call from any thread. The callable is copied.
	void post(std::function<void()> fn);

	// Run everything queued. Called only from the client thread, by the
	// wglSwapBuffers hook.
	void drain();

	// Drop anything still queued. For unload: the worker is going away and
	// nothing queued by it may run afterwards.
	void clear();
}
