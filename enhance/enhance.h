#include <sdk/includes.h>
#include "globals/globals.h"

namespace enhance
{
	class enhance_client
	{
	private:
		JNIEnv* env;
		JavaVM* vm;

	public:
		bool attach();
		void run();
		void unload();

		// False when a JNIHook could not be detached. The redefined Java class
		// still routes through native code that is about to be unmapped, so the
		// DLL must stay loaded — the alternative is a guaranteed access
		// violation the next time Minecraft calls that method.
		bool safe_to_free() const { return m_safe_to_free; }

	private:
		bool m_safe_to_free = true;

	public:

		const auto get_env() { return env; }
		const auto get_java_vm() { return vm; }
	};

	extern std::unique_ptr<enhance_client> instance;
}

