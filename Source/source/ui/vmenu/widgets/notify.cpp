#include "../headers/functions.h"
#include "../headers/widgets.h"

void c_notify::add_notify(std::string_view text, notify_type type)
{
    notifications.push_back({ notify_count++, std::string(text), type });
}

void c_notify::setup_notify()
{
    int cur_notify_value = 0;
    float accumulated_height = 0.f;

    // Remove finished notifications
    notifications.erase(
        std::remove_if(notifications.begin(), notifications.end(),
            [](const notify_state& n) { return n.notify_alpha <= 0.001f && !n.active_notify; }),
        notifications.end()
    );

    for (auto& notification : notifications)
    {
        cur_notify_value++;

        if (notification.active_notify)
        {
            notification.notify_timer += 1.0f;
        }

        // Auto-close after 5 seconds (300 frames @ 60fps)
        if (notification.notify_timer >= notify_time)
        {
            notification.active_notify = false;
        }

        // Smooth fade in/out
        fgui->easing(notification.notify_alpha, notification.active_notify ? 1.f : 0.f, 8.f, dynamic_easing);

        if (notification.notify_alpha > 0.001f)
        {
            float target_position = accumulated_height + notify_padding.y;
            fgui->easing(notification.notify_pos, target_position, 12.f, dynamic_easing);

            ImVec2 window_size = render_notify(cur_notify_value, notification.notify_alpha, notification.notify_timer, notification.notify_pos, notification.text, notification.type);

            accumulated_height += window_size.y + notify_spacing;
        }
    }
}

ImVec2 c_notify::render_notify(int cur_notify_value, float notify_alpha, float notify_percentage, float notify_pos, std::string_view text, notify_type type)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // Font
    ImFont* notify_font = font->get(main_font_data, 13.0f);
    if (!notify_font)
        notify_font = ImGui::GetFont();

    float padding = SCALE(12.0f);
    float width = SCALE(320.0f);
    float height = SCALE(56.0f);

    ImVec2 pos = ImVec2(io.DisplaySize.x - width - notify_padding.x, notify_pos);
    ImVec2 size = ImVec2(width, height);

    // Slide-in animation
    float slide_offset = (1.0f - notify_alpha) * SCALE(100.0f);
    pos.x += slide_offset;

    const char* icon = "";
    ImVec4 icon_color;

    switch (type)
    {
        case success:
            icon_color = ImVec4(50.0f / 255.0f, 255.0f / 255.0f, 100.0f / 255.0f, notify_alpha);
            icon = "\xEF\x82\x96";  // U+F096
            break;
        case warning:
            icon_color = ImVec4(255.0f / 255.0f, 180.0f / 255.0f, 50.0f / 255.0f, notify_alpha);  // Orange
            icon = "\xEF\x82\x98";  // U+F098
            break;
        case info:
            icon_color = ImVec4(100.0f / 255.0f, 180.0f / 255.0f, 255.0f / 255.0f, notify_alpha);  // Blau
            icon = "\xEF\x82\x97";  // U+F097
            break;
    }

    // Soft Shadow (6 layers)
    float shadow_blur = SCALE(12.0f);
    for (int s = 0; s < 6; s++)
    {
        float progress = (float)s / 6.0f;
        float alpha = (1.0f - progress) * 0.25f * notify_alpha;
        float blur_amount = shadow_blur * progress;

        draw_list->AddRectFilled(
            ImVec2(pos.x - blur_amount, pos.y - blur_amount),
            ImVec2(pos.x + size.x + blur_amount, pos.y + size.y + blur_amount),
            IM_COL32(0, 0, 0, (int)(alpha * 255)),
            SCALE(8.0f) + blur_amount
        );
    }

    ImVec4 bg_top = ImVec4(66.0f / 255.0f, 68.0f / 255.0f, 72.0f / 255.0f, 0.62f * notify_alpha);
    ImVec4 bg_bottom = ImVec4(46.0f / 255.0f, 48.0f / 255.0f, 52.0f / 255.0f, 0.62f * notify_alpha);

    draw_list->AddRectFilledMultiColor(
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        draw->get_clr(bg_top),
        draw->get_clr(bg_top),
        draw->get_clr(bg_bottom),
        draw->get_clr(bg_bottom)
    );

    for (int b = 0; b < 16; b++)
    {
        float blur_inset = SCALE(b * 0.8f);
        float blur_alpha = 0.06f * notify_alpha;

        float brightness = (b % 2 == 0) ? 45.0f : 12.0f;
        ImVec4 blur_color = ImVec4(brightness / 255.0f, brightness / 255.0f, (brightness + 5.0f) / 255.0f, blur_alpha);

        draw_list->AddRectFilled(
            ImVec2(pos.x + blur_inset, pos.y + blur_inset),
            ImVec2(pos.x + size.x - blur_inset, pos.y + size.y - blur_inset),
            draw->get_clr(blur_color),
            ImMax(SCALE(8.0f) - blur_inset, 0.0f)
        );
    }

    float icon_size = SCALE(18.0f);
    ImVec2 icon_pos = ImVec2(
        pos.x + padding,
        pos.y + (height - icon_size) * 0.5f
    );
    draw_list->AddText(notify_font, icon_size, icon_pos, draw->get_clr(icon_color), icon);

    float text_x = pos.x + padding * 2 + icon_size;
    float text_y = pos.y + (height - notify_font->CalcTextSizeA(13.0f, FLT_MAX, 0, text.data()).y) * 0.5f;
    ImVec4 text_color = ImVec4(0.95f, 0.95f, 0.95f, notify_alpha);

    draw_list->AddText(
        notify_font,
        13.0f,
        ImVec2(text_x, text_y),
        draw->get_clr(text_color),
        text.data(),
        text.data() + text.size(),
        width - text_x + pos.x - padding
    );

    float bar_height = SCALE(2.0f);
    float bar_progress = 1.0f - (notify_percentage / notify_time);

    if (bar_progress < 0.0f) bar_progress = 0.0f;
    if (bar_progress > 1.0f) bar_progress = 1.0f;

    float bar_width = size.x * bar_progress;

    if (bar_progress > 0.0f)
    {
        ImVec4 bar_bg = ImVec4(0.26f, 0.27f, 0.29f, 0.6f * notify_alpha);
        draw_list->AddRectFilled(
            ImVec2(pos.x, pos.y + size.y - bar_height),
            ImVec2(pos.x + size.x, pos.y + size.y),
            draw->get_clr(bar_bg),
            SCALE(8.0f),
            ImDrawFlags_RoundCornersBottom
        );

        ImVec4 bar_fill = ImVec4(icon_color.x, icon_color.y, icon_color.z, 0.7f * notify_alpha);
        draw_list->AddRectFilled(
            ImVec2(pos.x, pos.y + size.y - bar_height),
            ImVec2(pos.x + bar_width, pos.y + size.y),
            draw->get_clr(bar_fill),
            SCALE(8.0f),
            ImDrawFlags_RoundCornersBottomLeft
        );
    }

    return size;
}
