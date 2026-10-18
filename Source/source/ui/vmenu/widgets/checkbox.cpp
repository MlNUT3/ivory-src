#include "../headers/functions.h"
#include "../headers/widgets.h"

void c_widgets::checkbox(std::string_view name, bool* value)
{
    // Safety checks
    if (!value || !fgui || !draw || !font)
        return;

    try
    {
        struct anim_t
        {
            float hover_alpha{ 0 };
            float active_alpha{ 0 };
            float slow_active_alpha{ 0 };
            float held_alpha{ 0 };
        };

        ImGuiWindow* window = fgui->get_window();
        if (!window)
            return;

        ImGuiID id = window->GetID(name.data());

        // Calculate text size
        ImFont* text_font = font->get(main_font_data, 13.0f);
        if (!text_font)
            return;

        ImVec2 text_size = ImGui::CalcTextSize(name.data(), fgui->text_end(name.data()));

        float checkbox_size = SCALE(19.0f);
        float spacing = SCALE(10.0f);
        float top_offset = SCALE(6.0f);

        ImVec2 pos = window->DC.CursorPos;
        pos.y += top_offset;

        ImRect checkbox_rect(pos, pos + ImVec2(checkbox_size, checkbox_size));
        ImRect full_rect(pos, ImVec2(pos.x + checkbox_size + spacing + text_size.x + SCALE(10.0f), pos.y + checkbox_size));

        // Get animation state
        anim_t* anim = fgui->anim_container<anim_t>(id);
        if (!anim)
            return;

        // Hover and click detection
        bool hovered = full_rect.Contains(ImGui::GetMousePos());
        bool clicked = hovered && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None);
        bool mouse_down = hovered && ImGui::IsMouseDown(0);

        if (clicked)
            *value = !*value;

        fgui->easing(anim->hover_alpha, (hovered || *value) ? 1.0f : 0.0f, 20.f, static_easing);
        fgui->easing(anim->active_alpha, *value ? 1.0f : 0.0f, 20.f, dynamic_easing);
        fgui->easing(anim->slow_active_alpha, *value ? 1.0f : 0.0f, 10.f, dynamic_easing);
        fgui->easing(anim->held_alpha, (hovered && mouse_down) ? 0.0f : 1.0f, 8.f, dynamic_easing);

        ImVec4 checkbox_bg = ImVec4(66.0f / 255.0f, 78.0f / 255.0f, 92.0f / 255.0f, 0.72f);
        ImVec4 checkbox_bg_dark = ImVec4(56.0f / 255.0f, 66.0f / 255.0f, 78.0f / 255.0f, 0.70f);
        ImVec4 checkbox_bg_held = ImVec4(46.0f / 255.0f, 48.0f / 255.0f, 52.0f / 255.0f, 0.62f);
        ImVec4 checkbox_border = ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 0.34f);
        ImVec4 active_fill = ImVec4(201.0f / 255.0f, 168.0f / 255.0f, 255.0f / 255.0f, 1.0f);
        ImVec4 checkmark_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 text_color = ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        ImVec4 text_color_disabled = ImVec4(0.6f, 0.6f, 0.65f, 1.0f);

        float shadow_strength = 0.5f + (0.2f * anim->hover_alpha);
        float shadow_blur = SCALE(15.0f);
        float shadow_offset = SCALE(2.5f * anim->hover_alpha);

        for (int s = 0; s < 4; s++)
        {
            float shadow_progress = (float)s / 4.0f;
            float shadow_alpha = (1.0f - shadow_progress) * 0.15f * shadow_strength;
            float blur_amount = shadow_blur * shadow_progress;

            fgui->window_drawlist()->AddRectFilled(
                ImVec2(checkbox_rect.Min.x - blur_amount + shadow_offset, checkbox_rect.Min.y - blur_amount + shadow_offset),
                ImVec2(checkbox_rect.Max.x + blur_amount + shadow_offset, checkbox_rect.Max.y + blur_amount + shadow_offset),
                IM_COL32(0, 0, 0, (int)(shadow_alpha * 255)),
                SCALE(4.0f)
            );
        }

        if (anim->hover_alpha > 0.01f)
        {
            draw->rect_filled(fgui->window_drawlist(),
                ImVec2(checkbox_rect.Min.x - SCALE(5.0f), checkbox_rect.Min.y - SCALE(5.0f)),
                ImVec2(checkbox_rect.Max.x + SCALE(5.0f), checkbox_rect.Max.y + SCALE(5.0f)),
                IM_COL32(143, 200, 255, (int)(34 * anim->hover_alpha)), SCALE(12.0f));
        }

        // Background Detail Layer
        draw->rect_filled(fgui->window_drawlist(),
            ImVec2(checkbox_rect.Min.x + SCALE(1.0f), checkbox_rect.Min.y + SCALE(1.0f)),
            checkbox_rect.Max,
            draw->get_clr(checkbox_bg_dark), SCALE(4.0f));

        // Background Main
        draw->rect_filled(fgui->window_drawlist(), checkbox_rect.Min, checkbox_rect.Max,
            draw->get_clr(checkbox_bg), SCALE(4.0f));

        draw->rect(fgui->window_drawlist(), checkbox_rect.Min, checkbox_rect.Max,
            draw->get_clr(checkbox_border), SCALE(4.0f), 0, SCALE(1.0f));

        if (anim->held_alpha < 0.99f)
        {
            float held_scale = anim->held_alpha;
            ImVec2 held_center = ImVec2(
                checkbox_rect.Min.x + checkbox_size * 0.5f,
                checkbox_rect.Min.y + checkbox_size * 0.5f
            );
            ImVec2 held_min = ImVec2(
                held_center.x - (checkbox_size * 0.5f * held_scale),
                held_center.y - (checkbox_size * 0.5f * held_scale)
            );
            ImVec2 held_max = ImVec2(
                held_center.x + (checkbox_size * 0.5f * held_scale),
                held_center.y + (checkbox_size * 0.5f * held_scale)
            );

            draw->rect_filled(fgui->window_drawlist(), held_min, held_max,
                draw->get_clr(checkbox_bg_held, 1.0f - anim->held_alpha), SCALE(4.0f));
        }

        if (anim->active_alpha > 0.01f)
        {
            float quint_ease;
            if (*value)
            {
                // In Quint: t^5
                quint_ease = anim->active_alpha * anim->active_alpha * anim->active_alpha * anim->active_alpha * anim->active_alpha;
            }
            else
            {
                // Out Quint: 1 - (1-t)^5
                float inv = 1.0f - anim->active_alpha;
                quint_ease = 1.0f - (inv * inv * inv * inv * inv);
            }

            ImVec2 fill_center = ImVec2(
                checkbox_rect.Min.x + checkbox_size * 0.5f,
                checkbox_rect.Min.y + checkbox_size * 0.5f
            );

            ImVec2 fill_size = ImVec2(checkbox_size * quint_ease, checkbox_size * quint_ease);
            ImVec2 fill_min = ImVec2(
                fill_center.x - fill_size.x * 0.5f,
                fill_center.y - fill_size.y * 0.5f
            );
            ImVec2 fill_max = ImVec2(
                fill_center.x + fill_size.x * 0.5f,
                fill_center.y + fill_size.y * 0.5f
            );

            float dynamic_rounding = SCALE(4.0f) * (5.0f * (1.0f - quint_ease) + 1.0f);

            draw->rect_filled(fgui->window_drawlist(), fill_min, fill_max,
                draw->get_clr(active_fill, quint_ease), dynamic_rounding);
        }

        if (anim->slow_active_alpha > 0.01f)
        {
            float slow_quint;
            if (*value)
            {
                slow_quint = anim->slow_active_alpha * anim->slow_active_alpha * anim->slow_active_alpha * anim->slow_active_alpha * anim->slow_active_alpha;
            }
            else
            {
                float inv = 1.0f - anim->slow_active_alpha;
                slow_quint = 1.0f - (inv * inv * inv * inv * inv);
            }

            float checkmark_size_total = SCALE(14.0f) * (*value ?
                (anim->active_alpha * anim->active_alpha * anim->active_alpha * anim->active_alpha * anim->active_alpha) :
                (1.0f - ((1.0f - anim->active_alpha) * (1.0f - anim->active_alpha) * (1.0f - anim->active_alpha) * (1.0f - anim->active_alpha) * (1.0f - anim->active_alpha))));

            ImVec2 center = ImVec2(
                checkbox_rect.Min.x + checkbox_size * 0.5f,
                checkbox_rect.Min.y + checkbox_size * 0.5f
            );

            // Verbesserte Checkmark Proportionen
            float check_width = checkmark_size_total * 0.85f;
            float check_height = checkmark_size_total * 0.70f;

            ImVec2 p1 = ImVec2(
                std::roundf(center.x - check_width * 0.35f),
                std::roundf(center.y - check_height * 0.05f)
            );
            ImVec2 p2 = ImVec2(
                std::roundf(center.x - check_width * 0.05f),
                std::roundf(center.y + check_height * 0.35f)
            );
            ImVec2 p3 = ImVec2(
                std::roundf(center.x + check_width * 0.45f),
                std::roundf(center.y - check_height * 0.50f)
            );

            float thickness = SCALE(2.5f);
            ImU32 check_color = draw->get_clr(checkmark_color, slow_quint);

            fgui->window_drawlist()->PathLineTo(p1);
            fgui->window_drawlist()->PathLineTo(p2);
            fgui->window_drawlist()->PathLineTo(p3);
            fgui->window_drawlist()->PathStroke(check_color, ImDrawFlags_None, thickness);
        }

        float text_offset_x = spacing + std::roundf((spacing * 0.5f) * anim->hover_alpha);
        ImVec2 text_pos = ImVec2(
            checkbox_rect.Max.x + text_offset_x,
            checkbox_rect.Min.y + (checkbox_size - text_size.y) * 0.5f
        );

        float text_quint = *value ?
            (anim->active_alpha * anim->active_alpha * anim->active_alpha * anim->active_alpha * anim->active_alpha) :
            (1.0f - ((1.0f - anim->active_alpha) * (1.0f - anim->active_alpha) * (1.0f - anim->active_alpha) * (1.0f - anim->active_alpha) * (1.0f - anim->active_alpha)));

        draw->text(fgui->window_drawlist(), text_font, 13.0f, text_pos,
            draw->get_clr(text_color_disabled, 1.0f - text_quint), name.data());

        draw->text(fgui->window_drawlist(), text_font, 13.0f, text_pos,
            draw->get_clr(text_color, text_quint), name.data());

        // Tooltip
        render_tooltip(name, hovered);

        // Advance cursor
        fgui->item_size(full_rect);
        fgui->item_add(full_rect, id);
    }
    catch (...)
    {
        // Catch any crash in checkbox
    }
}
