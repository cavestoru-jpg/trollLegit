#include "client_thread.h"

#include <mutex>
#include <vector>

namespace
{
	std::mutex g_mutex;
	std::vector<std::function<void()>> g_pending;
}

void enhance::client_thread::post(std::function<void()> fn)
{
	if (!fn) return;

	// Runs inline, on the caller's thread.
	//
	// The queue was drained from the wglSwapBuffers hook so that attaches would
	// happen on Minecraft's thread and could skip can_suspend. That position
	// turned out to be inside a native downcall from Java, where the JVMTI
	// class redefinition an attach performs cannot safely take its safepoint --
	// it crashed the render thread inside jvm.dll. Until there is a drain site
	// where the client thread is executing Java rather than sitting in a native
	// call, deferring buys nothing and costs a crash, so the work is done where
	// it was asked for and JNIHook's can_suspend guard decides whether it may
	// proceed.
	try { fn(); } catch (...) {}
}

void enhance::client_thread::drain()
{
	std::vector<std::function<void()>> work;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_pending.empty())
			return;
		// Swap the queue out before running anything: a task may post more
		// work, and holding the lock across a JNI call is how a deadlock with
		// the worker starts.
		work.swap(g_pending);
	}

	for (auto& fn : work)
	{
		// One task throwing must not cost the others, and must never escape
		// into the render hook -- an exception loose there aborts the process.
		try { fn(); } catch (...) {}
	}
}

void enhance::client_thread::clear()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_pending.clear();
}
