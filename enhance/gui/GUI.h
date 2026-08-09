#ifndef GUI_H_
#define GUI_H_

#include <Windows.h>
#include <utils/imgui/imgui.h>
#include <utils/imgui/imgui_impl_win32.h>
#include <utils/imgui/imgui_impl_opengl3.h>

namespace GUI
{
	bool init(HWND wnd_handle);
	void shutdown();

	void draw();

	bool get_is_init();
	bool get_do_draw();

	void set_do_draw(bool new_value);
}

// Centralized per-tick bind processor. For each module registered in the GUI,
// translates the bound key + mode (Hold / Toggle) into the module's *_enabled
// global, so a bind activates the module directly without needing the row's
// enable toggle to already be on. Safe to call every game tick; no-ops while
// the GUI is open or MC isn't the foreground window.
void tick_module_binds();

#endif

