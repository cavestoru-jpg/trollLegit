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

#include <jnihook.h>
#include <unordered_map>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include "classfile.hpp"
#include "uuid.hpp"

typedef struct jnihook_t {
        JavaVM   *jvm;
        jvmtiEnv *jvmti;
} jnihook_t;

typedef struct method_info_t {
        std::string name;
        std::string signature;
        jint access_flags;
} method_info_t;

typedef struct hook_info_t {
        method_info_t method_info;
        void *native_hook_method;
} hook_info_t;

static std::unique_ptr<jnihook_t> g_jnihook = nullptr;
// Last JVMTI error behind a JNIHOOK_ERR_JVMTI_OPERATION result. Diagnostic
// only; read through JNIHook_LastJvmtiError().
static jvmtiError g_last_jvmti_error = JVMTI_ERROR_NONE;

// Words for the last failure. A result code says which step failed but not why,
// and the Java exception that explains it is cleared before the caller can look
// at it. Read through JNIHook_LastErrorDetail().
static std::string g_last_error_detail;

// Records the pending Java exception (class name and message) and clears it.
static void record_exception(JNIEnv *env, const char *what)
{
        g_last_error_detail = what ? what : "";

        jthrowable ex = env->ExceptionOccurred();
        if (!ex) {
                return;
        }
        env->ExceptionClear();

        jclass throwable_class = env->GetObjectClass(ex);
        jmethodID to_string = throwable_class
                ? env->GetMethodID(throwable_class, "toString", "()Ljava/lang/String;")
                : nullptr;
        if (to_string) {
                jstring text = reinterpret_cast<jstring>(env->CallObjectMethod(ex, to_string));
                if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                } else if (text) {
                        const char *chars = env->GetStringUTFChars(text, nullptr);
                        if (chars) {
                                g_last_error_detail += ": ";
                                g_last_error_detail += chars;
                                env->ReleaseStringUTFChars(text, chars);
                        }
                        env->DeleteLocalRef(text);
                }
        }
        if (throwable_class) {
                env->DeleteLocalRef(throwable_class);
        }
        env->DeleteLocalRef(ex);
}

// Pulls "<name><descriptor>" out of a VerifyError's Location line:
//
//   Location:
//     net/minecraft/.../Player_<uuid>.<init>(Lnet/.../Level;L...;)V @128: invokespecial
//
// Returns false when the message does not carry one, which is the signal to stop
// trying rather than to guess.
static bool parse_verify_error_method(const std::string &detail,
                                      const std::string &copy_class_name,
                                      std::string &name_out,
                                      std::string &descriptor_out)
{
        const std::string needle = copy_class_name + ".";
        size_t at = detail.find(needle);
        if (at == std::string::npos)
                return false;

        at += needle.size();
        size_t end = detail.find(" @", at);
        if (end == std::string::npos) {
                end = detail.find('\n', at);
        }
        if (end == std::string::npos || end <= at)
                return false;

        const std::string signature = detail.substr(at, end - at);
        const size_t paren = signature.find('(');
        if (paren == std::string::npos)
                return false;

        name_out = signature.substr(0, paren);
        descriptor_out = signature.substr(paren);
        return !name_out.empty() && descriptor_out.size() > 1;
}

// Makes one method of the copy native and drops its body, so the verifier has
// nothing to check there.
//
// The copy exists for exactly one purpose: to hold the ORIGINAL body of the
// method being hooked, so the hook can still call it. Every other method in it is
// dead weight -- and dead weight that can refuse to verify. Renaming the class
// makes `this` a type no other class knows, so any method that hands `this` to
// code expecting the real class (a constructor calling a helper, a Mixin handler
// calling a Fabric callback) fails verification and takes the whole copy with it.
//
// Rather than stub every method up front -- which would break a hooked method that
// calls a private sibling -- the verifier is allowed to name its own casualties.
static bool stub_method_body(ClassFile &cf, const std::string &name,
                             const std::string &descriptor)
{
        // A reference: get_methods() hands back the vector itself, and a copy would
        // drop the stub on the floor.
        auto &methods = cf.get_methods();
        for (auto &method : methods) {
                auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.name_index).bytes.data());
                auto desc_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.descriptor_index).bytes.data());

                const std::string method_name(name_ci->bytes, &name_ci->bytes[name_ci->length]);
                const std::string method_desc(desc_ci->bytes, &desc_ci->bytes[desc_ci->length]);

                if (method_name != name || method_desc != descriptor)
                        continue;
                if ((method.access_flags & ACC_NATIVE) == ACC_NATIVE)
                        return false;           // already stubbed; retrying would loop

                method.access_flags |= ACC_NATIVE;
                for (size_t i = 0; i < method.attributes.size(); ++i) {
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(
                                        method.attributes[i].attribute_name_index).bytes.data());
                        const std::string attr_name(attr_name_ci->bytes,
                                                    &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name == "Code") {
                                method.attributes.erase(method.attributes.begin() + i);
                                break;
                        }
                }
                return true;
        }
        return false;
}

// Which capability set Init actually managed to acquire, and whether it
// includes can_suspend. Attach must not try to suspend threads without it --
// SuspendThread would fail on every thread and the hook would be applied with
// no protection at all, which is worse than knowingly skipping the step.
static bool        g_can_suspend   = false;

// Set by JNIHook_AllowUnsafeAttach. Waives the can_suspend requirement, at the
// caller's stated risk -- see that function's documentation.
static bool        g_allow_unsafe_attach = false;

// Set on the thread that runs Minecraft's tick and render loop. Attaching from
// that thread is safe without can_suspend: the unbound window exists only
// between RedefineClasses and RegisterNatives, and the sole thread that calls
// the methods we hook is the one executing the attach, so it cannot be inside
// one of them at the time.
static thread_local bool t_client_thread = false;
static const char *g_caps_acquired = "none";

// One-shot probe: capture a class's bytes as the JVM currently has them and
// report whether another agent's transforms are already baked in. Answers the
// question the whole hooking strategy rests on -- if RedefineClasses would
// preserve those transforms, a Mixin-instrumented class can be hooked safely
// and one hook on Entity.getYaw could serve every consumer at once; if not,
// each consumer needs its own hook.
static bool        g_probe_active  = false;
static std::string g_probe_class;
static bool        g_probe_seen    = false;
static bool        g_probe_mixins  = false;
static std::string g_probe_sample;
static int         g_probe_size    = 0;

// Methods the verifier refused to accept in a renamed copy, per class. Rebuilt
// into every subsequent copy of that class as a native stub with no body. Grows
// only when a definition or a link actually fails, so a class that verifies
// cleanly never gets one.
static std::unordered_map<std::string, std::set<std::pair<std::string, std::string>>> g_forced_stubs;

static std::unordered_map<std::string, std::vector<hook_info_t>> g_hooks;
static std::unordered_map<std::string, std::unique_ptr<ClassFile>> g_class_file_cache;
static std::unordered_map<std::string, jclass> g_original_classes;

static std::string
get_class_signature(jvmtiEnv *jvmti, jclass clazz)
{
        char *sig;
        
        if (jvmti->GetClassSignature(clazz, &sig, NULL) != JVMTI_ERROR_NONE) {
                return "";
        }

        std::string signature = std::string(sig, &sig[strlen(sig)]);

        jvmti->Deallocate(reinterpret_cast<unsigned char *>(sig));

        return signature;
}

static std::string
get_class_name(JNIEnv *env, jclass clazz)
{
        // Calling getName() on nothing throws, and a caller that only checks the
        // return value leaves that exception pending on the thread.
        if (!clazz)
                return "";

        jclass klass = env->FindClass("java/lang/Class");
        if (!klass)
                return "";

        jmethodID getName_method = env->GetMethodID(klass, "getName", "()Ljava/lang/String;");
        if (!getName_method)
                return "";

        jstring name_obj = reinterpret_cast<jstring>(env->CallObjectMethod(clazz, getName_method));
        if (!name_obj)
                return "";

        const char *c_name = env->GetStringUTFChars(name_obj, 0);
        if (!c_name)
                return "";

        std::string name = std::string(c_name, &c_name[strlen(c_name)]);

        env->ReleaseStringUTFChars(name_obj, c_name);

        // Replace dots with slashes to match contents of ClassFile
        for (size_t i = 0; i < name.length(); ++i) {
                if (name[i] == '.')
                        name[i] = '/';
        }
        
        return name;
}

static std::unique_ptr<method_info_t>
get_method_info(jvmtiEnv *jvmti, jmethodID method)
{
        char *name;
        char *sig;
        jint access_flags;
        
        if (jvmti->GetMethodName(method, &name, &sig, NULL) != JVMTI_ERROR_NONE)
                return nullptr;

        if (jvmti->GetMethodModifiers(method, &access_flags) != JVMTI_ERROR_NONE)
                return nullptr;

        std::string name_str(name, &name[strlen(name)]);
        std::string signature_str(sig, &sig[strlen(sig)]);

        jvmti->Deallocate(reinterpret_cast<unsigned char *>(name));
        jvmti->Deallocate(reinterpret_cast<unsigned char *>(sig));

        return std::make_unique<method_info_t>(method_info_t { name_str, signature_str, access_flags });
}

void JNICALL JNIHook_ClassFileLoadHook(jvmtiEnv *jvmti_env,
                                       JNIEnv* jni_env,
                                       jclass class_being_redefined,
                                       jobject loader,
                                       const char* name,
                                       jobject protection_domain,
                                       jint class_data_len,
                                       const unsigned char* class_data,
                                       jint* new_class_data_len,
                                       unsigned char** new_class_data)
{
        // An ordinary class load has nothing being redefined, and this hook has
        // nothing to say about one: it exists to catch the bytes of a class on
        // its way through RedefineClasses. Returning here also keeps the common
        // case free -- this runs for every class the JVM ever loads.
        if (class_being_redefined == nullptr)
                return;

        auto class_name = get_class_name(jni_env, class_being_redefined);

        // Whatever happened above, this callback must not hand the VM back a
        // thread with an exception pending. It returns into whatever was loading
        // the class -- ClassLoader.defineClass1 for an ordinary load -- which
        // reports it as its own failure, and a NullPointerException from
        // defineClass1 looks nothing like a JVMTI callback misbehaving.
        if (jni_env->ExceptionCheck())
                jni_env->ExceptionClear();

        if (g_probe_active && class_name == g_probe_class) {
                g_probe_seen = true;
                g_probe_size = static_cast<int>(class_data_len);

                // Mixin's injected members keep their names in the constant
                // pool, so a raw scan of the class file finds them without
                // parsing it.
                static const char *markers[] = {
                        "handler$", "redirect$", "modify$", "mixinextras$",
                        "wrapOperation$", "cancellable$",
                };

                for (const char *marker : markers) {
                        const size_t mlen = strlen(marker);
                        if (class_data_len < static_cast<jint>(mlen))
                                continue;

                        for (jint i = 0; i + static_cast<jint>(mlen) <= class_data_len; ++i) {
                                if (memcmp(class_data + i, marker, mlen) == 0) {
                                        g_probe_mixins = true;

                                        // Copy the readable tail of the name for the report.
                                        jint end = i;
                                        while (end < class_data_len && end - i < 96) {
                                                const unsigned char c = class_data[end];
                                                if (c < 0x20 || c > 0x7e) break;
                                                ++end;
                                        }
                                        g_probe_sample.assign(
                                                reinterpret_cast<const char *>(class_data + i), end - i);
                                        return;
                                }
                        }
                }
                return;
        }

        // Don't do anything for unhooked classes
        if (class_name == "" || g_hooks.find(class_name) == g_hooks.end() || g_hooks[class_name].size() == 0)
                return;

        // Cache parsed ClassFile if it's not cached yet
        if (g_class_file_cache.find(class_name) == g_class_file_cache.end()) {
                auto cf = ClassFile::load(class_data);
                if (!cf)
                        return;

                g_class_file_cache[class_name] = std::move(cf);
        }

        return;
}

// Binds every hook currently registered on `clazz_name` to its native stub.
//
// This must run after ANY redefinition of the class, not just after the one
// that installed a given hook. RedefineClasses drops the class's native method
// bindings, and UnregisterNatives clears all of them at once -- neither is
// per-method. So with two hooks on one class the original code left the older
// one native with nothing behind it, and the next call to it threw
// UnsatisfiedLinkError. For a method Minecraft calls every tick that is an
// instant crash.
//
// Seen as: hooking ClientPlayerEntity.tick and .sendMovementPackets together,
// where enabling the second killed the first.
static bool
RegisterAllNatives(JNIEnv *env, jclass clazz, const std::string &clazz_name)
{
        auto it = g_hooks.find(clazz_name);
        if (it == g_hooks.end() || it->second.empty())
                return true;

        std::vector<JNINativeMethod> natives;
        natives.reserve(it->second.size());

        for (auto &hk_info : it->second) {
                JNINativeMethod nm;
                nm.name = const_cast<char *>(hk_info.method_info.name.c_str());
                nm.signature = const_cast<char *>(hk_info.method_info.signature.c_str());
                nm.fnPtr = hk_info.native_hook_method;
                natives.push_back(nm);
        }

        const jint rc = env->RegisterNatives(clazz, natives.data(),
                                             static_cast<jint>(natives.size()));
        if (env->ExceptionCheck())
                env->ExceptionClear();

        return rc >= 0;
}

// Patches up a class with the current hooks (if any)
// and redefines it using JVMTI
jnihook_result_t
ReapplyClass(jclass clazz, std::string clazz_name)
{
        jvmtiClassDefinition class_definition;

        auto cf = *g_class_file_cache[clazz_name];

        auto constant_pool = cf.get_constant_pool();

        // Patch class file
        // NOTE: The `methods` attribute only has the methods defined by the main class of this ClassFile
        //       Method references are not included here
        //       If the source file has more than one class, they are compiled as separate ClassFiles
        for (auto &method : cf.get_methods()) {
                auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.name_index).bytes.data()
                );

                auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                        cf.get_constant_pool_item(method.descriptor_index).bytes.data()
                );

                auto name = std::string(name_ci->bytes, &name_ci->bytes[name_ci->length]);
                auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                // Check if the current method is a method that should be hooked
                // TODO: Use hashmap for faster lookup
                bool should_hook = false;
                for (auto &hk_info : g_hooks[clazz_name]) {
                        auto &minfo = hk_info.method_info;
                        if (minfo.name == name && minfo.signature == descriptor) {
                                should_hook = true;
                                break;
                        }
                }
                if (!should_hook)
                        continue;

                // Set method to native
                method.access_flags |= ACC_NATIVE;

                // Remove "Code" attribute
                for (size_t i = 0; i < method.attributes.size(); ++i) {
                        auto attr = method.attributes[i];
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(attr.attribute_name_index).bytes.data()
                        );
                        auto attr_name = std::string(attr_name_ci->bytes, &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name == "Code") {
                                method.attributes.erase(method.attributes.begin() + i);
                                break;
                        }
                }
        }

        // Redefine class with modified ClassFile
        auto cf_bytes = cf.bytes();

        class_definition.klass = clazz;
        class_definition.class_byte_count = cf_bytes.size();
        class_definition.class_bytes = cf_bytes.data();

        // Keep the real error. JNIHOOK_ERR_JVMTI_OPERATION is returned from a
        // dozen places, so on its own it says nothing about what went wrong --
        // and the interesting failures here (UNMODIFIABLE_CLASS,
        // FAILS_VERIFICATION, UNSUPPORTED_REDEFINITION_*) are told apart only
        // by this code.
        const jvmtiError rc = g_jnihook->jvmti->RedefineClasses(1, &class_definition);
        if (rc != JVMTI_ERROR_NONE) {
                g_last_jvmti_error = rc;
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Init(JavaVM *jvm)
{
        jvmtiEnv *jvmti;
        jvmtiCapabilities capabilities;
        jvmtiEventCallbacks callbacks = {};

        // Already initialised. Without this an Init after a failed one would
        // acquire a second environment and leak the first.
        if (g_jnihook) {
                // Retry the one capability that decides whether anything can be
                // hooked at all. can_suspend is solo in HotSpot and stays taken
                // for a while after a previous environment is disposed, so an
                // environment created moments after an unload starts without
                // it. AddCapabilities is additive, so it can simply be asked
                // for again later instead of writing off the whole session.
                if (!g_can_suspend) {
                        jvmtiCapabilities want;
                        memset(&want, 0, sizeof(want));
                        want.can_suspend = 1;

                        if (g_jnihook->jvmti->AddCapabilities(&want) == JVMTI_ERROR_NONE) {
                                g_can_suspend = true;
                                g_caps_acquired = "can_suspend acquired late";
                        }
                }
                return JNIHOOK_OK;
        }

        if (jvm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
                return JNIHOOK_ERR_GET_JVMTI;
        }

        // Acquire capabilities by degrading, not all-or-nothing.
        //
        // The original code passed the entire GetPotentialCapabilities set to
        // AddCapabilities, i.e. asked for every capability the JVM offered.
        // AddCapabilities is all-or-nothing, and JVMTI lets some capabilities
        // be held by only one environment at a time, so one already-taken
        // capability sank the whole request -- including the handful actually
        // needed. Observed as ERR_ADD_JVMTI_CAPS with JVMTI_ERROR_NOT_AVAILABLE
        // (98) on every injection after the first into a given game session.
        //
        // Only two capabilities are essential: can_redefine_classes and
        // can_retransform_classes. The rest are refinements:
        //
        //   can_redefine_any_class / can_retransform_any_class
        //       needed only for classes the JVM would otherwise protect
        //       (bootstrap classes). Minecraft's are loaded by Knot, so these
        //       are not required to hook game code.
        //   can_suspend
        //       lets the attach pause other threads while the class is being
        //       swapped. A safety margin, not a requirement -- RedefineClasses
        //       is itself safe at a safepoint. can_suspend is a solo
        //       capability in HotSpot, so it is the first thing to lose when
        //       another agent is present.
        //
        // Try richest first and fall back, so a busy JVM costs us a refinement
        // rather than the feature.
        // can_suspend first and always: JNIHook_Attach refuses without it, so
        // trading it away for the optional any_class pair would leave an
        // environment that cannot hook anything.
        static const struct { bool any_class; bool suspend; const char *what; } k_attempts[] = {
                { true,  true,  "full"                },
                { false, true,  "without any_class"   },
                { true,  false, "without can_suspend" },
                { false, false, "minimal"             },
        };

        bool acquired = false;
        for (const auto &attempt : k_attempts) {
                memset(&capabilities, 0, sizeof(capabilities));
                capabilities.can_redefine_classes = 1;
                capabilities.can_retransform_classes = 1;
                if (attempt.any_class) {
                        capabilities.can_redefine_any_class = 1;
                        capabilities.can_retransform_any_class = 1;
                }
                if (attempt.suspend)
                        capabilities.can_suspend = 1;

                const jvmtiError e = jvmti->AddCapabilities(&capabilities);
                if (e == JVMTI_ERROR_NONE) {
                        g_can_suspend = attempt.suspend;
                        g_caps_acquired = attempt.what;
                        acquired = true;
                        break;
                }

                // Keep the last failure; if every attempt fails this is what
                // the caller gets told.
                g_last_jvmti_error = e;
        }

        if (!acquired) {
                jvmti->DisposeEnvironment();
                return JNIHOOK_ERR_ADD_JVMTI_CAPS;
        }

        callbacks.ClassFileLoadHook = JNIHook_ClassFileLoadHook;
        const jvmtiError cb_err = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
        if (cb_err != JVMTI_ERROR_NONE) {
                g_last_jvmti_error = cb_err;
                jvmti->DisposeEnvironment();
                return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
        }

        g_jnihook = std::make_unique<jnihook_t>(jnihook_t { jvm, jvmti });

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Attach(jmethodID method, void *native_hook_method, jmethodID *original_method)
{
        // Callers can reach here after teardown; g_jnihook is a unique_ptr and
        // dereferencing it null is an immediate fastfail rather than an error.
        if (!g_jnihook) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        // Refuse to attach without can_suspend.
        //
        // Attaching is not atomic: ReapplyClass makes the method native, and
        // only the RegisterNatives that follows gives it an implementation.
        // Between the two the method is native and empty, and any thread that
        // calls it in that window dies with UnsatisfiedLinkError. Suspending
        // the other threads is what closes the window.
        //
        // Not theoretical: hooking PlayerEntity.getEntityInteractionRange
        // without it crashed the game immediately, because the render thread
        // calls that method every frame.
        //
        // The capability is solo in HotSpot -- one environment at a time -- and
        // stays briefly unavailable after a previous environment is disposed,
        // so a re-injection a few seconds after an unload can land here.
        // Failing the attach costs one feature for the session; attaching
        // anyway costs the whole game.
        if (!g_can_suspend && !t_client_thread && !g_allow_unsafe_attach) {
                return JNIHOOK_ERR_ADD_JVMTI_CAPS;
        }

        jclass clazz;
        std::string clazz_name;
        hook_info_t hook_info;
        jobject class_loader;
        JNIEnv *env;

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvmti->GetMethodDeclaringClass(method, &clazz) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        clazz_name = get_class_name(env, clazz);
        if (clazz_name.length() == 0) {
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        auto method_info = get_method_info(g_jnihook->jvmti, method);
        if (!method_info) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        hook_info.method_info = *method_info;
        hook_info.native_hook_method = native_hook_method;

        // Force caching of the class being hooked
        if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                if (g_jnihook->jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
                        return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
                }

                // Temporarily register hook in g_hooks so that `ClassFileLoadHook` can see it
                // Leaving it there could be a problem if this hook fails, it will still patch
                // the class when JNIHook_Attach is called again for that same class, but won't
                // register the native method, causing `java.lang.UnsatisfiedLinkError`.
                g_hooks[clazz_name].push_back(hook_info);
                auto result = g_jnihook->jvmti->RetransformClasses(1, &clazz);
                g_hooks[clazz_name].pop_back();

                // NOTE: We disable the ClassFileLoadHook here because it breaks
                //       any `env->DefineClass()` calls. Also, it's not necessary
                //       to keep it active at all times, we just have to use it for caching
                //       uncached hooked classes.
                // TODO: Investigate why it breaks it (possibly NullPointerException in
                //       JNIHook_ClassFileLoadHook)
                if (g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
                        return JNIHOOK_ERR_SETUP_CLASS_FILE_LOAD_HOOK;
                }

                if (result != JVMTI_ERROR_NONE)
                        return JNIHOOK_ERR_CLASS_FILE_CACHE;

                if (g_class_file_cache.find(clazz_name) == g_class_file_cache.end()) {
                        return JNIHOOK_ERR_CLASS_FILE_CACHE;
                }
        }

        // Make copy of the class prior to hooking it
        // (allows calling the original functions)
        if (g_original_classes.find(clazz_name) == g_original_classes.end()) {
                std::string class_copy_name = clazz_name + "_" + GenerateUuid();
                std::string class_shortname = class_copy_name.substr(class_copy_name.find_last_of('/') + 1);
                std::string class_copy_source_name = class_shortname + ".java";
                jclass class_copy;
                auto cf = *g_class_file_cache[clazz_name];

                // Whatever the verifier rejected in an earlier attempt at this
                // class goes back in as a bodiless native method.
                for (const auto &stub : g_forced_stubs[clazz_name]) {
                        stub_method_body(cf, stub.first, stub.second);
                }

                // Patch source file name (Java will refuse to define the class otherwise)
                for (auto &attr : cf.get_attributes()) {
                        auto attr_name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(attr.attribute_name_index).bytes.data()
                        );
                        auto attr_name = std::string(attr_name_ci->bytes, &attr_name_ci->bytes[attr_name_ci->length]);
                        if (attr_name != "SourceFile")
                                continue;

                        u2 attr_index_be = ((attr.attribute_name_index >> 8) & 0xff) |
                                           ((attr.attribute_name_index & 0xff) << 8);
                        u2 source = *reinterpret_cast<u2 *>(attr.info.data());

                        // Some classes have 'SourceFile' attribute be equal to 'SourceFile',
                        // and not 'ClassName.java'. For those, we won't set a custom source.
                        if (source == attr_index_be)
                                break;

                        // Overwrite constant pool item
                        CONSTANT_Utf8_info ci;
                        cp_info sourcefile_cpi;
                        ci.tag = CONSTANT_Utf8;
                        ci.length = static_cast<u2>(class_copy_source_name.size());

                        sourcefile_cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                        memcpy(sourcefile_cpi.bytes.data(), &ci, sizeof(ci));
                        memcpy(&sourcefile_cpi.bytes.data()[sizeof(ci)], class_copy_source_name.c_str(), ci.length);

                        cf.set_constant_pool_item_be(source, sourcefile_cpi);
                }

                // Patch class name (Java will refuse to define the class otherwise)
                for (auto &cpi : cf.get_constant_pool()) {
                        if (cpi.bytes[0] != CONSTANT_Class)
                                continue;

                        auto class_ci = reinterpret_cast<CONSTANT_Class_info *>(
                                cpi.bytes.data()
                        );

                        auto name_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(class_ci->name_index).bytes.data()
                        );

                        auto name = std::string(name_ci->bytes, &name_ci->bytes[name_ci->length]);

                        if (name == clazz_name) {
                                // Overwrite constant pool item
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(class_copy_name.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], class_copy_name.c_str(), ci.length);

                                cf.set_constant_pool_item(class_ci->name_index, cpi);
                                break; // TODO: Assure that the ClassName can only happen once per ClassFile!
                        }
                }

                // Patch NameAndType things that instance the current class
                // NOTE: This is an attempt to fix the following exception when
                //       trying to get the method ID after defining the class:
                //
                // Type 'OrigClass' (current frame, stack[0]) is not assignable to 'OrigClass_<UUID>'
                auto constant_pool = cf.get_constant_pool();
                for (auto &item : constant_pool) {
                        if (item.bytes.empty() || item.bytes[0] != CONSTANT_NameAndType)
                                continue;

                        auto nt_ci = reinterpret_cast<CONSTANT_NameAndType_info *>(item.bytes.data());
                        auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info *>(
                                cf.get_constant_pool_item(nt_ci->descriptor_index).bytes.data()
                        );
                        auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                        std::string clazz_desc = std::string("L") + clazz_name + ";";
                        std::string clazz_copy_desc = std::string("L") + class_copy_name + ";";
                        if (auto index = descriptor.find(clazz_desc); index != descriptor.npos) {
                                // Overwrite constant pool item
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;
                                std::string new_descriptor = descriptor.replace(index, clazz_desc.size(), clazz_copy_desc);

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(new_descriptor.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], new_descriptor.c_str(), ci.length);

                                cf.set_constant_pool_item(nt_ci->descriptor_index, cpi);
                        }
                }

                // Patch method descriptors
                // NOTE: Not every Type or return Type is referenced by a NameAndType
                //       So we have to check the method descriptors as well
                auto methods = cf.get_methods();
                for (auto& method : methods)
                {
                        auto descriptor_index = method.descriptor_index;

                        auto descriptor_ci = reinterpret_cast<CONSTANT_Utf8_info*>(
                                cf.get_constant_pool_item(descriptor_index).bytes.data()
                                );

                        auto descriptor = std::string(descriptor_ci->bytes, &descriptor_ci->bytes[descriptor_ci->length]);

                        std::string clazz_desc = std::string("L") + clazz_name + ";";
                        std::string clazz_copy_desc = std::string("L") + class_copy_name + ";";

                        for (size_t index; (index = descriptor.find(clazz_desc)) != descriptor.npos;)
                        {
                                CONSTANT_Utf8_info ci;
                                cp_info cpi;
                                std::string new_descriptor = descriptor.replace(index, clazz_desc.size(), clazz_copy_desc);

                                ci.tag = CONSTANT_Utf8;
                                ci.length = static_cast<u2>(new_descriptor.size());

                                cpi.bytes = std::vector<uint8_t>(sizeof(ci) + ci.length);
                                memcpy(cpi.bytes.data(), &ci, sizeof(ci));
                                memcpy(&cpi.bytes.data()[sizeof(ci)], new_descriptor.c_str(), ci.length);

                                cf.set_constant_pool_item(descriptor_index, cpi);
                        }
                }

                if (g_jnihook->jvmti->GetClassLoader(clazz, &class_loader) != JVMTI_ERROR_NONE)
                        return JNIHOOK_ERR_JVMTI_OPERATION;

                // Renaming the class breaks any method that hands `this` to code
                // expecting the real type -- a constructor calling a helper, a Mixin
                // handler calling a Fabric callback -- and one such method sinks the
                // whole copy even though nothing here will ever call it.
                //
                // The verifier names its own casualties, so each one is remembered,
                // stubbed out, and the copy rebuilt. Rebuilt rather than patched:
                // verification is lazy, so a bad method can pass DefineClass and only
                // surface when the class is linked, and a class that failed to link
                // cannot be defined again under the same name.
                //
                // Stubbing everything up front would be simpler and wrong: a hooked
                // method that calls a private sibling needs that sibling's body.
                auto retry_without = [&](const std::string &what) -> jnihook_result_t {
                        record_exception(env, what.c_str());

                        std::string bad_name, bad_descriptor;
                        if (!parse_verify_error_method(g_last_error_detail, class_copy_name,
                                                       bad_name, bad_descriptor)) {
                                return JNIHOOK_ERR_JNI_OPERATION;
                        }

                        if (bad_name == method_info->name &&
                            bad_descriptor == method_info->signature) {
                                g_last_error_detail = "the hooked method itself does not verify in a "
                                                      "renamed copy: " + g_last_error_detail;
                                return JNIHOOK_ERR_JNI_OPERATION;
                        }

                        auto &stubs = g_forced_stubs[clazz_name];
                        if (stubs.size() > 64 ||
                            !stubs.insert({ bad_name, bad_descriptor }).second) {
                                // Already stubbed, so this attempt made no progress.
                                return JNIHOOK_ERR_JNI_OPERATION;
                        }

                        g_original_classes.erase(clazz_name);
                        return JNIHook_Attach(method, native_hook_method, original_method);
                };

                auto class_data = cf.bytes();
                class_copy = env->DefineClass(NULL, class_loader,
                                              reinterpret_cast<const jbyte *>(class_data.data()),
                                              class_data.size());

                if (!class_copy) {
                        return retry_without("DefineClass of the renamed copy of " + clazz_name +
                                             " failed");
                }

                // Force linking now, while the copy can still be rebuilt. Left to
                // happen on its own it would surface at the GetMethodID below, by
                // which point this class is cached and every later attach on it
                // fails the same way.
                const bool is_static = (method_info->access_flags & ACC_STATIC) == ACC_STATIC;
                jmethodID probe = is_static
                        ? env->GetStaticMethodID(class_copy, method_info->name.c_str(),
                                                 method_info->signature.c_str())
                        : env->GetMethodID(class_copy, method_info->name.c_str(),
                                           method_info->signature.c_str());
                if (!probe || env->ExceptionCheck()) {
                        return retry_without("linking the renamed copy of " + clazz_name + " failed");
                }

                g_original_classes[clazz_name] = class_copy;
        }

        // Verify that everything was cached correctly
        if (g_original_classes.find(clazz_name) == g_original_classes.end()) {
                return JNIHOOK_ERR_CLASS_FILE_CACHE;
        }

        // Get original method before applying hooks, because this is fallible
        if (original_method) {
                jclass orig_class = g_original_classes[clazz_name];
                jmethodID orig;

                if ((method_info->access_flags & ACC_STATIC) == ACC_STATIC) {
                        orig = env->GetStaticMethodID(orig_class, method_info->name.c_str(),
                                                      method_info->signature.c_str());
                } else {
                        orig = env->GetMethodID(orig_class, method_info->name.c_str(),
                                                method_info->signature.c_str());
                }

                if (!orig || env->ExceptionOccurred()) {
                        record_exception(env, ("the copy of " + clazz_name +
                                               " has no " + method_info->name + method_info->signature).c_str());
                        return JNIHOOK_ERR_JAVA_EXCEPTION;
                }

                *original_method = orig;
        }

        // Suspend other threads while the hook is being set up
        jthread curthread;
        jthread *threads;
        jint thread_count;

        env->PushLocalFrame(16);
        
        if (g_jnihook->jvmti->GetCurrentThread(&curthread) != JVMTI_ERROR_NONE)
                return JNIHOOK_ERR_JVMTI_OPERATION;

        if (g_jnihook->jvmti->GetAllThreads(&thread_count, &threads) != JVMTI_ERROR_NONE)
                return JNIHOOK_ERR_JVMTI_OPERATION;

        // TODO: Only suspend/resume threads that are actually active
        // Skipped entirely when can_suspend was not granted -- see the capability
        // fallback in JNIHook_Init. RedefineClasses takes its own safepoint, so
        // this is a narrowing of the window, not a correctness requirement.
        if (g_can_suspend) {
                for (jint i = 0; i < thread_count; ++i) {
                        if (env->IsSameObject(threads[i], curthread))
                                continue;

                        g_jnihook->jvmti->SuspendThread(threads[i]);
                }
        }

        // Apply current hooks
        jnihook_result_t ret;
        g_hooks[clazz_name].push_back(hook_info);
        if (ret = ReapplyClass(clazz, clazz_name); ret != JNIHOOK_OK) {
                g_hooks[clazz_name].pop_back();
                goto RESUME_THREADS;
        }

        // Bind ALL of this class's hooks, not just the one being added: the
        // redefinition above dropped the bindings of any that were already
        // here.
        if (!RegisterAllNatives(env, clazz, clazz_name)) {
                g_hooks[clazz_name].pop_back();
                ReapplyClass(clazz, clazz_name); // Attempt to restore class to previous state
                RegisterAllNatives(env, clazz, clazz_name);
                ret = JNIHOOK_ERR_JNI_OPERATION;
                goto RESUME_THREADS;
        }

        ret = JNIHOOK_OK;

RESUME_THREADS:
        // Resume other threads, hook already placed succesfully.
        // Must mirror the suspend above exactly: resuming a thread that was
        // never suspended is an error, and resuming one we did suspend is not
        // optional.
        if (g_can_suspend) {
                for (jint i = 0; i < thread_count; ++i) {
                        if (env->IsSameObject(threads[i], curthread))
                                continue;

                        g_jnihook->jvmti->ResumeThread(threads[i]);
                }
        }

        g_jnihook->jvmti->Deallocate(reinterpret_cast<unsigned char *>(threads));
        env->PopLocalFrame(NULL);

        return ret;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Detach(jmethodID method)
{
        // Callers can reach here after teardown; g_jnihook is a unique_ptr and
        // dereferencing it null is an immediate fastfail rather than an error.
        if (!g_jnihook) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        JNIEnv *env;
        jclass clazz;
        std::string clazz_name;
        hook_info_t hook_info;
        jvmtiClassDefinition class_definition;

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        if (g_jnihook->jvmti->GetMethodDeclaringClass(method, &clazz) != JVMTI_ERROR_NONE) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        clazz_name = get_class_name(env, clazz);
        if (clazz_name.length() == 0) {
                return JNIHOOK_ERR_JNI_OPERATION;
        }

        if (g_hooks.find(clazz_name) == g_hooks.end() || g_hooks[clazz_name].size() == 0) {
                return JNIHOOK_OK;
        }

        auto method_info = get_method_info(g_jnihook->jvmti, method);
        if (!method_info) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        // Keep a copy: if the restore turns out not to have worked we have to
        // put the hook back, and the entry is about to be erased.
        hook_info_t removed{};
        bool had_entry = false;

        for (size_t i = 0; i < g_hooks[clazz_name].size(); ++i) {
                auto &hi = g_hooks[clazz_name][i];
                if (hi.method_info.name != method_info->name ||
                    hi.method_info.signature != method_info->signature)
                        continue;

                removed = hi;
                had_entry = true;
                g_hooks[clazz_name].erase(g_hooks[clazz_name].begin() + i);
                break;
        }

        jnihook_result_t reapply = ReapplyClass(clazz, clazz_name);

        // RedefineClasses reporting success is NOT proof the method came back.
        // Ask the JVM directly whether ACC_NATIVE is gone. Getting this wrong is
        // not a cosmetic leak: the method stays native with no implementation,
        // and the first caller gets UnsatisfiedLinkError. For a method Minecraft
        // calls every frame that kills the game instantly.
        bool restored = false;
        if (reapply == JNIHOOK_OK) {
                jint mods = 0;
                if (g_jnihook->jvmti->GetMethodModifiers(method, &mods) == JVMTI_ERROR_NONE)
                        restored = (mods & ACC_NATIVE) == 0;
        }

        if (!restored) {
                // Put the hook back so the method keeps a working implementation.
                // A hooked-but-functional game beats a broken one, and the caller
                // is told so it can refuse to unload the module.
                if (had_entry) {
                        g_hooks[clazz_name].push_back(removed);
                        ReapplyClass(clazz, clazz_name);
                        RegisterAllNatives(env, clazz, clazz_name);
                }
                if (env->ExceptionCheck())
                        env->ExceptionClear();
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        // Only now, with the method proven to be plain Java again, drop the
        // native binding that Attach installed with RegisterNatives. Doing it
        // BEFORE the redefinition opens a window in which the method is still
        // native but has no implementation — and Minecraft calls this one every
        // frame, so that window is reliably hit.
        //
        // UnregisterNatives is per-CLASS, not per-method: it also unbinds any
        // sibling hook still living on this class. Re-bind the survivors
        // immediately, or detaching one hook silently breaks the other.
        env->UnregisterNatives(clazz);
        if (env->ExceptionCheck())
                env->ExceptionClear();

        RegisterAllNatives(env, clazz, clazz_name);

        return JNIHOOK_OK;
}


// Releases the JVMTI environment WITHOUT the class-restore loop.
//
// JNIHook_Shutdown does two unrelated jobs: it restores hooked classes by name
// through env->FindClass, and it clears the JVMTI event callbacks. The first
// cannot work on a native thread under Fabric (FindClass resolves against the
// AppClassLoader and cannot see net/minecraft/*), and it leaves a pending Java
// exception behind. The second is mandatory: the JVM holds ClassFileLoadHook as
// a raw pointer into the agent module, and calls it on every class load, so
// unmapping without clearing it crashes the process.
//
// Callers restore their classes with per-method JNIHook_Detach — which uses
// GetMethodDeclaringClass and needs no name lookup — and then call this.
JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_ReleaseEnvironment()
{
        if (!g_jnihook) {
                return JNIHOOK_OK;
        }

        JNIEnv *env = nullptr;
        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8) == JNI_OK && env) {
                if (env->ExceptionCheck())
                        env->ExceptionClear();
        }

        jvmtiEventCallbacks empty = {};
        g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
        g_jnihook->jvmti->SetEventCallbacks(&empty, sizeof(empty));
        g_jnihook->jvmti->DisposeEnvironment();

        g_class_file_cache.clear();
        g_hooks.clear();
        g_original_classes.clear();

        // The next environment has to re-acquire these; leaving them set would
        // let a later Attach believe it may suspend when it may not.
        g_can_suspend = false;
        g_caps_acquired = "none";

        g_jnihook = nullptr;

        return JNIHOOK_OK;
}

JNIHOOK_API jnihook_result_t JNIHOOK_CALL
JNIHook_Shutdown()
{
        // Callers can reach here after teardown; g_jnihook is a unique_ptr and
        // dereferencing it null is an immediate fastfail rather than an error.
        if (!g_jnihook) {
                return JNIHOOK_ERR_JVMTI_OPERATION;
        }

        JNIEnv *env;
        jvmtiEventCallbacks callbacks = {};

        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8)) {
                return JNIHOOK_ERR_GET_JNI;
        }

        for (auto &[key, _value] : g_class_file_cache) {
                jclass clazz = env->FindClass(key.c_str());

                g_hooks[key].clear();

                if (!clazz)
                        continue;

                // Reapplying the class with empty hooks will just restore the original one.
                ReapplyClass(clazz, key);
        }

        g_class_file_cache.clear();

        // TODO: Fully cleanup defined classes in `g_original_classes` by deleting them from the JVM memory
        //       (if possible without doing crazy hacks)
        g_original_classes.clear();

        g_jnihook->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
        g_jnihook->jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

        g_jnihook = nullptr;

        return JNIHOOK_OK;
}

JNIHOOK_API int JNIHOOK_CALL
JNIHook_LastJvmtiError(void)
{
        return static_cast<int>(g_last_jvmti_error);
}

JNIHOOK_API const char * JNIHOOK_CALL
JNIHook_AcquiredCapabilities(void)
{
        return g_caps_acquired;
}

JNIHOOK_API const char * JNIHOOK_CALL
JNIHook_LastErrorDetail(void)
{
        return g_last_error_detail.c_str();
}

JNIHOOK_API void JNIHOOK_CALL
JNIHook_MarkClientThread(void)
{
        t_client_thread = true;
}

JNIHOOK_API void JNIHOOK_CALL
JNIHook_AllowUnsafeAttach(int allow)
{
        g_allow_unsafe_attach = allow != 0;
}

JNIHOOK_API int JNIHOOK_CALL
JNIHook_ProbeClassMixins(jclass clazz, char *out_sample, int out_len, int *out_size)
{
        if (!g_jnihook || !clazz)
                return -1;

        JNIEnv *env = nullptr;
        if (g_jnihook->jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8) != JNI_OK || !env)
                return -1;

        g_probe_class  = get_class_name(env, clazz);
        if (g_probe_class.empty())
                return -1;

        g_probe_seen   = false;
        g_probe_mixins = false;
        g_probe_size   = 0;
        g_probe_sample.clear();
        g_probe_active = true;

        const jvmtiError en = g_jnihook->jvmti->SetEventNotificationMode(
                JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);

        jvmtiError rt = JVMTI_ERROR_NONE;
        if (en == JVMTI_ERROR_NONE)
                rt = g_jnihook->jvmti->RetransformClasses(1, &clazz);

        g_jnihook->jvmti->SetEventNotificationMode(
                JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);

        g_probe_active = false;

        if (en != JVMTI_ERROR_NONE || rt != JVMTI_ERROR_NONE) {
                g_last_jvmti_error = (en != JVMTI_ERROR_NONE) ? en : rt;
                return -1;
        }

        if (!g_probe_seen)
                return -2;   // retransform produced no bytes for this class

        if (out_sample && out_len > 0) {
                const int n = (int)g_probe_sample.size() < out_len - 1
                                    ? (int)g_probe_sample.size() : out_len - 1;
                memcpy(out_sample, g_probe_sample.c_str(), n);
                out_sample[n] = 0;
        }
        if (out_size)
                *out_size = g_probe_size;

        return g_probe_mixins ? 1 : 0;
}
