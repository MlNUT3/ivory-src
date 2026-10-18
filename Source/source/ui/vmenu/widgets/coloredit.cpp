#include "../headers/functions.h"
#include "../headers/widgets.h"

void render_color_rect_with_alpha_checkboard(ImDrawList* draw_list, ImVec2 p_min, ImVec2 p_max, ImU32 col, float grid_step, ImVec2 grid_off, float rounding, ImDrawFlags flags)
{
    if ((flags & ImDrawFlags_RoundCornersMask_) == 0)
        flags = ImDrawFlags_RoundCornersDefault_;

    if (((col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) < 0xFF)
    {
        ImU32 col_bg1 = draw->get_clr(ImColor(27, 27, 32));
        ImU32 col_bg2 = draw->get_clr(ImColor(46, 46, 56));
        draw_list->AddRectFilled(p_min, p_max, col_bg1, rounding, flags);

        int yi = 0;
        for (float y = p_min.y + grid_off.y; y < p_max.y; y += grid_step, yi++)
        {
            float y1 = ImClamp(y, p_min.y, p_max.y), y2 = ImMin(y + grid_step, p_max.y);
            if (y2 <= y1)
                continue;

            for (float x = p_min.x + grid_off.x + (yi & 1) * grid_step; x < p_max.x; x += grid_step * 2.0f)
            {
                float x1 = ImClamp(x, p_min.x, p_max.x), x2 = ImMin(x + grid_step, p_max.x);
                if (x2 <= x1)
                    continue;

                ImDrawFlags cell_flags = ImDrawFlags_RoundCornersNone;
                if (y1 <= p_min.y) { if (x1 <= p_min.x) cell_flags |= ImDrawFlags_RoundCornersTopLeft; if (x2 >= p_max.x) cell_flags |= ImDrawFlags_RoundCornersTopRight; }
                if (y2 >= p_max.y) { if (x1 <= p_min.x) cell_flags |= ImDrawFlags_RoundCornersBottomLeft; if (x2 >= p_max.x) cell_flags |= ImDrawFlags_RoundCornersBottomRight; }

                cell_flags = (flags == ImDrawFlags_RoundCornersNone || cell_flags == ImDrawFlags_RoundCornersNone) ? ImDrawFlags_RoundCornersNone : (cell_flags & flags);
                draw_list->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), col_bg2, rounding, cell_flags);
            }
        }
    }
    else
    {
        draw_list->AddRectFilled(p_min, p_max, col, rounding, flags);
    }
}

void c_widgets::color_picker(std::string_view name, float* color)
{
    struct anim_t
    {
        bool opened{ false };
        bool hovered{ false };
        float alpha{ 0.f };
        float h{ -1 }, s{ -1 }, v{ -1 };
        float grab[4]{ 0.f };
        bool update_hsv{ false };
        float box_hover{ 0.f };
    };

    ImGuiWindow* window = fgui->get_window();
    ImGuiID id = window->GetID(name.data());

    float box_width = SCALE(24.0f);
    float box_height = SCALE(16.0f);
    float box_padding = SCALE(8.0f);
    float left_offset = SCALE(0.0f);
    float top_offset = SCALE(6.0f);

    ImVec2 pos = window->DC.CursorPos;
    pos.x += left_offset;
    pos.y += top_offset;

    float width = fgui->content_avail().x - SCALE(10.0f);

    // Text size
    ImFont* text_font = font->get(main_font_data, 13.0f);
    ImVec2 text_size = fgui->text_size(text_font, name.data(), fgui->text_end(name.data()));

    float total_height = box_height + (box_padding * 2.0f);

    ImRect full_rect(pos, ImVec2(pos.x + width, pos.y + total_height));
    ImRect clickable_rect(ImVec2(pos.x + width - box_width - box_padding, pos.y + box_padding),
                          ImVec2(pos.x + width - box_padding, pos.y + box_padding + box_height));

    anim_t* anim = fgui->anim_container<anim_t>(id);

    bool value_changed = false;

    ImColor col_hues[7] = { ImColor(255, 0, 0), ImColor(255, 255, 0), ImColor(0, 255, 0), ImColor(0, 255, 255), ImColor(0, 0, 255), ImColor(255, 0, 255), ImColor(255, 0, 0) };

    bool is_hovered = clickable_rect.Contains(fgui->mouse_pos());

    if ((clickable_rect.Contains(fgui->mouse_pos()) && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None)) || (anim->opened && (fgui->mouse_clicked(mouse_button_left) || fgui->mouse_clicked(mouse_button_right)) && !anim->hovered))
        anim->opened = !anim->opened;

    fgui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    fgui->easing(anim->box_hover, is_hovered ? 1.f : 0.f, 15.f, static_easing);

    ImVec4 box_bg = ImVec4(66.0f / 255.0f, 68.0f / 255.0f, 72.0f / 255.0f, 0.62f);
    ImVec4 box_bg_detail = ImVec4(54.0f / 255.0f, 56.0f / 255.0f, 60.0f / 255.0f, 0.62f);

    if (anim->box_hover > 0.01f)
    {
        float shadow_strength = 0.15f * anim->box_hover;
        float shadow_blur = SCALE(8.0f);
        for (int s = 0; s < 3; s++)
        {
            float progress = (float)s / 3.0f;
            float alpha = (1.0f - progress) * 0.08f * shadow_strength;
            float blur_amount = shadow_blur * progress;

            fgui->window_drawlist()->AddRectFilled(
                ImVec2(pos.x - blur_amount, pos.y - blur_amount),
                ImVec2(pos.x + width + blur_amount, pos.y + total_height + blur_amount),
                IM_COL32(0, 0, 0, (int)(alpha * 255)),
                SCALE(6.0f)
            );
        }
    }

    // Detail Layer
    draw->rect_filled(fgui->window_drawlist(),
        ImVec2(pos.x + SCALE(1.0f), pos.y + SCALE(1.0f)),
        ImVec2(pos.x + width, pos.y + total_height),
        draw->get_clr(box_bg_detail), SCALE(6.0f));

    // Main Box
    draw->rect_filled(fgui->window_drawlist(), pos,
        ImVec2(pos.x + width, pos.y + total_height),
        draw->get_clr(box_bg), SCALE(6.0f));

    draw->rect(fgui->window_drawlist(), pos,
        ImVec2(pos.x + width, pos.y + total_height),
        draw->get_clr(ImVec4(0.0f, 0.0f, 0.0f, 0.3f)), SCALE(6.0f), 0, SCALE(1.0f));

    // Text
    draw->text(fgui->window_drawlist(), text_font, 13.0f,
        ImVec2(pos.x + box_padding, pos.y + (total_height - text_size.y) * 0.5f),
        draw->get_clr(ImVec4(0.88f, 0.88f, 0.88f, 1.0f)), name.data());

    if (anim->box_hover > 0.01f)
    {
        int glow_layers = 10;
        float max_glow_radius = SCALE(8.0f);

        for (int i = glow_layers; i >= 1; i--)
        {
            float progress = (float)i / (float)glow_layers;
            float radius = max_glow_radius * progress;
            float alpha = (1.0f - progress) * 0.08f * anim->box_hover;

            fgui->window_drawlist()->AddRectFilled(
                ImVec2(clickable_rect.Min.x - radius, clickable_rect.Min.y - radius),
                ImVec2(clickable_rect.Max.x + radius, clickable_rect.Max.y + radius),
                IM_COL32(255, 255, 255, (int)(alpha * 255)),
                SCALE(4.0f) + radius * 0.5f
            );
        }
    }

    render_color_rect_with_alpha_checkboard(fgui->window_drawlist(), clickable_rect.Min, clickable_rect.Max,
        draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), clickable_rect.GetWidth() / 2, ImVec2(0, 0), SCALE(4.0f), 0);

    draw->rect_filled(fgui->window_drawlist(), clickable_rect.Min, clickable_rect.Max,
        draw->get_clr({ color[0], color[1], color[2], color[3] }), SCALE(4.0f));

    draw->rect_filled_multi_color(fgui->window_drawlist(), clickable_rect.Min, clickable_rect.Max,
        draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 0.f }),
        draw->get_clr({ 0.f, 0.f, 0.f, 0.3f }), draw->get_clr({ 0.f, 0.f, 0.f, 0.3f }), SCALE(4.0f));

    if ((anim->h < 0.f || anim->s < 0.f || anim->v < 0.f) || anim->update_hsv)
    {
        fgui->rgb_to_hsv(color[0], color[1], color[2], anim->h, anim->s, anim->v);
        anim->update_hsv = false;
    }

    anim->update_hsv = true;

    if (anim->alpha >= 0.01f)
    {
        fgui->push_var(style_var_alpha, anim->alpha);
        fgui->push_var(style_var_item_spacing, SCALE(ImVec2(8, 8)));
        fgui->push_var(style_var_window_padding, SCALE(ImVec2(10, 10)));
        fgui->push_var(style_var_window_rounding, SCALE(8.0f));
        fgui->push_var(style_var_popup_border_size, SCALE(0));

        fgui->push_color(style_col_popup_bg, draw->get_clr(ImVec4(66.0f / 255.0f, 68.0f / 255.0f, 72.0f / 255.0f, 0.62f)));
        fgui->push_color(style_col_border, draw->get_clr(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)));

        fgui->set_next_window_size(SCALE(ImVec2(250 + 20, 0)));
        fgui->set_next_window_pos(clickable_rect.GetBL() - ImVec2((SCALE(250 + 20) - clickable_rect.GetWidth()) / 2, -SCALE(10)), ImGuiCond_Appearing);

        fgui->begin("coloredit_window" + std::string(name), nullptr,
            window_flags_always_use_window_padding |
            window_flags_no_saved_settings |
            window_flags_always_auto_resize |
            window_flags_no_scrollbar |
            window_flags_no_scroll_with_mouse |
            window_flags_no_collapse |
            window_flags_no_resize |
            window_flags_no_title_bar);
        {
            fgui->set_window_focus();
            anim->hovered = fgui->is_window_hovered(ImGuiHoveredFlags_None);

            // Custom Header (verschiebbar)
            ImVec2 header_min = fgui->window_pos();
            ImVec2 header_max = ImVec2(fgui->window_pos().x + fgui->window_width(), fgui->window_pos().y + SCALE(30));
            ImRect header_rect(header_min, header_max);

            ImGui::SetCursorPos(ImVec2(0, 0));
            fgui->invisible_button("##drag_header", ImVec2(fgui->window_width(), SCALE(30)));
            if (fgui->is_item_active() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                ImVec2 new_pos = fgui->window_pos() + delta;
                ImGui::SetWindowPos(new_pos);
            }

            fgui->dummy(SCALE(ImVec2(1, 30 - 10)));

            draw->rect_filled(fgui->window_drawlist(), fgui->window_pos(), fgui->window_pos() + ImVec2(fgui->window_width(), SCALE(30)),
                draw->get_clr(ImVec4(33.0f / 255.0f, 33.0f / 255.0f, 38.0f / 255.0f, 1.0f)), SCALE(8), draw_flags_round_corners_top);

            draw->text_clipped(fgui->window_drawlist(), text_font, fgui->window_pos() + SCALE(ImVec2(10, 0)), fgui->window_pos() + ImVec2(fgui->window_width(), SCALE(30)),
                draw->get_clr(ImVec4(0.88f, 0.88f, 0.88f, 1.0f)), name.data(), fgui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

            // sv_rect
            float sv_size = SCALE(250.0f);
            float sv_grab_size = SCALE(12.0f);
            float bar_height = SCALE(20.0f);
            float bar_width = SCALE(3.0f);
            float bar_padding = SCALE(2.0f);
            float rounding = SCALE(6.0f);

            fgui->invisible_button("sv_rect", ImVec2(sv_size, sv_size));
            if (fgui->is_item_active())
            {
                anim->s = ImSaturate((fgui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + sv_grab_size / 2)) / (GImGui->LastItemData.Rect.GetWidth() - sv_grab_size));
                anim->v = 1.f - ImSaturate((fgui->mouse_pos().y - (GImGui->LastItemData.Rect.Min.y + sv_grab_size / 2)) / (GImGui->LastItemData.Rect.GetHeight() - sv_grab_size));
                value_changed = true;
            }

            fgui->easing(anim->grab[0], sv_grab_size / 2 + anim->s * (GImGui->LastItemData.Rect.GetWidth() - sv_grab_size), 20.f, dynamic_easing);
            fgui->easing(anim->grab[1], sv_grab_size / 2 + (1.f - anim->v) * (GImGui->LastItemData.Rect.GetHeight() - sv_grab_size), 20.f, dynamic_easing);

            float R, G, B;
            ColorConvertHSVtoRGB(anim->h, 1.f, 1.f, R, G, B);
            draw->rect_filled_multi_color(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min, GImGui->LastItemData.Rect.Max, draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), rounding);
            draw->rect_filled_multi_color(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min, GImGui->LastItemData.Rect.Max, draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), rounding - 1);
            draw->circle(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[0], anim->grab[1]), sv_grab_size / 2, draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 30, SCALE(1));

            // hue_bar
            fgui->invisible_button("hue_bar", ImVec2(sv_size, bar_height));
            if (fgui->is_item_active())
            {
                anim->h = ImSaturate((fgui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + bar_width / 2)) / (GImGui->LastItemData.Rect.GetWidth() - bar_width));
                value_changed = true;
            }

            fgui->easing(anim->grab[2], bar_width / 2 + anim->h * (GImGui->LastItemData.Rect.GetWidth() - bar_width), 20.f, dynamic_easing);

            for (int i = 0; i < IM_ARRAYSIZE(col_hues) - 1; ++i)
                draw->rect_filled_multi_color(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(roundf(i * (GImGui->LastItemData.Rect.GetWidth() / 6)), bar_padding), ImVec2(GImGui->LastItemData.Rect.Min.x + roundf((i + 1) * (GImGui->LastItemData.Rect.GetWidth() / 6)), GImGui->LastItemData.Rect.Max.y - bar_padding), draw->get_clr(col_hues[i]), draw->get_clr(col_hues[i + 1]), draw->get_clr(col_hues[i + 1]), draw->get_clr(col_hues[i]), rounding, (i == 0) ? draw_flags_round_corners_left : (i == 5) ? draw_flags_round_corners_right : draw_flags_round_corners_none);

            draw->rect_filled(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[2] - bar_width / 2, 0), GImGui->LastItemData.Rect.GetBL() + ImVec2(anim->grab[2] + bar_width / 2, 0), draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), SCALE(2));

            // alpha_bar
            fgui->invisible_button("alpha_bar", ImVec2(sv_size, bar_height));
            if (fgui->is_item_active())
                color[3] = ImSaturate((fgui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + bar_width / 2)) / (GImGui->LastItemData.Rect.GetWidth() - bar_width));

            fgui->easing(anim->grab[3], bar_width / 2 + color[3] * (GImGui->LastItemData.Rect.GetWidth() - bar_width), 20.f, dynamic_easing);

            draw->rect_filled_multi_color(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(0, bar_padding), GImGui->LastItemData.Rect.Max - ImVec2(0, bar_padding), draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), rounding);
            draw->rect_filled(fgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[3] - bar_width / 2, 0), GImGui->LastItemData.Rect.GetBL() + ImVec2(anim->grab[3] + bar_width / 2, 0), draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), SCALE(2));
        }
        fgui->end();
        fgui->pop_color(2);
        fgui->pop_var(5);
    }

    if (value_changed)
    {
        fgui->hsv_to_rgb(anim->h, anim->s, anim->v, color[0], color[1], color[2]);
    }

    // Tooltip
    render_tooltip(name, is_hovered);

    // Advance cursor
    fgui->item_size(full_rect);
    fgui->item_add(full_rect, id);
}
