#include "../headers/functions.h"
#include "../headers/widgets.h"

bool c_widgets::button(std::string_view name)
{
    if (!fgui || !draw || !font)
        return false;

    try
    {
        struct anim_t
        {
            float hover_alpha{ 0 };
            float active_alpha{ 0 };
        };

        ImGuiWindow* window = fgui->get_window();
        if (!window)
            return false;

        ImGuiID id = window->GetID(name.data());

        // Calculate text size
        ImFont* text_font = font->get(main_font_data, 16.0f);
        if (!text_font)
            return false;

        ImVec2 text_size = text_font->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, name.data());

        // Button dimensions
        float button_height = SCALE(22.0f);
        float top_offset = SCALE(6.0f);

        ImVec2 pos = window->DC.CursorPos;
        pos.y += top_offset;

        float width = fgui->content_avail().x - SCALE(10.0f);

        // Get animation state
        anim_t* anim = fgui->anim_container<anim_t>(id);
        if (!anim)
            return false;

        // Apply active scale effect (shrink when pressed)
        float scale_offset_x = (width * 0.1f) * anim->active_alpha;
        ImVec2 button_pos = ImVec2(pos.x + scale_offset_x * 0.5f, pos.y);
        ImVec2 button_size = ImVec2(width - scale_offset_x, button_height);

        ImRect button_rect(button_pos, ImVec2(button_pos.x + button_size.x, button_pos.y + button_size.y));
        ImRect full_rect(pos, ImVec2(pos.x + width, pos.y + button_height));

        // Hover and click detection
        bool hovered = button_rect.Contains(ImGui::GetMousePos());
        bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool clicked = hovered && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None);

        // Animations
        fgui->easing(anim->hover_alpha, hovered ? 1.0f : 0.0f, 20.f, static_easing);
        fgui->easing(anim->active_alpha, held ? 1.0f : 0.0f, 20.f, dynamic_easing);

        ImVec4 button_bg = ImVec4(78.0f / 255.0f, 80.0f / 255.0f, 84.0f / 255.0f, 0.70f);
        ImVec4 button_bg_detail = ImVec4(60.0f / 255.0f, 62.0f / 255.0f, 66.0f / 255.0f, 0.70f);
        ImVec4 text_color = ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        ImVec4 text_color_active = ImVec4(201.0f / 255.0f, 168.0f / 255.0f, 255.0f / 255.0f, 1.0f);

        float pill = SCALE(11.0f);
        float shadow_strength = 0.5f + (0.2f * anim->hover_alpha);
        float shadow_blur = SCALE(15.0f);
        float shadow_offset = SCALE(2.5f * anim->hover_alpha);

        for (int s = 0; s < 4; s++)
        {
            float progress = (float)s / 4.0f;
            float alpha = (1.0f - progress) * 0.15f * shadow_strength;
            float blur_amount = shadow_blur * progress;

            fgui->window_drawlist()->AddRectFilled(
                ImVec2(button_rect.Min.x - blur_amount + shadow_offset, button_rect.Min.y - blur_amount + shadow_offset),
                ImVec2(button_rect.Max.x + blur_amount + shadow_offset, button_rect.Max.y + blur_amount + shadow_offset),
                IM_COL32(0, 0, 0, (int)(alpha * 255)),
                pill
            );
        }

        if (anim->hover_alpha > 0.01f)
        {
            draw->rect_filled(fgui->window_drawlist(),
                ImVec2(button_rect.Min.x - SCALE(5.0f), button_rect.Min.y - SCALE(5.0f)),
                ImVec2(button_rect.Max.x + SCALE(5.0f), button_rect.Max.y + SCALE(5.0f)),
                IM_COL32(143, 200, 255, (int)(30 * anim->hover_alpha)), SCALE(16.0f));
        }

        // Background Detail Layer
        draw->rect_filled(fgui->window_drawlist(),
            ImVec2(button_rect.Min.x + SCALE(1.0f), button_rect.Min.y + SCALE(1.0f)),
            button_rect.Max,
            draw->get_clr(button_bg_detail), pill);

        // Background Main
        draw->rect_filled(fgui->window_drawlist(), button_rect.Min, button_rect.Max,
            draw->get_clr(button_bg), pill);

        // Text centered - dual layer (disabled + active)
        ImVec2 text_pos = ImVec2(
            button_rect.Min.x + (button_size.x - text_size.x) * 0.5f,
            button_rect.Min.y + (button_height - text_size.y) * 0.5f
        );

        // Disabled text layer
        draw->text(fgui->window_drawlist(), text_font, 16.0f, text_pos,
            draw->get_clr(text_color), name.data());

        draw->text(fgui->window_drawlist(), text_font, 16.0f, text_pos,
            draw->get_clr(text_color_active, anim->active_alpha), name.data());

        // Tooltip
        render_tooltip(name, hovered);

        // Advance cursor
        fgui->item_size(full_rect);
        fgui->item_add(full_rect, id);

        return clicked;
    }
    catch (...)
    {
        return false;
    }
}
