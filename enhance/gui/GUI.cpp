#define IMGUI_DEFINE_MATH_OPERATORS

#include "GUI.h"
#include "../globals/globals.h"
#include "../modules/esp/esp.h"
#include "../modules/storage_esp/storage_esp.h"
#include "../modules/nametags/vanilla_hide.h"
#include <sdk/render/render_view.h>
#include "../modules/gambling/gambling.hpp"
#include "../modules/eagle/eagle.h"
#include "../modules/hud/hud.h"
// #include "../modules/backtrack/backtrack.h" // Disabled
#include "../utils/logger.h"
#include "../utils/http_client.h"
#include "../utils/config.h"
#include "ui/menu.hpp"
#include <stdio.h>

void UpdateWolfMenuDPI();

#define STB_IMAGE_IMPLEMENTATION

#define WOLF_MENU_INCLUDED_MODE
#include "wolf_menu.cpp"
#undef WOLF_MENU_INCLUDED_MODE

static bool is_init{};
static bool do_draw{false};


bool GUI::init(HWND wnd_handle)
{
	if (is_init)
		return false;

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	ImGui::StyleColorsDark();
	
	ImGui_ImplWin32_Init(wnd_handle);
	ImGui_ImplOpenGL3_Init();

	// One atlas, built once. The legacy wolf_menu font set used to be baked
	// first, but its only consumer — RenderWolfMenu — is dead since the menu
	// was replaced, and its InitializeFonts calls io.Fonts->Clear() followed by
	// Build(), so registering the ported menu's fonts afterwards left the atlas
	// unbuilt and re-uploaded. Six unused font bakes went with it.
	menu::get().init();

	http_client::Initialize();

	is_init = true;

	return false;
}

void GUI::shutdown()
{
	if (!is_init)
		return;

	try
	{
		// Put the scoreboard back before we go, or every team we forced to
		// NEVER stays that way for the rest of the session.
		enhance::modules::vanilla_nametags::restore();
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		
		http_client::Cleanup();
	}
	catch (...)
	{
	}

	is_init = false;
	do_draw = false;
}

bool IsLoggedIn();
void RenderLoginScreen();
void RenderGamblingWindow();

void GUI::draw()
{
	bool should_draw_gui = do_draw || globals::flight_enabled;
	bool should_draw_box = globals::box_enabled || globals::esp_health_bar
	                    || globals::nametags_enabled
	                    || globals::storage_esp_enabled
	                    || (globals::killaura_enabled && (globals::killaura_esp_enabled || globals::killaura_debug_overlay))
	                    || (globals::eagle_enabled && globals::eagle_debug_overlay)
	                    || globals::hud_enabled;

	if (!should_draw_gui && !should_draw_box)
		return;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	io.MouseDrawCursor = do_draw;

	if (should_draw_box)
	{
		// One camera read per frame, on this thread, feeding every in-world
		// overlay below. Sampling here (rather than on the 10 ms worker) is
		// what keeps the boxes glued to the players while the mouse moves.
		try
		{
			double cam_x = 0.0, cam_y = 0.0, cam_z = 0.0;
			float cam_yaw = 0.0f, cam_pitch = 0.0f, cam_fov = 70.0f;
			if (sdk::render::sample_camera(cam_x, cam_y, cam_z, cam_yaw, cam_pitch, cam_fov))
			{
				sdk::render::set_view(cam_x, cam_y, cam_z, cam_yaw, cam_pitch, cam_fov,
				                      (int)io.DisplaySize.x, (int)io.DisplaySize.y);
			}
			else
			{
				// Without this the previous frame's view stays marked valid and
				// every consumer keeps projecting against a stale camera — a
				// real one-frame lag on exactly the frames that fail.
				sdk::render::invalidate_view();
			}
		}
		catch (...)
		{
		}

		try
		{
			enhance::modules::esp::draw_boxes();
		}
		catch (...)
		{
		}

		try
		{
			enhance::modules::storage_esp::draw_boxes();
		}
		catch (...)
		{
		}

		// The killaura target overlay was removed with the old module. Its
		// replacement draws in the world, through the same Java renderer as the
		// ESP boxes and nametags, rather than as a 2D overlay here.

		// Eagle debug overlay — one-liner under the killaura overlay
		// showing the live edge-detector state. Lets us tell at a glance
		// whether want_sneak is firing.
		if (globals::eagle_enabled && globals::eagle_debug_overlay)
		{
			try
			{
				char buf[256];
				enhance::modules::eagle::debug_status_string(buf, sizeof(buf));
				ImDrawList* dl_e = ImGui::GetBackgroundDrawList();
				if (dl_e)
				{
					dl_e->AddRectFilled(ImVec2(8, 32), ImVec2(560, 52),
					                    IM_COL32(0, 0, 0, 160), 4.0f);
					dl_e->AddText(ImVec2(14, 34),
					              IM_COL32(120, 220, 255, 255), buf);
				}
			}
			catch (...) {}
		}

		// HUD: watermark + potion list + keybinds + target HUD (ported from
		// the new sources). Renders only when enabled and the master HUD
		// toggle is on.
		try
		{
			enhance::modules::hud::render();
		}
		catch (...) {}
	}
	
	// Backtrack visualization disabled

	if (do_draw)
	{
		menu::get().draw();
		
		if (enhance::modules::gambling::g_mines_game.isWindowOpen()) {
			RenderGamblingWindow();
		}
	}

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void RenderWolfMenu() {
    extern bool g_child_consumed_scroll;
    g_child_consumed_scroll = false;

    if (!g_clr.is_valid()) {
        g_clr.reset_to_defaults();
    }

    // Sync theme with persisted global so the gear cycles through and
    // restoring globals::gui_theme_id picks the same theme back up.
    if (g_theme != globals::gui_theme_id) {
        g_theme = globals::gui_theme_id;
    }
    ApplyTheme(g_theme);

    for (auto& popup : g_popup_storage) {
        easing(popup.alpha, popup.open ? 1.f : 0.f, 9.f, static_easing);
    }

    // Window size + position (centered on first appearance, draggable after).
    c_vec2 win_size = s_(720, 460);
    ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f - win_size * 0.5f, ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    float win_rounding = s_(8);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, c_vec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, s_(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, c_vec4(0, 0, 0, 0));

    if (ImGui::Begin(g_elem.window.name.c_str(), nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        c_vec2 wp = ImGui::GetWindowPos();
        c_vec2 ws = ImGui::GetWindowSize();

        // Background panel with light-blue glass border + soft shadow.
        dl->AddRectFilled(wp + c_vec2(0, s_(2)), wp + ws + c_vec2(0, s_(2)),
                          get_clr(c_vec4(0, 0, 0, 0.35f * g_clr.layout.w)), win_rounding, 0);
        rect_filled(dl, wp, wp + ws, get_clr(g_clr.layout), win_rounding, 0);
        dl->AddRect(wp, wp + ws, get_clr(g_clr.accent, 0.30f), win_rounding, 0, s_(1));

        // ---------------- Header ----------------
        float header_h = s_(40);
        c_rect header(wp, wp + c_vec2(ws.x, header_h));
        rect_filled(dl, header.Min, header.Max,
                    get_clr(g_clr.lightchild, 0.6f), win_rounding, ImDrawFlags_RoundCornersTop);
        // Thin accent line under header.
        dl->AddLine(c_vec2(header.Min.x + s_(8), header.Max.y),
                    c_vec2(header.Max.x - s_(8), header.Max.y),
                    get_clr(g_clr.accent, 0.5f), s_(1));

        // Accent dot.
        dl->AddCircleFilled(c_vec2(header.Min.x + s_(18), header.GetCenter().y),
                            s_(5), get_clr(g_clr.accent), 32);

        // Title text.
        text_clipped(dl, g_font_inter_12,
                     c_vec2(header.Min.x + s_(30), header.Min.y),
                     c_vec2(header.Max.x, header.Max.y),
                     get_clr(g_clr.white), "enhance \xc2\xb7 1.21.11", {0.f, 0.5f});

        // Theme cycle button (top-right of header).
        c_vec2 gear_size = s_(18, 18);
        c_vec2 gear_pos(header.Max.x - gear_size.x - s_(12), header.GetCenter().y - gear_size.y * 0.5f);
        c_rect gear_rect(gear_pos, gear_pos + gear_size);
        bool gear_hover = gear_rect.Contains(ImGui::GetIO().MousePos);
        static float gear_anim = 0.f;
        easing(gear_anim, gear_hover ? 1.f : 0.f, 14.f);
        c_vec4 gear_col = ImLerp(g_clr.text, g_clr.white, gear_anim);
        if (g_font_icons_12) {
            text_clipped(dl, g_font_icons_12, gear_rect.Min, gear_rect.Max,
                         get_clr(gear_col), "E", {0.5f, 0.5f});
        }
        if (gear_hover && ImGui::IsMouseClicked(0)) {
            g_theme = (g_theme + 1) % 4;
            globals::gui_theme_id = g_theme;
        }

        // Make the header drag the window.
        c_rect drag_rect(header.Min, c_vec2(gear_rect.Min.x - s_(4), header.Max.y));
        ImGui::SetCursorScreenPos(drag_rect.Min);
        ImGui::InvisibleButton("##drag_header", drag_rect.GetSize());
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            c_vec2 d = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(wp + d);
        }

        // ---------------- ESC closes GUI ----------------
        // Capture this once, before per-row Keybind widgets may consume the press.
        bool was_capturing_bind = (g_keybind_waiting_id != 0);

        // ---------------- Category columns (Combat | Movement | Visual | Utility | Macros) ----------------
        // Categories arranged horizontally with their modules listed vertically below
        // a small header. Config is intentionally NOT a column — it lives in the gear popup.
        static const char* kVisibleCats[] = { "Combat", "Movement", "Visual", "Utility", "Macros" };
        const int kVisibleCatCount = (int)(sizeof(kVisibleCats) / sizeof(kVisibleCats[0]));
        const float col_spacing = s_(8);
        const float side_pad    = s_(12);
        const float cols_top    = header.Max.y + s_(10);
        const float cols_bot    = wp.y + ws.y - s_(10);
        const float cols_height = cols_bot - cols_top;
        const float total_w     = ws.x - side_pad * 2;
        const float col_w       = (total_w - col_spacing * (kVisibleCatCount - 1)) / (float)kVisibleCatCount;

        const auto& mods = get_modules();

        // Build a per-category alphabetically-sorted index of `mods` once
        // per process. We intentionally don't sort the underlying vector
        // (which would shift indices that the side-panel uses to remember
        // the currently-shown module). Comparing module names case-
        // insensitively so display order doesn't depend on how each entry
        // was originally capitalised.
        static std::vector<size_t> sorted_idx;
        static bool sorted_idx_built = false;
        if (!sorted_idx_built) {
            sorted_idx.resize(mods.size());
            for (size_t i = 0; i < mods.size(); ++i) sorted_idx[i] = i;
            auto ci_less = [&](size_t a, size_t b) {
                const char* na = mods[a].name ? mods[a].name : "";
                const char* nb = mods[b].name ? mods[b].name : "";
                return _stricmp(na, nb) < 0;
            };
            std::sort(sorted_idx.begin(), sorted_idx.end(), ci_less);
            sorted_idx_built = true;
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, c_vec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, c_vec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, g_clr.selectable);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, c_vec4(g_clr.accent.x, g_clr.accent.y, g_clr.accent.z, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, c_vec4(g_clr.accent.x, g_clr.accent.y, g_clr.accent.z, 0.9f));

        for (int ci = 0; ci < kVisibleCatCount; ++ci) {
            const char* cat = kVisibleCats[ci];
            float col_x = wp.x + side_pad + ci * (col_w + col_spacing);
            c_vec2 col_pos(col_x, cols_top);
            c_rect col_rect(col_pos, col_pos + c_vec2(col_w, cols_height));

            // Column panel.
            rect_filled(dl, col_rect.Min, col_rect.Max, get_clr(g_clr.child, 0.65f), s_(6), 0);
            dl->AddRect(col_rect.Min, col_rect.Max, get_clr(g_clr.accent, 0.15f), s_(6), 0, s_(1));

            // Column header.
            float header_band_h = s_(26);
            c_rect cat_band(col_rect.Min, col_rect.Min + c_vec2(col_w, header_band_h));
            rect_filled(dl, cat_band.Min, cat_band.Max,
                        get_clr(g_clr.lightchild, 0.7f), s_(6), ImDrawFlags_RoundCornersTop);
            text_clipped(dl, g_font_inter_12,
                         cat_band.Min, cat_band.Max,
                         get_clr(g_clr.accent), cat, {0.5f, 0.5f});

            // Module list (scrollable inside the column).
            c_vec2 list_pos(col_rect.Min.x + s_(4), col_rect.Min.y + header_band_h + s_(4));
            c_vec2 list_size(col_w - s_(8), cols_height - header_band_h - s_(8));

            ImGui::SetCursorScreenPos(list_pos);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, c_vec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, s_(0, 2));

            char child_id[32]; snprintf(child_id, sizeof(child_id), "##col_%d", ci);
            if (ImGui::BeginChild(child_id, list_size, false,
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoFocusOnAppearing)) {
                int row = 0;
                for (size_t k = 0; k < sorted_idx.size(); ++k) {
                    size_t i = sorted_idx[k];
                    if (strcmp(mods[i].category, cat) != 0) continue;
                    if (RenderModuleRow(mods[i], (ci << 16) | row)) {
                        g_settings_module_idx = (int)i;
                    }
                    row++;
                }
                if (row == 0) {
                    ImGui::PushFont(g_font_inter_11);
                    ImGui::PushStyleColor(ImGuiCol_Text, g_clr.text);
                    ImGui::Dummy(s_(0, 10));
                    ImGui::TextUnformatted("  Empty");
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
        }

        // ---------------- Gear settings popup (theme + GUI Keybind) ----------------
        if (gear_hover && ImGui::IsMouseClicked(1)) {
            // Right-click on the gear opens the config popup (theme cycle is left-click).
            ImGui::OpenPopup("##gear_popup");
        }

        ImGui::PushStyleColor(ImGuiCol_PopupBg, g_clr.layout);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, s_(10, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s_(6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, s_(6, 6));
        if (ImGui::BeginPopup("##gear_popup", ImGuiWindowFlags_NoMove)) {
            c_vec2 pp = ImGui::GetWindowPos();
            c_vec2 psz = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRect(pp, pp + psz, get_clr(g_clr.accent, 0.30f), s_(6), 0, s_(1));

            ImGui::PushFont(g_font_inter_12);
            ImGui::TextColored(g_clr.white, "Config");
            ImGui::PopFont();
            ImGui::Separator();

            // GUI keybind row.
            ImGui::SetNextItemWidth(s_(240));
            Keybind("GUI Keybind", "Key to open this menu", &globals::gui_keybind, &globals::gui_keybind_mode);

            // Theme selector.
            static const std::vector<std::string> theme_names = {"Dark", "Darker", "Void", "Light Blue Glass"};
            int t = g_theme;
            ImGui::SetNextItemWidth(s_(240));
            Dropdown("Theme", "Color scheme", &t, theme_names);
            if (t != g_theme) {
                g_theme = t;
                globals::gui_theme_id = t;
            }

            // Scale slider.
            extern float g_menu_scale;
            ImGui::SetNextItemWidth(s_(240));
            Slider("Menu scale", "Resize the GUI", &g_menu_scale, 100.f, 200.f, "%.0f");

            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();

        ImGui::PopStyleColor(5);

        // ---------------- ESC handling ----------------
        // Close GUI on Escape (unless a keybind capture was in progress at frame start —
        // that capture's own ESC-handler cancels itself first, so we leave the GUI alone).
        if (!was_capturing_bind && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g_settings_module_idx = -1;
            GUI::set_do_draw(false);
        }
    }
    c_vec2 main_wp = ImGui::GetWindowPos();
    c_vec2 main_ws = ImGui::GetWindowSize();
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // ---------------- Side settings panel ----------------
    // Floats to the right of the main window. Always at the same screen-relative
    // position so right-clicking different modules never makes the popup appear
    // on top of other module rows.
    const auto& mods = get_modules();
    if (g_settings_module_idx >= 0 && g_settings_module_idx < (int)mods.size()) {
        const ModuleEntry& e = mods[g_settings_module_idx];

        c_vec2 panel_size = s_(260, 460);
        c_vec2 panel_pos  = main_wp + c_vec2(main_ws.x + s_(8), 0);

        // Clamp to display so it doesn't render off-screen.
        c_vec2 display = ImGui::GetIO().DisplaySize;
        if (panel_pos.x + panel_size.x > display.x) {
            panel_pos.x = main_wp.x - panel_size.x - s_(8); // flip to left side
        }
        if (panel_pos.x < 0) panel_pos.x = 0;

        ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(panel_size, ImGuiCond_Always);

        ImGuiWindowFlags pflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, s_(12, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, s_(6, 6));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, c_vec4(0, 0, 0, 0));

        if (ImGui::Begin("##settings_panel", nullptr, pflags)) {
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            c_vec2 pp = ImGui::GetWindowPos();
            c_vec2 psz = ImGui::GetWindowSize();

            // Shadow + bg + border.
            pdl->AddRectFilled(pp + c_vec2(0, s_(2)), pp + psz + c_vec2(0, s_(2)),
                               get_clr(c_vec4(0, 0, 0, 0.35f * g_clr.layout.w)), s_(8), 0);
            rect_filled(pdl, pp, pp + psz, get_clr(g_clr.layout), s_(8), 0);
            pdl->AddRect(pp, pp + psz, get_clr(g_clr.accent, 0.35f), s_(8), 0, s_(1));

            // Header bar.
            float ph = s_(34);
            c_rect pheader(pp, pp + c_vec2(psz.x, ph));
            rect_filled(pdl, pheader.Min, pheader.Max, get_clr(g_clr.lightchild, 0.6f),
                        s_(8), ImDrawFlags_RoundCornersTop);
            pdl->AddLine(c_vec2(pheader.Min.x + s_(8), pheader.Max.y),
                         c_vec2(pheader.Max.x - s_(8), pheader.Max.y),
                         get_clr(g_clr.accent, 0.5f), s_(1));
            text_clipped(pdl, g_font_inter_12,
                         pheader.Min + c_vec2(s_(12), 0), pheader.Max,
                         get_clr(g_clr.white), e.name, {0.f, 0.5f});

            // Close button (X) on the right.
            c_vec2 close_size = s_(16, 16);
            c_vec2 close_pos(pheader.Max.x - close_size.x - s_(10),
                             pheader.GetCenter().y - close_size.y * 0.5f);
            c_rect close_rect(close_pos, close_pos + close_size);
            bool close_hover = close_rect.Contains(ImGui::GetIO().MousePos);
            if (g_font_icons_12) {
                text_clipped(pdl, g_font_icons_12, close_rect.Min, close_rect.Max,
                             get_clr(ImLerp(g_clr.text, g_clr.white, close_hover ? 1.f : 0.f)),
                             "G", {0.5f, 0.5f}); // "G" icon as close glyph; fallback
            }
            if (close_hover && ImGui::IsMouseClicked(0)) {
                g_settings_module_idx = -1;
            }

            // Body content.
            ImGui::SetCursorPos(s_(12, 12) + c_vec2(0, ph));

            if (e.description) {
                ImGui::PushFont(g_font_inter_11);
                ImGui::PushStyleColor(ImGuiCol_Text, g_clr.text);
                ImGui::TextWrapped("%s", e.description);
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::Dummy(s_(0, 4));
            }

            if (e.keybind) {
                // No SetNextItemWidth here — panel_size is already scaled, so
                // calling s_(...) on it would double-scale and push the widget
                // off-screen. Let Keybind use content_avail_x() of the panel.
                Keybind("Keybind", "Activation key", e.keybind, e.mode);
                if (e.mode && *e.keybind != 0 && *e.mode == 2) *e.mode = 1;
            }

            if (e.render_settings) {
                e.render_settings();
            } else if (!e.keybind) {
                ImGui::PushFont(g_font_inter_11);
                ImGui::PushStyleColor(ImGuiCol_Text, g_clr.text);
                ImGui::TextUnformatted("No additional settings.");
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

void RenderGamblingWindow() {
    auto& game = enhance::modules::gambling::g_mines_game;
    if (!game.isWindowOpen()) return;
    
    c_vec2 window_size = s_(500, 600);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, s_(g_elem.content.padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, s_(g_elem.content.padding));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, c_vec4(0, 0, 0, 0));
    
    if (ImGui::Begin("Mines Game", &game.window_open, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        c_vec2 win_pos = ImGui::GetWindowPos();
        c_vec2 win_size = ImGui::GetWindowSize();
        
        rect_filled(dl, win_pos, win_pos + win_size, get_clr(g_clr.layout), s_(g_elem.window.rounding), 0);
        
        if (BeginChildWindow("Mines", s_(0, 0), false)) {
            char balance_text[64];
            snprintf(balance_text, sizeof(balance_text), "Balance: %.2f", game.balance);
            ImGui::Text(balance_text);
            
            if (!game.game_active && !game.game_won && !game.game_lost) {
                Slider("Bet Amount", "Amount to bet", &game.bet_amount, 0.1f, game.balance, "%.2f");
                if (game.bet_amount < 0.1f) game.bet_amount = 0.1f;
                if (game.bet_amount > game.balance) game.bet_amount = game.balance;
                
                float num_mines_f = (float)game.num_mines;
                Slider("Number of Mines", "Mines to place", &num_mines_f, 1.0f, (float)(game.grid_size * game.grid_size - 1), "%.0f");
                game.num_mines = (int)num_mines_f;
                if (game.num_mines < 1) game.num_mines = 1;
                if (game.num_mines > game.grid_size * game.grid_size - 1) game.num_mines = game.grid_size * game.grid_size - 1;
                
                if (Button("Start Game")) {
                    if (game.bet_amount <= game.balance) {
                        game.balance -= game.bet_amount;
                        game.game_active = true;
                        game.generateGrid();
                    }
                }
            } else {
                if (game.game_won) {
                    char win_text[64];
                    snprintf(win_text, sizeof(win_text), "You Won! +%.2f", game.bet_amount * game.current_multiplier);
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), win_text);
                    if (Button("Play Again")) {
                        game.resetGame();
                    }
                } else if (game.game_lost) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "You Hit a Mine!");
                    if (Button("Play Again")) {
                        game.resetGame();
                    }
                } else {
                    char mult_text[64];
                    snprintf(mult_text, sizeof(mult_text), "Multiplier: %.2fx", game.current_multiplier);
                    ImGui::Text(mult_text);
                    char win_text[64];
                    snprintf(win_text, sizeof(win_text), "Potential Win: %.2f", game.bet_amount * game.current_multiplier);
                    ImGui::Text(win_text);
                    
                    if (Button("Cash Out")) {
                        game.balance += game.bet_amount * game.current_multiplier;
                        game.resetGame();
                    }
                    
                    ImGui::Spacing();
                    
                    float tile_size = s_(35);
                    float spacing = s_(4);
                    float total_width = game.grid_size * tile_size + (game.grid_size - 1) * spacing;
                    float start_x = (content_avail_x() - total_width) * 0.5f;
                    
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);
                    
                    for (int i = 0; i < game.grid_size; i++) {
                        for (int j = 0; j < game.grid_size; j++) {
                            if (j > 0) ImGui::SameLine(0, spacing);
                            
                            ImVec4 button_color = g_clr.button;
                            std::string label = "?";
                            
                            if (game.revealed[i][j]) {
                                if (game.grid[i][j] == -1) {
                                    button_color = ImVec4(1, 0, 0, 1);
                                    label = "*";
                                } else {
                                    button_color = g_clr.accent;
                                    label = "";
                                }
                            }
                            
                            ImGui::PushID(i * game.grid_size + j);
                            ImGui::PushStyleColor(ImGuiCol_Button, button_color);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(button_color.x * 1.2f, button_color.y * 1.2f, button_color.z * 1.2f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(button_color.x * 0.8f, button_color.y * 0.8f, button_color.z * 0.8f, 1.0f));
                            
                            if (ImGui::Button(label.c_str(), ImVec2(tile_size, tile_size))) {
                                if (game.game_active) {
                                    game.revealTile(i, j);
                                }
                            }
                            
                            ImGui::PopStyleColor(3);
                            ImGui::PopID();
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (Button("Close")) {
                game.closeWindow();
            }
        }
        EndChildWindow();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}


bool GUI::get_is_init()
{
	return is_init;
}

bool GUI::get_do_draw()
{
	return do_draw;
}

extern void wolf_menu_reset_popups();

void GUI::set_do_draw(bool new_value)
{
	if (do_draw == new_value) return;

	// Closing the menu is the natural commit point: everything the user just
	// changed is in globals, and the debounce keeps the write off the frame
	// where the click happened.
	if (do_draw && !new_value)
		enhance::config::touch();

	if (do_draw && !new_value)
	{
		wolf_menu_reset_popups();
	}
	do_draw = new_value;
	globals::show_gui = new_value;

	// Release cursor from MC's capture so the user can interact with the menu.
	// When closing, hide it again so MC's GLFW cursor capture resumes cleanly.
	if (new_value)
	{
		ClipCursor(NULL);
		int guard = 0;
		while (ShowCursor(TRUE)  <  0 && guard++ < 16) {}
	}
	else
	{
		int guard = 0;
		while (ShowCursor(FALSE) >= 0 && guard++ < 16) {}
	}
}
