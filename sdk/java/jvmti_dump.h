#pragma once

#include <sdk/includes.h>
#include <string>

namespace sdk
{
	namespace java
	{
		// Writes every method (name + signature + modifiers) of a class to the
		// log. Exists so obfuscated names are read off the running game instead
		// of guessed: a wrong guess resolves to a null jmethodID and fails
		// silently, which is far worse to debug than a missing mapping.
		void dump_class_methods(JNIEnv* env, const char* class_sig);

		// Same for fields.
		void dump_class_fields(JNIEnv* env, const char* class_sig);

		// True when the class carries Mixin-injected members (Fabric mods such
		// as Iris or Sodium). Redefining such a class — which is what installing
		// a JNIHook on one of its methods does — can drop those transformations
		// and break whatever the mods were doing, so it must be checked before
		// attaching to anything in the render path.
		bool class_is_mixin_instrumented(JNIEnv* env, const char* class_sig,
		                                 std::string* first_example);

		// Logs every loaded class whose name contains `needle`.
		//
		// For the case where a class that is definitely present cannot be
		// resolved by name: sdk::classloader::find_class swallows the
		// ClassNotFoundException, so a wrong package or a renamed class is
		// indistinguishable from a missing mod. This asks the JVM what it
		// actually has, which settles it in one run. Capped by `limit` — the
		// loaded set is tens of thousands of classes.
		void dump_loaded_classes_matching(JNIEnv* env, const char* needle, int limit);

		// Dumps the classes needed to wire the in-world renderer: WorldRenderer
		// and GameRenderer (hook target), Entity (interpolation fields) and
		// MinecraftClient (render tick counter). Runs at most once per process.
		void dump_render_mappings(JNIEnv* env);
	}
}
