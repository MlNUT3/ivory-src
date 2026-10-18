#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include <windows.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "colors.h"
#include "variables.h"
#include "elements.h"
#include <vector>
#include <sstream>
#include <string>

using namespace ImGui;

// NOTE: c_gui / c_draw / c_notify and the gui / draw / notify globals now live
// in source/ui/menufw (the reference menu framework). This header keeps only
// the compat shims the features still use (g_menu, orok_config, colors).

namespace g {
    inline HWND hWindow = nullptr;
    inline int iWidth = 1920;
    inline int iHeight = 1080;
    inline float StoredAlpha = 1.0f;
}

struct MenuCompat {
    bool IsMenuOpened() { return var ? var->gui.menu_opened : false; }
    void SetMenuOpened(bool v) { if (var) var->gui.menu_opened = v; }
};
inline MenuCompat g_menu;

class ConfigSystem {
public:
    float menu_color[4] = { 221 / 255.f, 168 / 255.f, 93 / 255.f, 1.f };
    float menu_color_secondary[4] = { 44 / 255.f, 48 / 255.f, 55 / 255.f, 1.f };
    float menu_color_border[4] = { 78 / 255.f, 81 / 255.f, 88 / 255.f, 1.f };
    float menu_color_child[4] = { 31 / 255.f, 33 / 255.f, 37 / 255.f, 1.f };
    float menu_color_text[4] = { 214 / 255.f, 217 / 255.f, 224 / 255.f, 1.f };
    float menu_color_button[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_button_hover[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_button_active[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_frame[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_frame_hover[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_frame_active[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_popup[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_header[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_header_hover[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_header_active[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_checkmark[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_slider_grab[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_slider_grab_active[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_scrollbar_bg[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_color_scrollbar_grab[4] = { 0.f, 0.f, 0.f, 1.f };
    float menu_size[2] = { 680.0f, 580.0f };
    float menu_window_rounding = 0.0f;
    float menu_frame_rounding = 0.0f;
    float menu_scrollbar_size = 6.0f;
    bool custom_cursor = false;
    void SetupValues() {}
};
inline ConfigSystem orok_config;

#ifndef ImGuiWindowFlags_NpcSystem
#define ImGuiWindowFlags_NpcSystem 0
#endif

#ifndef ImGuiCol_ButtonShadow
#define ImGuiCol_ButtonShadow ImGuiCol_BorderShadow
#endif

#ifndef ImGuiChildFlags_Borders
#define ImGuiChildFlags_Borders ImGuiChildFlags_Border
#endif

inline ImVec2 g_saved_explorer_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_explorer_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_perf_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_perf_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_spotify_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_spotify_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_main_menu_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_main_menu_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_settings_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_settings_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_luau_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_luau_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_preview_pos = ImVec2(-1.f, -1.f);
inline ImVec2 g_saved_preview_size = ImVec2(-1.f, -1.f);
inline ImVec2 g_main_menu_pos = ImVec2(100.0f, 100.0f);
inline ImVec2 g_main_menu_size = ImVec2(559.0f, 668.0f);

namespace ImGui {
    inline bool SingleSelect(const char* label, int* current_item, const std::vector<const char*>& items) {
        const char* preview_item = (current_item && *current_item >= 0 && *current_item < (int)items.size()) ? items[*current_item] : "";
        bool clicked = false;
        if (ImGui::BeginCombo(label, preview_item)) {
            for (int i = 0; i < (int)items.size(); i++) {
                bool is_selected = (*current_item == i);
                if (ImGui::Selectable(items[i], is_selected)) {
                    *current_item = i;
                    clicked = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return clicked;
    }

    inline bool ListBoxHeader(const char* label, const ImVec2& size = ImVec2(0, 0)) {
        return ImGui::BeginListBox(label, size);
    }
    inline void ListBoxFooter() {
        ImGui::EndListBox();
    }
}

namespace ui = ImGui;
