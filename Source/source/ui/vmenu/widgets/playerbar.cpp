#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "playerbar.h"

void c_playerbar::render()
{
    if (!fgui || !draw || !font)
        return;

    float spawn_alpha = fvar->fgui.playerbar_spawn_alpha;
    if (spawn_alpha <= 0.01f)
        return;

    float spawn_scale = 0.85f + (spawn_alpha * 0.15f);

    try
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        ImFont* text_font  = font->get(main_font_data, 13.0f);
        ImFont* chip_font  = font->get(main_font_data, 10.0f);
        if (!text_font || !chip_font)
            return;

        const float fs = 13.0f;
        static const char* ico_clock = "\xEF\x80\x97"; // U+F017

        ImVec2 name_sz = text_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, username.c_str());
        ImVec2 clk_sz  = chip_font->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, ico_clock);
        ImVec2 lic_sz  = chip_font->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, key_time_left.c_str());

        float avatar_size = SCALE(36.0f);
        float pad_l  = SCALE(9.0f);
        float gap    = SCALE(10.0f);
        float pad_r  = SCALE(12.0f);
        float chip_w = SCALE(5.0f) + clk_sz.x + SCALE(4.0f) + lic_sz.x + SCALE(16.0f);

        float width  = pad_l + avatar_size + gap + name_sz.x + pad_r;
        if (key_set)
            width += SCALE(8.0f) + chip_w;

        float height = SCALE(54.0f);

        if (!placed)
        {
            placed = true;
            playerbar_pos = ImVec2(SCALE(16.0f), SCALE(100.0f));
        }

        ImVec2 pos  = ImVec2(playerbar_pos.x, playerbar_pos.y);
        ImVec2 size = ImVec2(width, height);

        ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImVec2 scaled_pos = ImVec2(
            center.x + (pos.x - center.x) * spawn_scale,
            center.y + (pos.y - center.y) * spawn_scale
        );
        ImVec2 scaled_size = ImVec2(size.x * spawn_scale, size.y * spawn_scale);

        ImRect bar_rect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));

        // Dragging
        bool hovered   = bar_rect.Contains(ImGui::GetMousePos());
        bool menu_open = fvar->fgui.menu_open;

        if (menu_open && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            is_dragging = true;
            drag_offset = ImVec2(
                ImGui::GetMousePos().x - pos.x,
                ImGui::GetMousePos().y - pos.y
            );
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            is_dragging = false;

        if (is_dragging)
        {
            playerbar_pos.x = ImGui::GetMousePos().x - drag_offset.x;
            playerbar_pos.y = ImGui::GetMousePos().y - drag_offset.y;

            playerbar_pos.x = ImMax(0.0f, ImMin(playerbar_pos.x, io.DisplaySize.x - width));
            playerbar_pos.y = ImMax(0.0f, ImMin(playerbar_pos.y, io.DisplaySize.y - height));

            pos = ImVec2(playerbar_pos.x, playerbar_pos.y);

            center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
            scaled_pos = ImVec2(
                center.x + (pos.x - center.x) * spawn_scale,
                center.y + (pos.y - center.y) * spawn_scale
            );
            scaled_size = ImVec2(size.x * spawn_scale, size.y * spawn_scale);
            bar_rect = ImRect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));
        }

        int a = (int)(spawn_alpha * 255.0f);

        // Soft drop shadow
        float shadow_blur = SCALE(12.0f);
        for (int s = 0; s < 5; s++)
        {
            float p  = (float)s / 5.0f;
            float al = (1.0f - p) * 0.32f * spawn_alpha;
            float bl = shadow_blur * p;
            draw_list->AddRectFilled(
                ImVec2(scaled_pos.x - bl, scaled_pos.y - bl),
                ImVec2(scaled_pos.x + scaled_size.x + bl, scaled_pos.y + scaled_size.y + bl),
                IM_COL32(0, 0, 0, (int)(al * 255)),
                SCALE(9.0f) + bl);
        }

        // Glass plate
        draw_list->PushClipRect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y), true);
        draw_list->AddRectFilledMultiColor(
            scaled_pos,
            ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y),
            IM_COL32(38, 40, 45, (int)(0.78f * a)),
            IM_COL32(38, 40, 45, (int)(0.78f * a)),
            IM_COL32(24, 26, 30, (int)(0.80f * a)),
            IM_COL32(24, 26, 30, (int)(0.80f * a)));
        draw_list->PopClipRect();

        // Border
        draw_list->AddRect(scaled_pos,
            ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y),
            IM_COL32(255, 255, 255, (int)(0.12f * a)),
            SCALE(9.0f), 0, SCALE(1.0f));

        // Avatar
        float av_r = avatar_size * 0.5f * spawn_scale;
        ImVec2 av_c = ImVec2(scaled_pos.x + (pad_l + avatar_size * 0.5f) * spawn_scale,
                             scaled_pos.y + scaled_size.y * 0.5f);

        draw_list->AddCircleFilled(av_c, av_r + SCALE(1.5f), IM_COL32(143, 200, 255, (int)(0.22f * a)), 32);
        draw_list->AddCircleFilled(av_c, av_r, IM_COL32(143, 200, 255, (int)(0.20f * a)), 32);
        draw_list->AddCircleFilled(av_c, av_r * 0.72f, IM_COL32(201, 168, 255, (int)(0.14f * a)), 32);
        draw_list->AddCircle(av_c, av_r, IM_COL32(143, 200, 255, (int)(0.65f * a)), 32, SCALE(1.5f));

        static const char* user_glyph = "\xEF\x80\x87"; // U+F007 user
        ImFont* glyph_font = font->get(main_font_data, 14.0f);
        if (glyph_font)
        {
            ImVec2 g_sz = glyph_font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, user_glyph);
            draw_list->AddText(glyph_font, 14.0f,
                ImVec2(av_c.x - g_sz.x * 0.5f, av_c.y - g_sz.y * 0.5f),
                IM_COL32(220, 235, 255, (int)(0.92f * a)), user_glyph);
        }

        // Online status dot
        ImVec2 dot_c = ImVec2(av_c.x + av_r * 0.72f, av_c.y + av_r * 0.72f);
        draw_list->AddCircleFilled(dot_c, SCALE(5.5f), IM_COL32(20, 22, 26, a), 20);
        draw_list->AddCircleFilled(dot_c, SCALE(4.5f), IM_COL32(76, 242, 76, a), 20);
        draw_list->AddCircleFilled(dot_c, SCALE(6.5f), IM_COL32(76, 242, 76, (int)(0.16f * a)), 20);

        // Username
        float text_x = scaled_pos.x + (pad_l + avatar_size + gap) * spawn_scale;
        float text_y = scaled_pos.y + (scaled_size.y - name_sz.y * spawn_scale) * 0.5f;
        draw_list->AddText(text_font, fs, ImVec2(text_x, text_y),
            IM_COL32(255, 255, 255, (int)(0.95f * a)), username.c_str());

        // License countdown (only when a key is set up)
        if (key_set)
        {
            float chip_h = SCALE(18.0f);
            float chip_x = text_x + (name_sz.x + SCALE(8.0f)) * spawn_scale;
            float chip_y = scaled_pos.y + (scaled_size.y - chip_h * spawn_scale) * 0.5f;

            ImVec2 chip_min(chip_x, chip_y);
            ImVec2 chip_max(chip_x + chip_w * spawn_scale, chip_y + chip_h * spawn_scale);

            draw_list->AddRectFilled(chip_min, chip_max,
                IM_COL32(143, 200, 255, (int)(0.12f * a)), SCALE(5.0f));
            draw_list->AddRect(chip_min, chip_max,
                IM_COL32(143, 200, 255, (int)(0.38f * a)), SCALE(5.0f), 0, SCALE(1.0f));

            float inner_y = chip_y + (chip_h * spawn_scale - lic_sz.y * spawn_scale) * 0.5f;
            float ix = chip_x + SCALE(8.0f) * spawn_scale;

            draw_list->AddText(chip_font, 10.0f,
                ImVec2(ix, chip_y + (chip_h * spawn_scale - clk_sz.y * spawn_scale) * 0.5f),
                IM_COL32(143, 200, 255, (int)(0.80f * a)), ico_clock);
            ix += (clk_sz.x + SCALE(4.0f)) * spawn_scale;

            draw_list->AddText(chip_font, 10.0f, ImVec2(ix, inner_y),
                IM_COL32(143, 200, 255, a), key_time_left.c_str());
        }
    }
    catch (...)
    {
    }
}
