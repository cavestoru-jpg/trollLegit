#pragma once

#include <sdk/includes.h>

// Which Minecraft this DLL landed in, and which spelling of the game's symbols is
// live in that JVM.
//
// The two are separate questions. The version says WHICH symbols exist; the
// namespace says what they are CALLED. A Fabric 1.21.4 and a vanilla 1.21.4 have
// the same API and share nothing in naming, and 26.x dropped obfuscation entirely,
// so its runtime names are Mojang's own.
//
// Detection runs once, before a single symbol is looked up, and never guesses
// silently: an unrecognised version is logged and pinned to the nearest supported
// table, because a client that says which table it fell back to is debuggable and
// one that quietly resolves nothing is not.
namespace sdk
{
	namespace version
	{
		enum class ns_kind
		{
			unknown = -1,
			official = 0,      // net/minecraft/client/Minecraft -- 26.x, or a mojmap instance
			intermediary = 1,  // net/minecraft/class_310        -- Fabric on 1.20-1.21.11
			obfuscated = 2,    // gfj                            -- vanilla on 1.20-1.21.11
		};

		// Reads the game's own version.json off the classpath. Namespace-independent
		// by construction: it is a resource, not a class, so no mapping is needed to
		// find it. Returns false when the version could not be read at all.
		bool detect(JNIEnv* env);

		bool detected();

		// Index into the supported-version table, or -1. Always valid to pass to the
		// mapping table once detect() has returned.
		int ordinal();

		// The id the game reported ("1.21.11", "26.3", or a snapshot id), which is
		// not necessarily a supported version -- see exact().
		const char* reported();

		// The supported version whose table is in use.
		const char* name();

		// False when the reported id was not one of the supported versions and a
		// neighbouring table was substituted.
		bool exact();

		int ordinal_of(const char* version_name);

		// Version comparisons for compat shims. Both take a supported version name:
		//     if (sdk::version::at_least("1.21.2")) ... // render states exist
		bool at_least(const char* version_name);
		bool before(const char* version_name);

		ns_kind ns();
		const char* ns_name();
		void set_ns(ns_kind kind);

		// "1.21.11 (intermediary)" for menus, watermarks and log lines.
		const char* describe();
	}
}
