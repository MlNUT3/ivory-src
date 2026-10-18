#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "keybinds.h"

std::string c_keybinds::vk_to_string(int vk)
{
    if (vk == 0)             return "NONE";
    if (vk == VK_LBUTTON)   return "M1";
    if (vk == VK_RBUTTON)   return "M2";
    if (vk == VK_MBUTTON)   return "M3";
    if (vk == VK_XBUTTON1)  return "M4";
    if (vk == VK_XBUTTON2)  return "M5";
    if (vk == VK_SPACE)     return "SPACE";
    if (vk == VK_RETURN)    return "ENTER";
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return "SHIFT";
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) return "CTRL";
    if (vk == VK_MENU)      return "ALT";
    if (vk == VK_DELETE)    return "DEL";
    if (vk == VK_INSERT)    return "INS";
    if (vk == VK_HOME)      return "HOME";
    if (vk == VK_END)       return "END";
    if (vk == VK_PRIOR)     return "PGUP";
    if (vk == VK_NEXT)      return "PGDN";
    if (vk == VK_ESCAPE)    return "ESC";
    if (vk == VK_TAB)       return "TAB";
    if (vk == VK_CAPITAL)   return "CAPS";

    // F1-F12
    if (vk >= VK_F1 && vk <= VK_F12)
    {
        char buf[8];
        sprintf_s(buf, "F%d", vk - VK_F1 + 1);
        return buf;
    }

    // Numpad
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        char buf[8];
        sprintf_s(buf, "NUM%d", vk - VK_NUMPAD0);
        return buf;
    }

    // A-Z / 0-9
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        char buf[4] = { (char)vk, 0 };
        return buf;
    }

    char buf[32];
    sprintf_s(buf, "KEY%d", vk);
    return buf;
}

void c_keybinds::add_keybind(const std::string& name, int key,
                              keybind_mode mode, bool active)
{
    // Doppelte verhindern
    for (auto& kb : keybinds)
        if (kb.name == name) return;

    keybind_item item;
    item.name   = name;
    item.key    = key;
    item.mode   = mode;
    item.active = active;
    item.state  = (mode == keybind_mode::always);
    keybinds.push_back(item);
}

void c_keybinds::seed_defaults()
{
    // entries are synced live from the real settings by the menu each
    // frame (keybind_list_sync) - no demo binds ever.
    (void)0;
}

bool c_keybinds::is_active(const std::string& name)
{
    for (auto& kb : keybinds)
        if (kb.name == name) return kb.state;
    return false;
}

void c_keybinds::update()
{
    for (auto& kb : keybinds)
    {
        if (kb.listening)
        {
            // ESC = abbrechen
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                kb.listening = false;
                continue;
            }

            static const int scan_keys[] = {
                VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2,
                VK_SPACE, VK_RETURN, VK_SHIFT, VK_CONTROL, VK_MENU,
                VK_DELETE, VK_INSERT, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
                VK_TAB, VK_CAPITAL,
                VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,
                VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,
                VK_NUMPAD0,VK_NUMPAD1,VK_NUMPAD2,VK_NUMPAD3,VK_NUMPAD4,
                VK_NUMPAD5,VK_NUMPAD6,VK_NUMPAD7,VK_NUMPAD8,VK_NUMPAD9,
            };
            for (int vk : scan_keys)
            {
                if (GetAsyncKeyState(vk) & 0x8000)
                {
                    kb.key       = vk;
                    kb.listening = false;
                    break;
                }
            }
            // A-Z, 0-9
            if (kb.listening)
            {
                for (int vk = '0'; vk <= 'Z'; vk++)
                {
                    if (GetAsyncKeyState(vk) & 0x8000)
                    {
                        kb.key       = vk;
                        kb.listening = false;
                        break;
                    }
                }
            }
            continue;
        }

        switch (kb.mode)
        {
        case keybind_mode::always:
            kb.state = true;
            break;

        case keybind_mode::hold:
            if (kb.key != 0)
                kb.state = (GetAsyncKeyState(kb.key) & 0x8000) != 0;
            else
                kb.state = false;
            break;

        case keybind_mode::toggle:
        {
            if (kb.key != 0)
            {
                static std::unordered_map<std::string, bool> prev_down;
                bool down = (GetAsyncKeyState(kb.key) & 0x8000) != 0;
                if (down && !prev_down[kb.name])
                    kb.state = !kb.state;
                prev_down[kb.name] = down;
            }
            break;
        }
        }
    }
}

void c_keybinds::render()
{
    if (!gui || !draw || !font)
        return;

    float spawn_alpha = var->gui.active_hotkeys_spawn_alpha;
    if (spawn_alpha <= 0.01f)
        return;

    if (keybinds.empty())
        seed_defaults();

    try
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        ImFont* text_font  = font->get(main_font_data, 13.0f);
        ImFont* small_font = font->get(main_font_data, 12.0f);
        ImFont* key_font   = font->get(main_font_data, 11.0f);
        ImFont* chip_font  = font->get(main_font_data, 10.0f);
        if (!text_font || !small_font || !key_font || !chip_font)
            return;

        float header_h = SCALE(34.0f);
        float row_h    = SCALE(29.0f);
        float row_gap  = SCALE(5.0f);
        float body_pad = SCALE(8.0f);
        float width    = SCALE(238.0f);

        int shown = 0;
        for (auto& kb : keybinds)
            if (kb.active) shown++;
        if (shown == 0)
            return;

        float total_height = header_h + body_pad +
                             shown * row_h + (shown - 1) * row_gap + body_pad;

        if (!placed)
        {
            placed = true;
            keybind_pos = ImVec2(SCALE(16.0f), SCALE(200.0f));
        }

        ImVec2 pos  = keybind_pos;
        ImVec2 size = ImVec2(width, total_height);

        // Dragging (header only)
        ImRect header_rect(pos, ImVec2(pos.x + size.x, pos.y + header_h));
        bool   header_hovered = header_rect.Contains(ImGui::GetMousePos());
        bool   menu_open      = var->gui.menu_open;

        if (menu_open && header_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            is_dragging = true;
            drag_offset = ImVec2(ImGui::GetMousePos().x - pos.x,
                                 ImGui::GetMousePos().y - pos.y);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            is_dragging = false;

        if (is_dragging)
        {
            keybind_pos.x = ImClamp(ImGui::GetMousePos().x - drag_offset.x,
                                    0.0f, io.DisplaySize.x - width);
            keybind_pos.y = ImClamp(ImGui::GetMousePos().y - drag_offset.y,
                                    0.0f, io.DisplaySize.y - total_height);
            pos = keybind_pos;
            header_rect = ImRect(pos, ImVec2(pos.x + size.x, pos.y + header_h));
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
                ImVec2(pos.x - bl, pos.y - bl),
                ImVec2(pos.x + size.x + bl, pos.y + size.y + bl),
                IM_COL32(0, 0, 0, (int)(al * 255)),
                SCALE(9.0f) + bl);
        }

        // Glass plate
        draw_list->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
        draw_list->AddRectFilledMultiColor(
            pos, ImVec2(pos.x + size.x, pos.y + size.y),
            IM_COL32(38, 40, 45, (int)(0.78f * a)),
            IM_COL32(38, 40, 45, (int)(0.78f * a)),
            IM_COL32(24, 26, 30, (int)(0.80f * a)),
            IM_COL32(24, 26, 30, (int)(0.80f * a)));

        // Header
        draw_list->AddRectFilled(
            pos, ImVec2(pos.x + size.x, pos.y + header_h),
            IM_COL32(58, 60, 65, (int)(0.70f * a)),
            SCALE(9.0f), ImDrawFlags_RoundCornersTop);
        draw_list->AddLine(
            ImVec2(pos.x, pos.y + header_h),
            ImVec2(pos.x + size.x, pos.y + header_h),
            IM_COL32(255, 255, 255, (int)(0.09f * a)));

        static const char* hdr_icon = "\xEF\x80\x93"; // U+F013 gear
        ImVec2 hdr_icon_sz = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, hdr_icon);
        float hdr_y = pos.y + (header_h - hdr_icon_sz.y) * 0.5f;

        draw_list->AddText(small_font, 12.0f,
            ImVec2(pos.x + SCALE(11.0f), hdr_y),
            IM_COL32(143, 200, 255, a), hdr_icon);

        const char* hdr_text = "active keybinds";
        ImVec2 hdr_text_sz = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, hdr_text);
        draw_list->AddText(small_font, 12.0f,
            ImVec2(pos.x + SCALE(11.0f) + hdr_icon_sz.x + SCALE(8.0f),
                   pos.y + (header_h - hdr_text_sz.y) * 0.5f),
            IM_COL32(255, 255, 255, (int)(0.90f * a)), hdr_text);

        char meta_text[32];
        sprintf_s(meta_text, "%d active", shown);
        ImVec2 meta_sz = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, meta_text);
        draw_list->AddText(small_font, 12.0f,
            ImVec2(pos.x + size.x - SCALE(11.0f) - meta_sz.x,
                   pos.y + (header_h - meta_sz.y) * 0.5f),
            IM_COL32(255, 255, 255, (int)(0.34f * a)), meta_text);

        // Rows
        float row_y = pos.y + header_h + body_pad;
        int   drawn = 0;

        for (int idx = 0; idx < (int)keybinds.size(); idx++)
        {
            auto& kb = keybinds[idx];
            if (!kb.active)
                continue;

            ImRect row_rect(ImVec2(pos.x, row_y),
                            ImVec2(pos.x + size.x, row_y + row_h));

            draw_list->AddRectFilled(row_rect.Min, row_rect.Max,
                IM_COL32(255, 255, 255, (int)(0.045f * a)),
                SCALE(6.0f));
            draw_list->AddRect(row_rect.Min, row_rect.Max,
                IM_COL32(255, 255, 255, (int)(0.075f * a)),
                SCALE(6.0f), 0, SCALE(1.0f));

            float cy = row_y + row_h * 0.5f;

            // Mode dot
            ImU32 dot_clr;
            switch (kb.mode)
            {
            case keybind_mode::hold:   dot_clr = IM_COL32(143, 200, 255, a); break;
            case keybind_mode::always: dot_clr = IM_COL32(76, 242, 76, a);   break;
            default:                   dot_clr = IM_COL32(201, 168, 255, a); break;
            }
            ImVec2 dot = ImVec2(pos.x + SCALE(9.0f) + SCALE(3.0f), cy);
            draw_list->AddCircleFilled(dot, SCALE(4.5f), IM_COL32(
                (dot_clr >> 0) & 0xFF, (dot_clr >> 8) & 0xFF, (dot_clr >> 16) & 0xFF, (int)(0.18f * a)));
            draw_list->AddCircleFilled(dot, SCALE(3.0f), dot_clr);

            // Feature name
            ImVec2 name_sz = text_font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, kb.name.c_str());
            draw_list->AddText(text_font, 13.0f,
                ImVec2(pos.x + SCALE(21.0f), cy - name_sz.y * 0.5f),
                IM_COL32(255, 255, 255, (int)(0.88f * a)), kb.name.c_str());

            // Key pill (right)
            std::string key_str = vk_to_string(kb.key);
            ImVec2 key_sz = key_font->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, key_str.c_str());
            float pill_w = key_sz.x + SCALE(18.0f);
            float pill_h = SCALE(19.0f);
            ImVec2 pill_min(pos.x + size.x - SCALE(8.0f) - pill_w, cy - pill_h * 0.5f);
            ImVec2 pill_max(pill_min.x + pill_w, pill_min.y + pill_h);

            bool live = kb.state;
            draw_list->AddRectFilled(pill_min, pill_max,
                live ? IM_COL32(143, 200, 255, (int)(0.12f * a))
                     : IM_COL32(0, 0, 0, (int)(0.34f * a)),
                SCALE(5.0f));
            draw_list->AddRect(pill_min, pill_max,
                live ? IM_COL32(143, 200, 255, (int)(0.55f * a))
                     : IM_COL32(255, 255, 255, (int)(0.13f * a)),
                SCALE(5.0f), 0, SCALE(1.0f));

            draw_list->AddText(key_font, 11.0f,
                ImVec2(pill_min.x + SCALE(9.0f), cy - key_sz.y * 0.5f),
                live ? IM_COL32(143, 200, 255, a)
                     : IM_COL32(255, 255, 255, (int)(0.68f * a)),
                key_str.c_str());

            // Mode chip (left of pill)
            const char* mode_txt;
            ImU32 mode_text_clr, mode_bg_clr, mode_border_clr;
            switch (kb.mode)
            {
            case keybind_mode::hold:
                mode_txt = "HOLD";
                mode_text_clr  = IM_COL32(143, 200, 255, a);
                mode_bg_clr    = IM_COL32(143, 200, 255, (int)(0.13f * a));
                mode_border_clr= IM_COL32(143, 200, 255, (int)(0.34f * a));
                break;
            case keybind_mode::always:
                mode_txt = "ALWAYS";
                mode_text_clr  = IM_COL32(124, 232, 124, a);
                mode_bg_clr    = IM_COL32(76, 242, 76, (int)(0.11f * a));
                mode_border_clr= IM_COL32(76, 242, 76, (int)(0.30f * a));
                break;
            default:
                mode_txt = "TOGGLE";
                mode_text_clr  = IM_COL32(201, 168, 255, a);
                mode_bg_clr    = IM_COL32(201, 168, 255, (int)(0.13f * a));
                mode_border_clr= IM_COL32(201, 168, 255, (int)(0.34f * a));
                break;
            }

            ImVec2 mode_sz = chip_font->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, mode_txt);
            float chip_w = mode_sz.x + SCALE(14.0f);
            float chip_h = SCALE(17.0f);
            ImVec2 chip_min(pill_min.x - SCALE(8.0f) - chip_w, cy - chip_h * 0.5f);
            ImVec2 chip_max(chip_min.x + chip_w, chip_min.y + chip_h);

            draw_list->AddRectFilled(chip_min, chip_max, mode_bg_clr, SCALE(4.0f));
            draw_list->AddRect(chip_min, chip_max, mode_border_clr, SCALE(4.0f), 0, SCALE(1.0f));
            draw_list->AddText(chip_font, 10.0f,
                ImVec2(chip_min.x + SCALE(7.0f), cy - mode_sz.y * 0.5f),
                mode_text_clr, mode_txt);

            row_y += row_h + row_gap;
            drawn++;
        }

        draw_list->PopClipRect();

        // Border
        draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            IM_COL32(255, 255, 255, (int)(0.12f * a)),
            SCALE(9.0f), 0, SCALE(1.0f));
    }
    catch (...) {}
}
