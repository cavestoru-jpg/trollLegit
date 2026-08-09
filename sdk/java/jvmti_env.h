#pragma once

#include <sdk/includes.h>
#include <jvmti.h>

namespace sdk
{
	namespace java
	{
		// Shared JVMTI environment.
		//
		// JNIHook already brings one up and adds the redefine/retransform
		// capabilities (utils/jnihook-master/src/jnihook.cpp). This is a second,
		// independent environment used for read-only introspection, which needs
		// no capabilities beyond the defaults and therefore cannot fail for the
		// live-phase reasons that restrict the interesting ones.
		//
		// Returns nullptr if the JVM is not attached yet.
		jvmtiEnv* jvmti();

		// Disposes the environment. Must run before the module is unmapped:
		// a live JVMTI environment belonging to a freed agent is a dangling
		// registration inside the JVM.
		void dispose_jvmti();
	}
}
