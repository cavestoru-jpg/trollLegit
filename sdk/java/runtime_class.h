#pragma once

#include <sdk/includes.h>
#include <cstddef>

namespace sdk
{
	namespace java
	{
		// Defines a class straight from a bytecode buffer in the DLL's memory —
		// nothing is ever written to disk. The class lands in the loader the game
		// resolves through (Fabric's KnotClassLoader), so it can reference the
		// game's own dependencies such as org.lwjgl.
		//
		// `binary_name` is the internal form with slashes, e.g.
		// "enhance/EnhanceRenderer". Returns a GlobalRef owned by this module,
		// or nullptr on failure. Calling twice for the same name returns the
		// already-defined class rather than failing with a duplicate definition.
		jclass define_from_memory(JNIEnv* env, const char* binary_name,
		                          const unsigned char* bytes, size_t len);

		// Drops the cached global refs. Does not undefine anything — the JVM has
		// no such operation.
		void forget_defined_classes(JNIEnv* env);
	}
}
