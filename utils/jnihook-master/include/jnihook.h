/*
 *  -----------------------------------
 * |         JNIHook - by rdbo         |
 * |      Java VM Hooking Library      |
 *  -----------------------------------
 */

/*
 * Copyright (C) 2023    Rdbo
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License version 3
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _JNIHOOK_H_
#define _JNIHOOK_H_

#include <jni.h>
#include <jvmti.h>

#define JNIHOOK_API
#define JNIHOOK_CALL

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	JNIHOOK_OK = 0,

	JNIHOOK_ERR_GET_JNI,
	JNIHOOK_ERR_GET_JVMTI,
	JNIHOOK_ERR_ADD_JVMTI_CAPS,
	JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK,

	JNIHOOK_ERR_JNI_OPERATION,
	JNIHOOK_ERR_JVMTI_OPERATION,
	JNIHOOK_ERR_CLASS_FILE_CACHE,
	JNIHOOK_ERR_JAVA_EXCEPTION,
} jnihook_result_t;

/**
 * Initializes the JNIHook library
 *
 * @param jvm The Java Virtual Machine that will be instrumented by JNIHook
 * @return JNIHOOK_OK on success, JNIHOOK_ERR_* on failure.
 */
JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Init(JavaVM *jvm);

/**
 * Attaches a hook to a Java method
 * NOTE: Native method signatures are as follows:
 *           ReturnType (*fnPtr)(JNIEnv *env, jobject objectOrClass, ...);
 *
 * @param method The Java method being hooked
 * @param native_hook_method The native method that will be called by the JVM instead of `method`
 * @param original_method (optional) Output variable that will receive a copy of the original (unhooked) method
 * @return JNIHOOK_OK on success, JNIHOOK_ERR_* on failure.
 */
JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Attach(jmethodID method, void *native_hook_method, jmethodID *original_method);

/**
 * Detaches a hook from a Java method
 *
 * @param method The method being unhooked
 * @return JNIHOOK_OK on success, JNIHOOK_ERR_* on failure.
 */
JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Detach(jmethodID method);

/**
 * Returns the JVMTI error behind the most recent
 * JNIHOOK_ERR_JVMTI_OPERATION result, or JVMTI_ERROR_NONE (0) if none has
 * occurred. Diagnostic only -- that result is returned from many places, so
 * on its own it does not identify the failure.
 */
JNIHOOK_API int JNIHOOK_CALL
JNIHook_LastJvmtiError(void);

/**
 * Names the capability set Init managed to acquire: "full",
 * "without can_suspend", "without any_class", "minimal", or "none".
 * Anything but "full" means another agent already holds a capability we
 * would have liked, and the corresponding refinement is switched off.
 */
JNIHOOK_API const char * JNIHOOK_CALL
JNIHook_AcquiredCapabilities(void);

/**
 * Marks the CALLING thread as Minecraft's tick/render thread.
 *
 * Attaching normally needs can_suspend, because between RedefineClasses and
 * RegisterNatives the hooked method is native with no implementation and any
 * thread calling it there dies with UnsatisfiedLinkError. That capability is
 * solo in HotSpot and stays taken for a while after a previous environment is
 * disposed, so re-injections routinely cannot get it.
 *
 * Attaching from the client thread removes the need for it: the only thread
 * that calls the hooked methods is the one performing the attach, and it
 * cannot be inside them while it is here.
 */
JNIHOOK_API void JNIHOOK_CALL
JNIHook_MarkClientThread(void);

/**
 * Waives the can_suspend requirement for attaching.
 *
 * Attaching is not atomic: RedefineClasses makes the method native, and only
 * the RegisterNatives that follows gives it an implementation. can_suspend
 * closes that window by stopping the other threads across it. Without the
 * capability the window is a few microseconds wide, and a thread that calls the
 * method inside it dies with UnsatisfiedLinkError -- which for a method called
 * every frame is close to certain, and for one called every tick is a real but
 * much smaller chance.
 *
 * The capability is solo per JVM and is NOT released promptly when an
 * environment is disposed, so after one unload it can stay unavailable for the
 * rest of the game's life. That makes refusing outright a poor default for a
 * client that is re-injected constantly, hence this explicit opt-in rather than
 * a silent relaxation.
 */
JNIHOOK_API void JNIHOOK_CALL
JNIHook_AllowUnsafeAttach(int allow);

/**
 * Captures a class's bytes exactly as the JVM currently holds them and reports
 * whether another agent's transforms (Mixin) are already baked into them.
 *
 * This is the question the hooking strategy rests on. JNIHook redefines a class
 * from the bytes ClassFileLoadHook hands it; if those bytes already contain
 * Mixin's work, redefining preserves it and instrumented classes are safe to
 * hook. If they are the pre-Mixin original, redefining silently strips other
 * mods -- which is what blanked the world when WorldRenderer was hooked.
 *
 * Nothing is redefined and no hook is installed; it only triggers a
 * retransformation to observe the bytes.
 *
 * @return 1 if Mixin members were found, 0 if none, -2 if the retransform
 *         produced no bytes for this class, -1 on error.
 */
JNIHOOK_API int JNIHOOK_CALL
JNIHook_ProbeClassMixins(jclass clazz, char *out_sample, int out_len, int *out_size);

/**
 * Detaches every hook and shuts down JNIHook
 */
JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Shutdown();

/*
 * Clears the JVMTI event callbacks and disposes the environment, without
 * JNIHook_Shutdown's by-name class restore (which cannot work on a native
 * thread under Fabric). Detach every hooked method first, then call this
 * before the agent module is unloaded — the JVM otherwise keeps calling
 * ClassFileLoadHook at an address that no longer exists.
 */
JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_ReleaseEnvironment();

#ifdef __cplusplus
}
#endif
#endif
