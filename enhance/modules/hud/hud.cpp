#define NOMINMAX
#include "hud.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include <sdk/includes.h>
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/player/player.h>
#include <vector>
#include <string>
#include <algorithm>

// Helper to draw text with shadow
static void DrawTextWithShadow(ImDrawList* draw_list, ImFont* font, float size, const ImVec2& pos, ImU32 color, const char* text)
{
	draw_list->AddText(font, size, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 180), text);
	draw_list->AddText(font, size, pos, color, text);
}

static ImFont* GetFont(float size)
{
	(void)size;
	return ImGui::GetFont();
}

// ===================================================================
//  Drag-and-drop layout system for HUD elements
// ===================================================================
//
// When the menu is open we treat each HUD element as a draggable widget:
// the user clicks anywhere on its bounding rect and drags it to wherever
// they want. The position is stored in globals so it survives menu open/
// close and gets saved naturally with the rest of the settings.
//
// Only one element can be dragged at a time. We use a small id-based
// state machine: s_drag_id is -1 normally, becomes the element id while
// that element is being dragged. The element renders itself, calls
// handle_drag() with its current rect — if drag is active we override
// the position before drawing.

namespace enhance::modules::hud
{

enum HudElement
{
	HUD_WATERMARK = 1,
	HUD_POTION_LIST,
	HUD_KEYBINDS,
	HUD_TARGET_HUD,
};

static int    s_drag_id = -1;
static ImVec2 s_drag_offset(0, 0);

// True when the HUD is in interactive edit mode (i.e. the menu is open
// and the mouse isn't currently inside an ImGui widget).
static bool edit_mode_active()
{
	if (!GUI::get_do_draw()) return false;
	return !ImGui::GetIO().WantCaptureMouse;
}

// Process drag for the element identified by `id` whose current
// bounding rect is (pos, pos+size). Returns the (possibly updated)
// position and writes it back to *xref / *yref. Also paints the edit-
// mode outline when the menu is open.
static ImVec2 handle_drag(int id, float* xref, float* yref, ImVec2 size,
                          ImDrawList* dl)
{
	ImVec2 pos(*xref, *yref);

	// Edit-mode outline so the user sees where each element is.
	if (GUI::get_do_draw() && dl)
	{
		const ImU32 col = (s_drag_id == id)
		                  ? IM_COL32(255, 200,  80, 230)
		                  : IM_COL32(120, 220, 255, 160);
		dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, 4.0f, 0, 1.5f);
	}

	if (!edit_mode_active()) return pos;

	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 mouse = io.MousePos;
	const bool over = mouse.x >= pos.x && mouse.x <= pos.x + size.x
	               && mouse.y >= pos.y && mouse.y <= pos.y + size.y;

	if (s_drag_id == id)
	{
		if (ImGui::IsMouseDown(0))
		{
			pos.x = mouse.x - s_drag_offset.x;
			pos.y = mouse.y - s_drag_offset.y;
			// Clamp to screen so a misclick can't drag an element off
			// the visible area and lose it.
			pos.x = std::clamp(pos.x, 0.0f, io.DisplaySize.x - size.x);
			pos.y = std::clamp(pos.y, 0.0f, io.DisplaySize.y - size.y);
			*xref = pos.x;
			*yref = pos.y;
		}
		else
		{
			s_drag_id = -1;
		}
	}
	else if (s_drag_id == -1 && over && ImGui::IsMouseClicked(0))
	{
		s_drag_id = id;
		s_drag_offset = ImVec2(mouse.x - pos.x, mouse.y - pos.y);
	}

	return pos;
}

// ==================== WATERMARK ====================
void render_watermark()
{
	if (!globals::hud_watermark_enabled) return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list) return;

	const char* text = "enhance v1.21.11";
	ImFont* font = GetFont(14.0f);
	ImVec2 text_size = ImGui::CalcTextSize(text);
	const ImVec2 size(text_size.x + 16, text_size.y + 8);

	ImVec2 pos = handle_drag(HUD_WATERMARK,
		&globals::hud_watermark_x, &globals::hud_watermark_y, size, draw_list);

	ImVec2 bg_min = pos;
	ImVec2 bg_max = ImVec2(pos.x + size.x, pos.y + size.y);
	draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(0, 0, 0, 180), 4.0f);

	ImU32 text_color = IM_COL32(122, 184, 255, 255);
	DrawTextWithShadow(draw_list, font, 14.0f,
		ImVec2(pos.x + 8, pos.y + 4), text_color, text);
}

// ==================== POTION LIST ====================
void render_potion_list()
{
	if (!globals::hud_potion_list_enabled) return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list) return;

	// Reading active effects needs a JNI snapshot from the worker thread
	// (calling JNI on the render thread crashes the JVM — see target_esp).
	// For now this is a stub. When the worker-side fetcher is added,
	// publish results to a mutex-guarded vector here.
	std::vector<std::string> effects;

	// In edit mode we still want a visible handle so the user can drag
	// it to where they'll want the eventual list to appear. Show a
	// placeholder strip when the list is empty.
	if (effects.empty())
	{
		if (!GUI::get_do_draw()) return;

		const char* placeholder = "Potion list (drag me)";
		ImVec2 ts = ImGui::CalcTextSize(placeholder);
		ImVec2 size(ts.x + 12, ts.y + 6);
		ImVec2 pos = handle_drag(HUD_POTION_LIST,
			&globals::hud_potion_list_x, &globals::hud_potion_list_y, size, draw_list);
		draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
			IM_COL32(0, 0, 0, 140), 3.0f);
		DrawTextWithShadow(draw_list, GetFont(13.0f), 13.0f,
			ImVec2(pos.x + 6, pos.y + 3),
			IM_COL32(200, 200, 200, 220), placeholder);
		return;
	}

	// (Real implementation will iterate `effects` and render rows here.)
}

// ==================== KEYBINDS / HOTKEYS ====================
// Convert a VK code to a printable label using the OS keyboard layout,
// falling back to common-key names for things GetKeyNameTextA doesn't
// handle well (mouse buttons, modifier-only keys).
static std::string vk_to_label(int vk)
{
	if (vk == 0) return "—";
	switch (vk)
	{
		case VK_LBUTTON: return "LMB";
		case VK_RBUTTON: return "RMB";
		case VK_MBUTTON: return "MMB";
		case VK_XBUTTON1: return "MB4";
		case VK_XBUTTON2: return "MB5";
		case VK_LSHIFT:  return "LShift";
		case VK_RSHIFT:  return "RShift";
		case VK_SHIFT:   return "Shift";
		case VK_LCONTROL: return "LCtrl";
		case VK_RCONTROL: return "RCtrl";
		case VK_CONTROL: return "Ctrl";
		case VK_LMENU:   return "LAlt";
		case VK_RMENU:   return "RAlt";
		case VK_MENU:    return "Alt";
		case VK_SPACE:   return "Space";
		case VK_TAB:     return "Tab";
		case VK_RETURN:  return "Enter";
		case VK_ESCAPE:  return "Esc";
		case VK_BACK:    return "Backspace";
		case VK_CAPITAL: return "CapsLock";
	}
	char buf[64] = {0};
	UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
	// Bit 24 of lParam = extended-key flag (numpad/arrow keys etc.). The
	// scan code-only call without the extended bit gets wrong names for
	// arrows, so set it for VK_LEFT/RIGHT/UP/DOWN/INS/DEL/HOME/END.
	long lparam = (long)(sc << 16);
	switch (vk)
	{
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
		case VK_INSERT: case VK_DELETE:
		case VK_HOME: case VK_END:
		case VK_PRIOR: case VK_NEXT:
			lparam |= (1L << 24);
			break;
		default: break;
	}
	if (GetKeyNameTextA(lparam, buf, sizeof(buf)) > 0) return std::string(buf);
	char fallback[32];
	snprintf(fallback, sizeof(fallback), "VK%02X", vk);
	return std::string(fallback);
}

void render_keybinds()
{
	if (!globals::hud_keybinds_enabled) return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list) return;

	struct ModuleInfo
	{
		const char* name;
		int keybind;
	};

	std::vector<ModuleInfo> modules;
	if (globals::killaura_enabled)      modules.push_back({"KillAura",     globals::killaura_keybind});
	if (globals::triggerbot_enabled)    modules.push_back({"TriggerBot",   globals::triggerbot_keybind});
	if (globals::reach_enabled)         modules.push_back({"Reach",        globals::reach_keybind});
	if (globals::aimassist_enabled)     modules.push_back({"AimAssist",    globals::aimassist_keybind});
	if (globals::eagle_enabled)         modules.push_back({"Eagle",        globals::eagle_keybind});
	if (globals::box_enabled)           modules.push_back({"ESP",          globals::esp_keybind});
	if (globals::hitbox_enabled)        modules.push_back({"HitBox",       globals::hitbox_keybind});
	if (globals::autototem_enabled)     modules.push_back({"AutoTotem",    globals::autototem_keybind});
	if (globals::autoclicker_enabled)   modules.push_back({"AutoClicker",  globals::autoclicker_keybind});
	if (globals::auto_anchor_enabled)   modules.push_back({"AutoAnchor",   globals::auto_anchor_keybind});
	if (globals::anchor_macro_enabled)  modules.push_back({"AnchorMacro",  globals::anchor_macro_keybind});

	// Build display strings up-front so we can compute the panel size
	// before we ask for a drag rect.
	struct Row { std::string text; ImVec2 size; };
	std::vector<Row> rows;
	rows.reserve(modules.size() ? modules.size() : 1);

	if (modules.empty())
	{
		// Empty placeholder so the panel is still draggable in edit mode.
		if (!GUI::get_do_draw()) return;
		rows.push_back({ "Keybinds (drag me)", ImGui::CalcTextSize("Keybinds (drag me)") });
	}
	else
	{
		for (const auto& m : modules)
		{
			std::string label = vk_to_label(m.keybind);
			std::string text = std::string(m.name) + "  [" + label + "]";
			ImVec2 sz = ImGui::CalcTextSize(text.c_str());
			rows.push_back({ std::move(text), sz });
		}
	}

	const float row_h = ImGui::CalcTextSize("X").y + 4.0f;
	float panel_w = 0.0f;
	for (const auto& r : rows) panel_w = std::max(panel_w, r.size.x);
	panel_w += 16.0f;
	const float panel_h = row_h * rows.size() + 4.0f;
	ImVec2 size(panel_w, panel_h);

	ImVec2 pos = handle_drag(HUD_KEYBINDS,
		&globals::hud_keybinds_x, &globals::hud_keybinds_y, size, draw_list);

	// Subtle background for the whole panel so dragging feels solid.
	draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
		IM_COL32(0, 0, 0, modules.empty() ? 140 : 100), 4.0f);

	ImFont* font = GetFont(13.0f);
	for (size_t i = 0; i < rows.size(); ++i)
	{
		ImVec2 rp(pos.x + 8, pos.y + 2 + i * row_h);
		DrawTextWithShadow(draw_list, font, 13.0f, rp,
			modules.empty() ? IM_COL32(200, 200, 200, 220)
			                : IM_COL32(122, 184, 255, 230),
			rows[i].text.c_str());
	}
}

// ==================== TARGET HUD ====================
void render_target_hud()
{
	if (!globals::hud_target_hud_enabled) return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list) return;

	struct TargetData
	{
		bool   has_target;
		std::string name;
		float  health;
		float  max_health;
		double distance;
	};
	TargetData target = {};
	target.has_target = false;
	// Real data will plug in once killaura is back online — we'd publish
	// it from the worker tick into a mutex-guarded snapshot here.

	if (!target.has_target)
	{
		// Show a draggable placeholder while the menu is open so the user
		// can position the target HUD even if there's no live target yet.
		if (!GUI::get_do_draw()) return;
		const float hud_w = 180.0f;
		const float hud_h = 60.0f;
		ImVec2 size(hud_w, hud_h);
		ImVec2 pos = handle_drag(HUD_TARGET_HUD,
			&globals::hud_target_hud_x, &globals::hud_target_hud_y, size, draw_list);
		draw_list->AddRectFilled(pos, ImVec2(pos.x + hud_w, pos.y + hud_h),
			IM_COL32(20, 25, 35, 220), 6.0f);
		draw_list->AddRect(pos, ImVec2(pos.x + hud_w, pos.y + hud_h),
			IM_COL32(122, 184, 255, 100), 6.0f, 0, 1.5f);
		const char* placeholder = "Target HUD (drag me)";
		ImVec2 ts = ImGui::CalcTextSize(placeholder);
		DrawTextWithShadow(draw_list, GetFont(13.0f), 13.0f,
			ImVec2(pos.x + (hud_w - ts.x) / 2, pos.y + (hud_h - ts.y) / 2),
			IM_COL32(200, 200, 200, 220), placeholder);
		return;
	}

	const float hud_w = 180.0f;
	const float hud_h = 60.0f;
	ImVec2 size(hud_w, hud_h);
	ImVec2 pos = handle_drag(HUD_TARGET_HUD,
		&globals::hud_target_hud_x, &globals::hud_target_hud_y, size, draw_list);

	draw_list->AddRectFilled(pos, ImVec2(pos.x + hud_w, pos.y + hud_h),
		IM_COL32(20, 25, 35, 220), 6.0f);
	draw_list->AddRect(pos, ImVec2(pos.x + hud_w, pos.y + hud_h),
		IM_COL32(122, 184, 255, 100), 6.0f, 0, 1.5f);

	ImFont* font = GetFont(14.0f);
	DrawTextWithShadow(draw_list, font, 14.0f,
		ImVec2(pos.x + 8, pos.y + 6),
		IM_COL32(255, 255, 255, 255), target.name.c_str());

	float bar_x = pos.x + 8;
	float bar_y = pos.y + 26;
	float bar_width = hud_w - 16;
	float bar_height = 12;
	draw_list->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_width, bar_y + bar_height),
		IM_COL32(40, 45, 55, 200), 3.0f);

	float health_pct = target.health / target.max_health;
	health_pct = std::clamp(health_pct, 0.0f, 1.0f);
	ImU32 health_color = (health_pct > 0.6f) ? IM_COL32(80, 220, 80, 255)
	                   : (health_pct > 0.3f) ? IM_COL32(240, 200, 60, 255)
	                   :                       IM_COL32(240, 60, 60, 255);
	draw_list->AddRectFilled(ImVec2(bar_x, bar_y),
		ImVec2(bar_x + bar_width * health_pct, bar_y + bar_height),
		health_color, 3.0f);

	char health_text[32];
	snprintf(health_text, sizeof(health_text), "%.1f / %.1f", target.health, target.max_health);
	ImVec2 health_text_size = ImGui::CalcTextSize(health_text);
	DrawTextWithShadow(draw_list, GetFont(12.0f), 12.0f,
		ImVec2(bar_x + (bar_width - health_text_size.x) / 2,
		       bar_y + (bar_height - health_text_size.y) / 2),
		IM_COL32(255, 255, 255, 255), health_text);

	if (target.distance > 0)
	{
		char dist_text[32];
		snprintf(dist_text, sizeof(dist_text), "%.1fm", target.distance);
		ImVec2 dist_size = ImGui::CalcTextSize(dist_text);
		DrawTextWithShadow(draw_list, GetFont(11.0f), 11.0f,
			ImVec2(pos.x + hud_w - 8 - dist_size.x, pos.y + 6),
			IM_COL32(180, 180, 180, 200), dist_text);
	}
}

// ==================== MAIN RENDER ====================
void render()
{
	if (!GUI::get_is_init()) return;
	if (!globals::hud_enabled) return;

	render_watermark();
	render_potion_list();
	render_keybinds();
	render_target_hud();

	// If the menu just closed, drop any active drag so a stale id doesn't
	// keep firing on subsequent render frames.
	if (!GUI::get_do_draw()) s_drag_id = -1;
}

} // namespace enhance::modules::hud
