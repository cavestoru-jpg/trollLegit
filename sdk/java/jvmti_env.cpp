#include "jvmti_env.h"

#include <enhance/enhance.h>

namespace
{
	jvmtiEnv* g_jvmti = nullptr;
	bool g_tried = false;
}

jvmtiEnv* sdk::java::jvmti()
{
	if (g_jvmti)
		return g_jvmti;

	if (g_tried)
		return nullptr;

	if (!enhance::instance)
		return nullptr;

	JavaVM* vm = enhance::instance->get_java_vm();
	if (!vm)
		return nullptr;

	// Only mark as tried once there was a VM to ask — otherwise an early call
	// during startup would poison every later attempt.
	g_tried = true;

	jvmtiEnv* env = nullptr;
	if (vm->GetEnv(reinterpret_cast<void**>(&env), JVMTI_VERSION_1_2) != JNI_OK || !env)
		return nullptr;

	g_jvmti = env;
	return g_jvmti;
}

void sdk::java::dispose_jvmti()
{
	if (!g_jvmti)
		return;

	g_jvmti->DisposeEnvironment();
	g_jvmti = nullptr;
	// Leave g_tried set: re-acquiring after teardown would only happen on the
	// way out, and would put a fresh registration back into the JVM.
}
