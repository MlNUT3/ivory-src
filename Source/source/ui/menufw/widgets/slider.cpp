#include "../headers/functions.h"
#include "../headers/widgets.h"

void c_widgets::slider_int(std::string_view name, int* value, int min, int max)
{
    // Safety checks
    if (!value || !gui || !draw || !font)
        return;

    try
    {
        struct anim_t
        {
            float fill_width{ 0 };
            float hover_alpha{ 0 };
        };

        ImGuiWindow* window = gui->get_window();
        if (!window)
            return;

        ImGuiID id = window->GetID(name.data());

        float track_height = SCALE(8.0f);
        float total_height = SCALE(26.0f);
        float top_offset = SCALE(4.0f);

        ImVec2 pos = window->DC.CursorPos;
        pos.y += top_offset;

        float width = gui->content_avail().x - SCALE(10.0f);

        ImFont* text_font = font->get(main_font_data, 13.0f);
        if (!text_font)
            return;

        ImVec2 label_size = gui->text_size(text_font, name.data());
        float label_w = ImMin(label_size.x, width * 0.45f);
        float value_w = SCALE(44.0f);

        // Rects - label left, track right, value pinned at the end
        ImRect full_rect(pos, ImVec2(pos.x + width, pos.y + total_height));
        float track_top = pos.y + (total_height - track_height) * 0.5f;
        ImRect track_rect(
            ImVec2(pos.x + label_w + SCALE(20.0f), track_top),
            ImVec2(ImMax(pos.x + label_w + SCALE(46.0f), pos.x + width - value_w), track_top + track_height));

        // Get animation state
        anim_t* anim = gui->anim_container<anim_t>(id);
        if (!anim)
            return;

        // Slider calculations
        float progress = static_cast<float>(*value - min) / static_cast<float>(max - min);
        float target_fill_width = track_rect.GetWidth() * progress;

        // Interaction
        bool hovered = track_rect.Contains(gui->mouse_pos());
        bool held;
        gui->button_behavior(track_rect, id, &hovered, &held);

        // Handle dragging
        if (held)
        {
            const float t = ImClamp((gui->mouse_pos().x - track_rect.Min.x) / track_rect.GetWidth(), 0.0f, 1.0f);
            *value = min + static_cast<int>(t * (max - min));
            *value = ImClamp(*value, min, max);
        }

        // Animations
        gui->easing(anim->fill_width, target_fill_width, 12.f, dynamic_easing);
        gui->easing(anim->hover_alpha, hovered ? 1.0f : 0.0f, 15.f, dynamic_easing);

        ImVec4 track_bg = ImVec4(78.0f / 255.0f, 80.0f / 255.0f, 84.0f / 255.0f, 0.75f);
        ImVec4 track_bg_detail = ImVec4(60.0f / 255.0f, 62.0f / 255.0f, 66.0f / 255.0f, 0.70f);
        ImVec4 track_fill = ImVec4(111.0f / 255.0f, 182.0f / 255.0f, 242.0f / 255.0f, 1.0f);
        ImVec4 track_fill_end = ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 1.0f);
        ImVec4 text_color = ImVec4(0.74f, 0.75f, 0.78f, 1.0f);
        ImVec4 value_color = ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 1.0f);

        float shadow_strength = 0.5f + (0.2f * anim->hover_alpha);
        float shadow_blur = SCALE(15.0f);
        float shadow_offset = SCALE(2.5f * anim->hover_alpha);

        for (int s = 0; s < 4; s++)
        {
            float shadow_progress = (float)s / 4.0f;
            float shadow_alpha = (1.0f - shadow_progress) * 0.15f * shadow_strength;
            float blur_amount = shadow_blur * shadow_progress;

            gui->window_drawlist()->AddRectFilled(
                ImVec2(track_rect.Min.x - blur_amount + shadow_offset, track_rect.Min.y - blur_amount + shadow_offset),
                ImVec2(track_rect.Max.x + blur_amount + shadow_offset, track_rect.Max.y + blur_amount + shadow_offset),
                IM_COL32(0, 0, 0, (int)(shadow_alpha * 255)),
                SCALE(8.0f)
            );
        }

        draw->rect_filled(gui->window_drawlist(),
            ImVec2(track_rect.Min.x + SCALE(1.0f), track_rect.Min.y + SCALE(1.0f)),
            track_rect.Max,
            draw->get_clr(track_bg_detail), SCALE(8.0f));

        draw->rect_filled(gui->window_drawlist(), track_rect.Min, track_rect.Max,
            draw->get_clr(track_bg), SCALE(8.0f));

        draw->rect_filled_multi_color(gui->window_drawlist(),
            ImVec2(track_rect.Min.x + SCALE(4.0f), track_rect.Min.y + SCALE(2.0f)),
            ImVec2(track_rect.Max.x - SCALE(4.0f), track_rect.Min.y + SCALE(3.5f)),
            IM_COL32(255, 255, 255, 70), IM_COL32(255, 255, 255, 70),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

        draw->rect(gui->window_drawlist(), track_rect.Min, track_rect.Max,
            draw->get_clr(ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f,
                0.16f + 0.34f * anim->hover_alpha)),
            SCALE(8.0f), 0, SCALE(1.0f));

        if (anim->fill_width > SCALE(5.0f))
        {
            ImVec2 fill_min = ImVec2(track_rect.Min.x + SCALE(1.0f), track_rect.Min.y + SCALE(1.0f));
            ImVec2 fill_end = ImVec2(track_rect.Min.x + anim->fill_width, track_rect.Max.y);

            draw->rect_filled(gui->window_drawlist(),
                ImVec2(fill_min.x - SCALE(4.0f), fill_min.y - SCALE(4.0f)),
                ImVec2(fill_end.x + SCALE(4.0f), fill_end.y + SCALE(4.0f)),
                IM_COL32(143, 200, 255, 44), SCALE(8.0f));

            draw->rect_filled_multi_color(gui->window_drawlist(),
                fill_min, fill_end,
                draw->get_clr(track_fill), draw->get_clr(track_fill_end),
                draw->get_clr(track_fill_end), draw->get_clr(track_fill),
                SCALE(8.0f), 0);
        }

        // Label - left, vertically centered
        draw->text(gui->window_drawlist(), text_font, 13.0f,
            ImVec2(full_rect.Min.x, full_rect.Min.y + (total_height - label_size.y) * 0.5f),
            draw->get_clr(text_color), name.data());

        // Value - pinned right, always visible
        {
            char value_buf[64];
            sprintf_s(value_buf, sizeof(value_buf), "%d", *value);
            ImVec2 vsz = gui->text_size(text_font, value_buf);
            draw->text(gui->window_drawlist(), text_font, 13.0f,
                ImVec2(full_rect.Max.x - vsz.x, full_rect.Min.y + (total_height - vsz.y) * 0.5f),
                draw->get_clr(value_color), value_buf);
        }

        // Tooltip
        render_tooltip(name, hovered);

        // Advance cursor
        gui->item_size(full_rect);
        gui->item_add(full_rect, id);
    }
    catch (...)
    {
        // Catch any crash in slider
    }
}

void c_widgets::slider_float(std::string_view name, float* value, float min, float max)
{
    // Safety checks
    if (!value || !gui || !draw || !font)
        return;

    try
    {
        struct anim_t
        {
            float fill_width{ 0 };
            float hover_alpha{ 0 };
        };

        ImGuiWindow* window = gui->get_window();
        if (!window)
            return;

        ImGuiID id = window->GetID(name.data());

        float track_height = SCALE(8.0f);
        float total_height = SCALE(26.0f);
        float top_offset = SCALE(4.0f);

        ImVec2 pos = window->DC.CursorPos;
        pos.y += top_offset;

        float width = gui->content_avail().x - SCALE(10.0f);

        ImFont* text_font = font->get(main_font_data, 13.0f);
        if (!text_font)
            return;

        ImVec2 label_size = gui->text_size(text_font, name.data());
        float label_w = ImMin(label_size.x, width * 0.45f);
        float value_w = SCALE(44.0f);

        // Rects - label left, track right, value pinned at the end
        ImRect full_rect(pos, ImVec2(pos.x + width, pos.y + total_height));
        float track_top = pos.y + (total_height - track_height) * 0.5f;
        ImRect track_rect(
            ImVec2(pos.x + label_w + SCALE(20.0f), track_top),
            ImVec2(ImMax(pos.x + label_w + SCALE(46.0f), pos.x + width - value_w), track_top + track_height));

        // Get animation state
        anim_t* anim = gui->anim_container<anim_t>(id);
        if (!anim)
            return;

        // Slider calculations
        float progress = (*value - min) / (max - min);
        progress = ImClamp(progress, 0.0f, 1.0f);
        float target_fill_width = track_rect.GetWidth() * progress;

        // Interaction
        bool hovered = track_rect.Contains(gui->mouse_pos());
        bool held;
        gui->button_behavior(track_rect, id, &hovered, &held);

        // Handle dragging
        if (held)
        {
            const float t = ImClamp((gui->mouse_pos().x - track_rect.Min.x) / track_rect.GetWidth(), 0.0f, 1.0f);
            *value = min + t * (max - min);
            *value = ImClamp(*value, min, max);
        }

        // Animations
        gui->easing(anim->fill_width, target_fill_width, 12.f, dynamic_easing);
        gui->easing(anim->hover_alpha, hovered ? 1.0f : 0.0f, 15.f, dynamic_easing);

        ImVec4 track_bg = ImVec4(78.0f / 255.0f, 80.0f / 255.0f, 84.0f / 255.0f, 0.75f);
        ImVec4 track_bg_detail = ImVec4(60.0f / 255.0f, 62.0f / 255.0f, 66.0f / 255.0f, 0.70f);
        ImVec4 track_fill = ImVec4(111.0f / 255.0f, 182.0f / 255.0f, 242.0f / 255.0f, 1.0f);
        ImVec4 track_fill_end = ImVec4(178.0f / 255.0f, 156.0f / 255.0f, 255.0f / 255.0f, 1.0f);
        ImVec4 text_color = ImVec4(0.74f, 0.75f, 0.78f, 1.0f);
        ImVec4 value_color = ImVec4(178.0f / 255.0f, 156.0f / 255.0f, 255.0f / 255.0f, 1.0f);

        float shadow_strength = 0.5f + (0.2f * anim->hover_alpha);
        float shadow_blur = SCALE(15.0f);
        float shadow_offset = SCALE(2.5f * anim->hover_alpha);

        for (int s = 0; s < 4; s++)
        {
            float shadow_progress = (float)s / 4.0f;
            float shadow_alpha = (1.0f - shadow_progress) * 0.15f * shadow_strength;
            float blur_amount = shadow_blur * shadow_progress;

            gui->window_drawlist()->AddRectFilled(
                ImVec2(track_rect.Min.x - blur_amount + shadow_offset, track_rect.Min.y - blur_amount + shadow_offset),
                ImVec2(track_rect.Max.x + blur_amount + shadow_offset, track_rect.Max.y + blur_amount + shadow_offset),
                IM_COL32(0, 0, 0, (int)(shadow_alpha * 255)),
                SCALE(8.0f)
            );
        }

        draw->rect_filled(gui->window_drawlist(),
            ImVec2(track_rect.Min.x + SCALE(1.0f), track_rect.Min.y + SCALE(1.0f)),
            track_rect.Max,
            draw->get_clr(track_bg_detail), SCALE(8.0f));

        draw->rect_filled(gui->window_drawlist(), track_rect.Min, track_rect.Max,
            draw->get_clr(track_bg), SCALE(8.0f));

        draw->rect_filled_multi_color(gui->window_drawlist(),
            ImVec2(track_rect.Min.x + SCALE(4.0f), track_rect.Min.y + SCALE(2.0f)),
            ImVec2(track_rect.Max.x - SCALE(4.0f), track_rect.Min.y + SCALE(3.5f)),
            IM_COL32(255, 255, 255, 70), IM_COL32(255, 255, 255, 70),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

        draw->rect(gui->window_drawlist(), track_rect.Min, track_rect.Max,
            draw->get_clr(ImVec4(178.0f / 255.0f, 156.0f / 255.0f, 255.0f / 255.0f,
                0.16f + 0.34f * anim->hover_alpha)),
            SCALE(8.0f), 0, SCALE(1.0f));

        if (anim->fill_width > SCALE(5.0f))
        {
            ImVec2 fill_min = ImVec2(track_rect.Min.x + SCALE(1.0f), track_rect.Min.y + SCALE(1.0f));
            ImVec2 fill_end = ImVec2(track_rect.Min.x + anim->fill_width, track_rect.Max.y);

            draw->rect_filled(gui->window_drawlist(),
                ImVec2(fill_min.x - SCALE(4.0f), fill_min.y - SCALE(4.0f)),
                ImVec2(fill_end.x + SCALE(4.0f), fill_end.y + SCALE(4.0f)),
                IM_COL32(178, 156, 255, 44), SCALE(8.0f));

            draw->rect_filled_multi_color(gui->window_drawlist(),
                fill_min, fill_end,
                draw->get_clr(track_fill), draw->get_clr(track_fill_end),
                draw->get_clr(track_fill_end), draw->get_clr(track_fill),
                SCALE(8.0f), 0);
        }

        // Label - left, vertically centered
        draw->text(gui->window_drawlist(), text_font, 13.0f,
            ImVec2(full_rect.Min.x, full_rect.Min.y + (total_height - label_size.y) * 0.5f),
            draw->get_clr(text_color), name.data());

        // Value - pinned right, always visible
        {
            char value_buf[64];
            sprintf_s(value_buf, sizeof(value_buf), "%.2f", *value);
            ImVec2 vsz = gui->text_size(text_font, value_buf);
            draw->text(gui->window_drawlist(), text_font, 13.0f,
                ImVec2(full_rect.Max.x - vsz.x, full_rect.Min.y + (total_height - vsz.y) * 0.5f),
                draw->get_clr(value_color), value_buf);
        }

        // Tooltip
        render_tooltip(name, hovered);

        // Advance cursor
        gui->item_size(full_rect);
        gui->item_add(full_rect, id);
    }
    catch (...)
    {
        // Catch any crash in slider
    }
}
