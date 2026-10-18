#include "../headers/functions.h"
#include "../headers/widgets.h"

void c_widgets::dropdown(std::string_view name, int* value, const char* items[], int items_count)
{
    if (!value || !items || items_count <= 0)
        return;

    try
    {
        struct anim_t
        {
            float hover_alpha{ 0 };
            float active_alpha{ 0 };
            bool opened{ false };
        };

        ImGuiWindow* window = fgui->get_window();
        if (!window)
            return;

        ImGuiID id = window->GetID(name.data());

        // Calculate text size
        ImFont* text_font = font->get(main_font_data, 13.0f);
        if (!text_font)
            return;

        ImVec2 text_size = text_font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, name.data());

        // Dropdown dimensions - 28px height like reference
        float box_height = SCALE(28.0f);
        float box_padding = SCALE(10.0f);
        float top_offset = SCALE(4.0f);

        ImVec2 pos = window->DC.CursorPos;
        pos.y += top_offset;

        float width = fgui->content_avail().x - SCALE(10.0f);
        float label_w = ImMin(text_size.x, width * 0.42f);

        ImVec2 label_pos = ImVec2(pos.x, pos.y + (box_height - text_size.y) * 0.5f);
        ImVec2 box_pos = ImVec2(pos.x + label_w + SCALE(22.0f), pos.y);
        ImRect dropdown_rect(box_pos, ImVec2(pos.x + width, box_pos.y + box_height));

        // Full rect for cursor advance
        ImRect full_rect(pos, ImVec2(pos.x + width, pos.y + box_height));

        // Get animation state
        anim_t* anim = fgui->anim_container<anim_t>(id);
        if (!anim)
            return;

        // Hover and click detection
        bool hovered = dropdown_rect.Contains(ImGui::GetMousePos());
        bool clicked = hovered && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None);

        if (clicked)
            anim->opened = !anim->opened;

        // Smooth Animations
        fgui->easing(anim->hover_alpha, (hovered || anim->opened) ? 1.0f : 0.0f, 20.f, static_easing);
        fgui->easing(anim->active_alpha, anim->opened ? 1.0f : 0.0f, 20.f, dynamic_easing);

        ImVec4 dropdown_bg = ImVec4(78.0f / 255.0f, 80.0f / 255.0f, 84.0f / 255.0f, 0.70f);
        ImVec4 dropdown_bg_detail = ImVec4(60.0f / 255.0f, 62.0f / 255.0f, 66.0f / 255.0f, 0.70f);
        ImVec4 list_bg_top = ImVec4(88.0f / 255.0f, 90.0f / 255.0f, 94.0f / 255.0f, 1.0f);
        ImVec4 list_bg_bottom = ImVec4(56.0f / 255.0f, 58.0f / 255.0f, 62.0f / 255.0f, 1.0f);
        ImVec4 text_color = ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        ImVec4 label_color = ImVec4(0.74f, 0.75f, 0.78f, 1.0f);
        ImVec4 text_color_dim = ImVec4(0.6f, 0.6f, 0.65f, 1.0f);
        ImVec4 text_color_active = ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 1.0f);
        ImVec4 selected_color = ImVec4(201.0f / 255.0f, 168.0f / 255.0f, 255.0f / 255.0f, 1.0f);

        draw->text(fgui->window_drawlist(), text_font, 13.0f, label_pos,
            draw->get_clr(label_color), name.data());

        if (anim->hover_alpha > 0.01f)
        {
            float shadow_strength = 0.5f + (0.2f * anim->hover_alpha);
            float shadow_blur = SCALE(15.0f);
            for (int s = 0; s < 4; s++)
            {
                float progress = (float)s / 4.0f;
                float alpha = (1.0f - progress) * 0.15f * shadow_strength;
                float blur_amount = shadow_blur * progress;

                fgui->window_drawlist()->AddRectFilled(
                    ImVec2(dropdown_rect.Min.x - blur_amount, dropdown_rect.Min.y - blur_amount),
                    ImVec2(dropdown_rect.Max.x + blur_amount, dropdown_rect.Max.y + blur_amount),
                    IM_COL32(0, 0, 0, (int)(alpha * 255)),
                    SCALE(4.0f)
                );
            }
        }

        // Background Detail Layer (1px offset)
        draw->rect_filled(fgui->window_drawlist(),
            ImVec2(dropdown_rect.Min.x + SCALE(1.0f), dropdown_rect.Min.y + SCALE(1.0f)),
            dropdown_rect.Max,
            draw->get_clr(dropdown_bg_detail), SCALE(4.0f));

        // Background Main
        draw->rect_filled(fgui->window_drawlist(), dropdown_rect.Min, dropdown_rect.Max,
            draw->get_clr(dropdown_bg), SCALE(4.0f));

        draw->rect(fgui->window_drawlist(), dropdown_rect.Min, dropdown_rect.Max,
            draw->get_clr(ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f,
                0.20f + 0.40f * anim->hover_alpha)),
            SCALE(4.0f), 0, SCALE(1.0f));

        if (*value >= 0 && *value < items_count)
        {
            ImVec2 item_text_pos = ImVec2(
                dropdown_rect.Min.x + box_padding,
                dropdown_rect.Min.y + (box_height - ImGui::CalcTextSize(items[*value]).y) * 0.5f
            );
            draw->text(fgui->window_drawlist(), text_font, 13.0f, item_text_pos,
                draw->get_clr(text_color), items[*value]);
        }

        float chevron_size = SCALE(8.0f);
        float chevron_thickness = SCALE(2.0f);
        ImVec2 chevron_center = ImVec2(
            dropdown_rect.Max.x - SCALE(15.0f),
            dropdown_rect.Min.y + box_height * 0.5f
        );

        float rotation_angle = anim->active_alpha * 3.14159265f; // 0 to PI (180 degrees)

        ImVec2 left_start = ImVec2(chevron_center.x - chevron_size * 0.5f, chevron_center.y - chevron_size * 0.25f);
        ImVec2 left_end = ImVec2(chevron_center.x, chevron_center.y + chevron_size * 0.25f);

        ImVec2 right_start = ImVec2(chevron_center.x, chevron_center.y + chevron_size * 0.25f);
        ImVec2 right_end = ImVec2(chevron_center.x + chevron_size * 0.5f, chevron_center.y - chevron_size * 0.25f);

        auto rotate_point = [](ImVec2 point, ImVec2 center, float angle) -> ImVec2 {
            float s = sinf(angle);
            float c = cosf(angle);
            point.x -= center.x;
            point.y -= center.y;
            float new_x = point.x * c - point.y * s;
            float new_y = point.x * s + point.y * c;
            return ImVec2(new_x + center.x, new_y + center.y);
        };

        left_start = rotate_point(left_start, chevron_center, rotation_angle);
        left_end = rotate_point(left_end, chevron_center, rotation_angle);
        right_start = rotate_point(right_start, chevron_center, rotation_angle);
        right_end = rotate_point(right_end, chevron_center, rotation_angle);

        ImVec4 chevron_color = ImVec4(
            text_color_dim.x + (text_color_active.x - text_color_dim.x) * anim->active_alpha,
            text_color_dim.y + (text_color_active.y - text_color_dim.y) * anim->active_alpha,
            text_color_dim.z + (text_color_active.z - text_color_dim.z) * anim->active_alpha,
            1.0f
        );

        fgui->window_drawlist()->AddLine(left_start, left_end, draw->get_clr(chevron_color), chevron_thickness);
        fgui->window_drawlist()->AddLine(right_start, right_end, draw->get_clr(chevron_color), chevron_thickness);

        if (anim->active_alpha > 0.01f)
        {
            ImVec2 list_pos = ImVec2(dropdown_rect.Min.x, dropdown_rect.Max.y + SCALE(4.0f));
            float item_height = SCALE(28.0f);
            float list_height = item_height * items_count;
            ImVec2 list_size = ImVec2(width, list_height);

            // Slide-Down Animation (center scale)
            ImVec2 list_center = ImVec2(list_pos.x + list_size.x * 0.5f, list_pos.y + list_size.y * 0.5f);
            ImVec2 scaled_pos = ImVec2(
                list_center.x + (list_pos.x - list_center.x) * anim->active_alpha,
                list_center.y + (list_pos.y - list_center.y) * anim->active_alpha
            );
            ImVec2 scaled_size = ImVec2(list_size.x * anim->active_alpha, list_size.y * anim->active_alpha);

            ImRect list_rect(scaled_pos, ImVec2(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y));

            ImDrawList* fg_drawlist = ImGui::GetForegroundDrawList();

            // List Shadow
            float list_shadow_blur = SCALE(18.0f);
            for (int s = 0; s < 4; s++)
            {
                float progress = (float)s / 4.0f;
                float alpha = (1.0f - progress) * 0.32f * anim->active_alpha;
                float blur_amount = list_shadow_blur * progress;

                fg_drawlist->AddRectFilled(
                    ImVec2(list_rect.Min.x - blur_amount, list_rect.Min.y - blur_amount),
                    ImVec2(list_rect.Max.x + blur_amount, list_rect.Max.y + blur_amount),
                    IM_COL32(0, 0, 0, (int)(alpha * 255)),
                    SCALE(6.0f)
                );
            }

            // Clipping
            fg_drawlist->PushClipRect(list_rect.Min, list_rect.Max, true);

            // List Background Detail
            fg_drawlist->AddRectFilled(
                ImVec2(list_rect.Min.x + SCALE(1.0f), list_rect.Min.y + SCALE(1.0f)),
                list_rect.Max,
                draw->get_clr(list_bg_bottom, anim->active_alpha),
                SCALE(4.0f)
            );

            // List Background
            fg_drawlist->AddRectFilledMultiColor(
                list_rect.Min, list_rect.Max,
                draw->get_clr(list_bg_top, anim->active_alpha),
                draw->get_clr(list_bg_top, anim->active_alpha),
                draw->get_clr(list_bg_bottom, anim->active_alpha),
                draw->get_clr(list_bg_bottom, anim->active_alpha)
            );

            fg_drawlist->AddRectFilledMultiColor(
                ImVec2(list_rect.Min.x + SCALE(4.0f), list_rect.Min.y),
                ImVec2(list_rect.Max.x - SCALE(4.0f), list_rect.Min.y + SCALE(2.0f)),
                IM_COL32(255, 255, 255, (int)(72 * anim->active_alpha)),
                IM_COL32(255, 255, 255, (int)(72 * anim->active_alpha)),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, 0)
            );

            fg_drawlist->AddRect(list_rect.Min, list_rect.Max,
                IM_COL32(143, 200, 255, (int)(86 * anim->active_alpha)),
                SCALE(4.0f), 0, SCALE(1.0f));

            // Items
            for (int i = 0; i < items_count; i++)
            {
                float item_y = list_rect.Min.y + (item_height * anim->active_alpha * i);
                ImRect item_rect(ImVec2(list_rect.Min.x, item_y),
                               ImVec2(list_rect.Max.x, item_y + item_height * anim->active_alpha));

                bool item_hovered = item_rect.Contains(ImGui::GetMousePos()) && list_rect.Contains(ImGui::GetMousePos());
                bool is_selected = (i == *value);

                // Item hover/selected effect
                if (item_hovered || is_selected)
                {
                    float item_alpha = (is_selected ? 0.25f : 0.12f) * anim->active_alpha;
                    fg_drawlist->AddRectFilled(
                        item_rect.Min, item_rect.Max,
                        draw->get_clr(is_selected ? selected_color : text_color_active, item_alpha),
                        (i == 0) ? SCALE(4.0f) : (i == items_count - 1) ? SCALE(4.0f) : 0.0f,
                        (i == 0) ? draw_flags_round_corners_top : (i == items_count - 1) ? draw_flags_round_corners_bottom : 0
                    );
                }

                // Item click
                if (item_hovered && fgui->mouse_clicked(mouse_button_left))
                {
                    *value = i;
                    anim->opened = false;
                }

                ImVec2 item_text_pos = ImVec2(
                    item_rect.Min.x + box_padding + (item_hovered ? SCALE(2.5f) : 0.0f) + (is_selected ? SCALE(2.5f) : 0.0f),
                    item_rect.Min.y + (item_height * anim->active_alpha - ImGui::CalcTextSize(items[i]).y) * 0.5f
                );

                // Draw disabled text
                fg_drawlist->AddText(text_font, 13.0f, item_text_pos,
                    draw->get_clr(text_color, anim->active_alpha * (1.0f - (is_selected ? 1.0f : 0.0f))),
                    items[i]);

                fg_drawlist->AddText(text_font, 13.0f, item_text_pos,
                    draw->get_clr(selected_color, anim->active_alpha * (is_selected ? 1.0f : 0.0f)),
                    items[i]);
            }

            fg_drawlist->PopClipRect();

            if (!list_rect.Contains(ImGui::GetMousePos()) && !dropdown_rect.Contains(ImGui::GetMousePos()))
            {
                if (fgui->mouse_clicked(mouse_button_left))
                    anim->opened = false;
            }
        }

        // Tooltip
        render_tooltip(name, hovered);

        // Advance cursor
        fgui->item_size(full_rect);
        fgui->item_add(full_rect, id);
    }
    catch (...)
    {
        // Catch any crash in dropdown
    }
}
