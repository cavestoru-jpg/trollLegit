#include <sdk/includes.h>

#include "Hook.h"
#include "../utils/client_thread.h"
#include <jnihook.h>
#include "../gui/GUI.h"
#include "../globals/globals.h"
#include "../utils/logger.h"

#include <atomic>
#include <vector>

typedef BOOL(__stdcall* TWglSwapBuffers) (HDC hDc);

static bool is_init{};
static HWND wnd_handle{};
static WNDPROC origin_wndproc{};
void* p_swap_buffers{};
TWglSwapBuffers origin_wglSwapBuffers{};

// Static variables for GUI context - reset on each injection
static HGLRC new_context{};
static bool gui_was_init{};

// Unload coordination. See Hook.h for why this is split across two threads.
static std::atomic<bool> g_unload_requested{ false };
static std::atomic<bool> g_render_teardown_done{ false };
static std::atomic<int>  g_in_hook{ 0 };
// False until the original window procedure is provably back in place. If it
// is not, this module must never be unmapped — the next window message would
// jump into freed memory.
static std::atomic<bool> g_wndproc_restored{ false };

// RAII guard so every return path out of the hook decrements the counter.
struct hook_guard
{
	hook_guard() { g_in_hook.fetch_add(1, std::memory_order_acquire); }
	~hook_guard() { g_in_hook.fetch_sub(1, std::memory_order_release); }
};

static LRESULT __stdcall WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static bool __stdcall wglSwapBuffers(HDC hDc);

// The game's own top-level window, found by process rather than by class name.
//
// FindWindowA("LWJGL") searched EVERY process, which was wrong twice over. With
// two Minecraft versions open it subclassed whichever window Windows listed
// first -- possibly the other game's -- and on 26.3 it found nothing at all,
// because that version dropped GLFW for SDL and its window class is "SDL_app".
// The class name is not ours to predict; the process id is.
//
// Skips the invisible helpers every backend leaves lying around (the RenderPearl
// utility window, the wgl dummy, IME windows) by requiring a visible window with
// a title and a real size.
// Windows this process owns besides the main one. SDL keeps hidden helpers
// that receive input messages, so the menu can only swallow input if they are
// subclassed too. Each original is kept so shutdown can put it back.
struct sibling_window
{
	HWND    hwnd;
	WNDPROC original;
};
static std::vector<sibling_window> g_siblings;

static void subclass_sibling_windows();
static void restore_sibling_windows();

static HWND find_own_window()
{
	struct search
	{
		DWORD pid;
		HWND  best;
	} state{ GetCurrentProcessId(), nullptr };

	EnumWindows([](HWND hwnd, LPARAM param) -> BOOL
	{
		auto* st = reinterpret_cast<search*>(param);

		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid != st->pid || !IsWindowVisible(hwnd))
			return TRUE;

		if (GetWindow(hwnd, GW_OWNER) != nullptr)
			return TRUE;                      // a dialog or tool window

		if (GetWindowTextLengthA(hwnd) == 0)
			return TRUE;

		RECT rc{};
		if (!GetWindowRect(hwnd, &rc) || (rc.right - rc.left) < 200 || (rc.bottom - rc.top) < 200)
			return TRUE;

		st->best = hwnd;
		return FALSE;
	}, reinterpret_cast<LPARAM>(&state));

	return state.best;
}

static void subclass_sibling_windows()
{
	struct collect
	{
		DWORD pid;
		HWND  main_window;
	} state{ GetCurrentProcessId(), wnd_handle };

	EnumWindows([](HWND hwnd, LPARAM param) -> BOOL
	{
		auto* st = reinterpret_cast<collect*>(param);

		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid != st->pid || hwnd == st->main_window)
			return TRUE;

		WNDPROC current = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
		if (!current || current == WndProc)
			return TRUE;

		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
		g_siblings.push_back({ hwnd, current });
		return TRUE;
	}, reinterpret_cast<LPARAM>(&state));
}

static void restore_sibling_windows()
{
	for (const auto& sw : g_siblings)
	{
		if (IsWindow(sw.hwnd) &&
		    (WNDPROC)GetWindowLongPtrW(sw.hwnd, GWLP_WNDPROC) == WndProc)
		{
			SetWindowLongPtrW(sw.hwnd, GWLP_WNDPROC, (LONG_PTR)sw.original);
		}
	}
	g_siblings.clear();
}

bool Hook::init()
{
	if (is_init)
	{
		return false;
	}

	gui_was_init = false;
	new_context = nullptr;

	MH_STATUS mh_status = MH_Initialize();
	if (mh_status == MH_ERROR_ALREADY_INITIALIZED)
	{
		MH_Uninitialize();
		Sleep(50);
		mh_status = MH_Initialize();
	}
	
	if (mh_status != MH_OK)
	{
		return true;
	}

	{
		wnd_handle = find_own_window();

		// Falls back to the old class-name search only when the process has no
		// window yet -- injecting during startup, before the game creates one.
		if (!wnd_handle)
		{
			wnd_handle = FindWindowA("LWJGL", nullptr);
			if (!wnd_handle)
				wnd_handle = FindWindowA("GLFW30", nullptr);
		}

		if (!wnd_handle)
		{
			logger::log_error("[hook] no window for this process yet -- inject once the game is drawing");
			return true;
		}

		WNDPROC current_proc = (WNDPROC)GetWindowLongPtrW(wnd_handle, GWLP_WNDPROC);
		
		if (current_proc == WndProc)
		{
			origin_wndproc = nullptr;
		}
		else
		{
			origin_wndproc = current_proc;
		}

		SetWindowLongPtrW(wnd_handle, GWLP_WNDPROC, (LONG_PTR)WndProc);

		// And every other window this process owns. SDL -- which 26.3 uses
		// instead of GLFW -- keeps hidden helper windows beside the visible
		// one and delivers wheel and raw input through them, so filtering
		// only the window the player sees let the scroll wheel reach the
		// hotbar while the menu was open.
		subclass_sibling_windows();
	}

	{
		p_swap_buffers = (void*)GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglSwapBuffers");

		if (p_swap_buffers == nullptr)
		{
			return true;
		}

		MH_RemoveHook(p_swap_buffers);
		
		MH_STATUS create_status = MH_CreateHook(p_swap_buffers, &wglSwapBuffers, (LPVOID*)&origin_wglSwapBuffers);
		if (create_status != MH_OK && create_status != MH_ERROR_ALREADY_CREATED)
		{
			return true;
		}
	}

	MH_EnableHook(MH_ALL_HOOKS);

	is_init = true;

	return false;
}

void Hook::shutdown()
{
	if (!is_init)
	{
		return;
	}

	is_init = false;

	if (gui_was_init)
	{
		try
		{
			GUI::shutdown();
		}
		catch (...)
		{
		}
		gui_was_init = false;
	}

	// Close both doors BEFORE waiting. Disabling the trampoline stops new swap
	// hook entries; putting the original window procedure back stops new
	// message entries. Waiting first and restoring afterwards would leave a gap
	// in which a fresh message walks straight into a module about to be freed.
	MH_DisableHook(MH_ALL_HOOKS);

	if (wnd_handle && IsWindow(wnd_handle))
	{
		WNDPROC current_proc = (WNDPROC)GetWindowLongPtrW(wnd_handle, GWLP_WNDPROC);

		if (current_proc == WndProc)
		{
			if (origin_wndproc && origin_wndproc != WndProc)
			{
				SetWindowLongPtrW(wnd_handle, GWLP_WNDPROC, (LONG_PTR)origin_wndproc);
		restore_sibling_windows();
				g_wndproc_restored = true;
			}
			// else: our procedure is installed but the original was never
			// captured, so there is nothing to put back. g_wndproc_restored
			// stays false and unloading is refused.
		}
		else
		{
			// Someone else chained on top of us. Unhooking now would cut them
			// out; leaving ours in place keeps the chain intact.
			g_wndproc_restored = (current_proc != nullptr) ? false : false;
		}
	}
	else
	{
		// The window is gone, so nothing can call into our procedure any more.
		g_wndproc_restored = true;
	}

	// Only now is it safe to wait: no new entries can start through either door.
	if (!wait_until_hook_idle(2000))
	{
		// Something is wedged inside our code. Leaving everything installed is
		// far better than unloading out from under it.
		return;
	}

	MH_RemoveHook(MH_ALL_HOOKS);

	new_context = nullptr;
	
	MH_Uninitialize();

	wnd_handle = nullptr;
	origin_wndproc = nullptr;
	p_swap_buffers = nullptr;
	origin_wglSwapBuffers = nullptr;
}

void Hook::request_unload()
{
	g_unload_requested.store(true, std::memory_order_release);
}

bool Hook::unload_requested()
{
	return g_unload_requested.load(std::memory_order_acquire);
}

bool Hook::render_teardown_done()
{
	return g_render_teardown_done.load(std::memory_order_acquire);
}

bool Hook::wait_until_hook_idle(unsigned timeout_ms)
{
	for (unsigned waited = 0; waited < timeout_ms; waited += 5)
	{
		if (g_in_hook.load(std::memory_order_acquire) == 0)
			return true;
		Sleep(5);
	}
	return g_in_hook.load(std::memory_order_acquire) == 0;
}

bool Hook::safe_to_unmap()
{
	return g_wndproc_restored.load(std::memory_order_acquire) &&
	       g_in_hook.load(std::memory_order_acquire) == 0;
}

bool Hook::get_is_init()
{
	return is_init;
}

HWND Hook::get_window()
{
	return wnd_handle;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// When our menu opens, MC stops receiving any input we swallow — including
// the WM_KEYUP for any keys the user happened to be holding at that moment.
// Without intervention MC keeps moving forever ("стрел[ка] зажата"). We send
// a synthetic WM_KEYUP for every key the OS reports as currently down so MC
// releases them cleanly. The OS's own key-repeat will re-trigger WM_KEYDOWN
// for keys actually still held the moment the menu closes.
static void release_held_keys_to_mc(HWND mc_window)
{
	if (!mc_window || !origin_wndproc) return;
	for (int vk = 0x08; vk < 0xFF; ++vk)
	{
		// Skip mouse buttons — MC tracks those via WM_LBUTTONUP/etc., and
		// blasting WM_KEYUP for VK_LBUTTON would also confuse it.
		if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON
		 || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;
		if ((GetAsyncKeyState(vk) & 0x8000) != 0)
		{
			CallWindowProcA(origin_wndproc, mc_window, WM_KEYUP,  (WPARAM)vk, 0);
			CallWindowProcA(origin_wndproc, mc_window, WM_SYSKEYUP,(WPARAM)vk, 0);
		}
	}
}

// Closing the menu: ImGui hid the OS cursor while it was rendering its own.
// GLFW (which MC uses) caches its cursor mode independently and won't
// resync until something triggers WM_SETCURSOR. We re-issue one against
// the unfiltered WndProc so GLFW's cursor mode (DISABLED for gameplay,
// NORMAL for inventory) restores what it should be. Also crank the
// ShowCursor counter back up in case ImGui's backend left it negative.
static void restore_cursor_state(HWND mc_window)
{
	if (!mc_window || !origin_wndproc) return;
	CallWindowProcA(origin_wndproc, mc_window, WM_SETCURSOR,
	                (WPARAM)mc_window, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
	for (int i = 0; i < 8 && ShowCursor(TRUE) < 0; ++i) {}
}

// Resolve generic VK_SHIFT/VK_CONTROL/VK_MENU to their left/right variant
// using the WM_KEY* lParam scancode + extended bit. Without this, the GUI
// hotkey "Right Shift" would also fire on Left Shift.
static int resolve_lr_vk(WPARAM wParam, LPARAM lParam)
{
	int vk = (int)wParam;
	UINT sc = (lParam >> 16) & 0xFF;
	bool ext = (lParam & (1 << 24)) != 0;
	if (vk == VK_SHIFT)
		return (sc == 0x36) ? VK_RSHIFT : VK_LSHIFT;
	if (vk == VK_CONTROL)
		return ext ? VK_RCONTROL : VK_LCONTROL;
	if (vk == VK_MENU)
		return ext ? VK_RMENU : VK_LMENU;
	return vk;
}

// Returns true if `vk_pressed` should fire the user's bind `vk_bound`.
// Treats generic SHIFT/CONTROL/MENU binds as matching either side.
static bool matches_bind(int vk_pressed, int vk_bound)
{
	if (vk_pressed == vk_bound) return true;
	if (vk_bound == VK_SHIFT   && (vk_pressed == VK_LSHIFT   || vk_pressed == VK_RSHIFT))   return true;
	if (vk_bound == VK_CONTROL && (vk_pressed == VK_LCONTROL || vk_pressed == VK_RCONTROL)) return true;
	if (vk_bound == VK_MENU    && (vk_pressed == VK_LMENU    || vk_pressed == VK_RMENU))    return true;
	return false;
}

LRESULT __stdcall WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Window messages arrive asynchronously on the game's message thread, so
	// this is a second way into the DLL and needs the same occupancy tracking
	// as the swap hook — otherwise unloading can unmap the module while a
	// message is being handled.
	hook_guard guard;

	// Detect menu open/close transitions across calls so we can clean up MC's
	// stuck-key state and re-sync GLFW's cursor. set_do_draw can flip outside
	// the keybind path (close button), so we watch the flag itself.
	static bool s_prev_gui_open = false;
	if (GUI::get_is_init())
	{
		const bool now_open = GUI::get_do_draw();
		if (now_open != s_prev_gui_open)
		{
			if (now_open) release_held_keys_to_mc(hWnd);
			else          restore_cursor_state(hWnd);
			s_prev_gui_open = now_open;
		}

		// GUI toggle hotkey: resolve L/R-distinct VK, match against globals::gui_keybind.
		if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lParam & (1 << 30)))
		{
			int vk = resolve_lr_vk(wParam, lParam);
			if (globals::gui_keybind != 0 && matches_bind(vk, globals::gui_keybind))
			{
				const bool was_open = GUI::get_do_draw();
				GUI::set_do_draw(!was_open);
				const bool now_open = GUI::get_do_draw();
				if (!was_open && now_open) release_held_keys_to_mc(hWnd);
				else if (was_open && !now_open) restore_cursor_state(hWnd);
				s_prev_gui_open = now_open;
				return 0;
			}
		}

		const bool gui_open = GUI::get_do_draw();

		if (gui_open)
		{
			// Always let ImGui see input first.
			ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

			// Swallow input for MC so the player doesn't move/look while the menu is open.
			switch (msg)
			{
			case WM_KEYDOWN: case WM_KEYUP:
			case WM_SYSKEYDOWN: case WM_SYSKEYUP:
			case WM_CHAR: case WM_DEADCHAR:
			case WM_SYSCHAR: case WM_SYSDEADCHAR:
			case WM_MOUSEMOVE:
			case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
			case WM_INPUT:
			case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
			case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
				return 0;
			case WM_SETCURSOR:
				SetCursor(LoadCursor(NULL, IDC_ARROW));
				return TRUE;
			}
		}
	}

	// Each window has its own original: handing a sibling's message to the
	// main window's procedure would deliver it to the wrong place.
	if (hWnd != wnd_handle)
	{
		for (const auto& sw : g_siblings)
		{
			if (sw.hwnd == hWnd)
				return CallWindowProcW(sw.original, hWnd, msg, wParam, lParam);
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	return CallWindowProcA(origin_wndproc, hWnd, msg, wParam, lParam);
}

bool __stdcall wglSwapBuffers(HDC hDc)
{
	// The guard must come FIRST, before the early-out below. That branch also
	// calls origin_wglSwapBuffers, which is MinHook's trampoline — memory that
	// MH_Uninitialize frees. Hook::shutdown clears is_init before anything
	// else, so the early-out is exactly the path the render thread takes for
	// the whole teardown window; leaving it unguarded made it invisible to
	// wait_until_hook_idle and let the trampoline be freed underneath it.
	hook_guard guard;

	if (!is_init || !origin_wglSwapBuffers)
	{
		if (!origin_wglSwapBuffers)
		{
			return TRUE;
		}
		return origin_wglSwapBuffers(hDc);
	}

	// Minecraft runs its ticks on this same thread (MinecraftClient.render
	// calls tick), so this is the client thread as far as JNIHook is
	// concerned. Marking it lets hooks attach here without can_suspend --
	// which after a re-injection is often unavailable, because it is solo in
	// HotSpot and stays held by environments that leaked when a previous
	// session crashed.
	{
		static bool s_marked = false;
		if (!s_marked)
		{
			s_marked = true;
			JNIHook_MarkClientThread();
		}
	}

	// NOT drained here any more.
	//
	// This is inside wglSwapBuffers, i.e. inside a native downcall made by
	// LWJGL from Java. Ordinary JNI from that position is fine -- the ESP does
	// it every frame -- but JVMTI RedefineClasses/DefineClass are not: they
	// need a safepoint, and driving one from inside a native frame the VM is
	// still holding crashed the render thread with an access violation inside
	// jvm.dll, several minutes into a session. Marking the thread above is
	// safe and still worth doing; running the attaches here is not.

	HGLRC origin_context{ wglGetCurrentContext() };

	// Render half of the unload. Everything ImGui and GL owns lives in the
	// context created below, so this is the only thread that may destroy it.
	// Afterwards the hook becomes a pass-through and the worker is free to pull
	// the trampoline out.
	if (g_unload_requested.load(std::memory_order_acquire))
	{
		if (!g_render_teardown_done.load(std::memory_order_acquire))
		{
			if (gui_was_init && new_context)
			{
				wglMakeCurrent(hDc, new_context);
				try { GUI::shutdown(); } catch (...) {}
				wglMakeCurrent(hDc, origin_context);

				wglDeleteContext(new_context);
				new_context = nullptr;
			}
			gui_was_init = false;
			g_render_teardown_done.store(true, std::memory_order_release);
		}

		return origin_wglSwapBuffers(hDc);
	}

	if (!gui_was_init)
	{
		new_context = wglCreateContext(hDc);
		if (!new_context)
		{
			return origin_wglSwapBuffers(hDc);
		}

		wglMakeCurrent(hDc, new_context);

		typedef BOOL(__stdcall* wglSwapIntervalEXT)(int);
		wglSwapIntervalEXT wglSwapInterval = (wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
		if (wglSwapInterval)
		{
			wglSwapInterval(0);
		}

		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		glViewport(0, 0, viewport[2], viewport[3]);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, viewport[2], viewport[3], 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);

		GUI::init(wnd_handle);

		gui_was_init = true;
	}
	else if (new_context)
	{
		wglMakeCurrent(hDc, new_context);
		GUI::draw();
	}

	wglMakeCurrent(hDc, origin_context);

	return origin_wglSwapBuffers(hDc);
}
