#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "playerlist.h"
#include <sdk/cache/core/cache.h>

void c_playerlist::init_dummy_data()
{
    static bool initialized = false;
    if (initialized) return;

    // Dummy players for testing
    players.clear();
    player_hover_alpha.clear();

    for (int i = 0; i < 25; i++)
    {
        player_info p;
        p.id = i;
        p.name = "Player " + std::to_string(i + 1);
        p.health = 50.0f + (float)(rand() % 50);
        p.is_alive = (rand() % 10) != 0;  // 90% alive
        p.avatar = nullptr;

        players.push_back(p);
        player_hover_alpha.push_back(0.0f);
    }

    initialized = true;
}

void c_playerlist::render()
{
    if (!gui || !draw || !font)
        return;

    float anim_alpha = var->gui.playerlist_spawn_alpha;
    if (anim_alpha <= 0.01f)
        return;

        auto snap = cache::get_players_snap();
        players.clear();
        player_hover_alpha.clear();
        if (snap) for (const auto& e : *snap) {
            player_info p;
            p.id = (int)players.size();
            p.name = !e.display_name.empty() ? e.display_name : e.name;
            p.health = e.health;
            p.is_alive = e.health > 0.f;
            p.avatar = nullptr;
            players.push_back(p);
            player_hover_alpha.push_back(0.f);
        }
        if (players.empty()) return;

    try
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        ImFont* text_font  = font->get(main_font_data, 13.0f);
        ImFont* small_font = font->get(main_font_data, 12.0f);
        ImFont* init_font  = font->get(main_font_data, 11.0f);
        if (!text_font || !small_font || !init_font)
            return;

        float width          = SCALE(274.0f);
        float header_height  = SCALE(36.0f);
        float row_height     = SCALE(30.0f);
        float row_gap        = SCALE(4.0f);
        float row_pitch      = row_height + row_gap;
        float body_pad       = SCALE(7.0f);
        float max_visible    = 7;
        float list_height    = max_visible * row_height + (max_visible - 1) * row_gap;
        float total_height   = header_height + body_pad + list_height + body_pad;
        float padding        = SCALE(7.0f);

        if (!placed && var->gui.menu_rect_valid)
        {
            placed = true;
            window_pos = ImVec2(var->gui.menu_rect_min.x + var->gui.menu_rect_size.x + SCALE(20.0f),
                                var->gui.menu_rect_min.y + SCALE(5.0f));
        }

        ImVec2 pos  = window_pos;
        ImVec2 size = ImVec2(width, total_height);

        ImRect window_rect(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        // Dragging (header only)
        bool hovered   = window_rect.Contains(ImGui::GetMousePos());
        bool menu_open = var->gui.menu_open;

        if (menu_open && hovered && ImGui::GetMousePos().y < pos.y + header_height)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                is_dragging = true;
                drag_offset = ImVec2(
                    ImGui::GetMousePos().x - pos.x,
                    ImGui::GetMousePos().y - pos.y
                );
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            is_dragging = false;

        if (is_dragging)
        {
            window_pos.x = ImGui::GetMousePos().x - drag_offset.x;
            window_pos.y = ImGui::GetMousePos().y - drag_offset.y;

            window_pos.x = ImMax(0.0f, ImMin(window_pos.x, io.DisplaySize.x - width));
            window_pos.y = ImMax(0.0f, ImMin(window_pos.y, io.DisplaySize.y - total_height));

            pos = window_pos;
            window_rect = ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        }

        gui->easing(hover_alpha, hovered ? 1.0f : 0.0f, 10.0f, dynamic_easing);

        int a = (int)(anim_alpha * 255.0f);

        // Soft drop shadow
        float shadow_blur = SCALE(14.0f);
        for (int s = 0; s < 5; s++)
        {
            float p  = (float)s / 5.0f;
            float al = (1.0f - p) * 0.34f * anim_alpha;
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
            pos, ImVec2(pos.x + width, pos.y + header_height),
            IM_COL32(58, 60, 65, (int)(0.70f * a)),
            SCALE(9.0f), ImDrawFlags_RoundCornersTop);
        draw_list->AddLine(
            ImVec2(pos.x, pos.y + header_height),
            ImVec2(pos.x + width, pos.y + header_height),
            IM_COL32(255, 255, 255, (int)(0.09f * a)));

        static const char* hdr_icon = "\xEF\x83\x80"; // U+F0C0 users
        ImVec2 hdr_icon_sz = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, hdr_icon);
        draw_list->AddText(small_font, 12.0f,
            ImVec2(pos.x + SCALE(11.0f), pos.y + (header_height - hdr_icon_sz.y) * 0.5f),
            IM_COL32(143, 200, 255, a), hdr_icon);

        const char* title = "players";
        ImVec2 title_size = text_font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, title);
        draw_list->AddText(text_font, 13.0f,
            ImVec2(pos.x + SCALE(11.0f) + hdr_icon_sz.x + SCALE(8.0f),
                   pos.y + (header_height - title_size.y) * 0.5f),
            IM_COL32(255, 255, 255, (int)(0.92f * a)), title);

        int alive_count = 0;
        for (auto& p : players)
            if (p.is_alive) alive_count++;

        char count_text[32];
        sprintf_s(count_text, "%d / %d alive", alive_count, (int)players.size());
        ImVec2 count_size = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, count_text);
        draw_list->AddText(small_font, 12.0f,
            ImVec2(pos.x + width - SCALE(11.0f) - count_size.x,
                   pos.y + (header_height - count_size.y) * 0.5f),
            IM_COL32(255, 255, 255, (int)(0.34f * a)), count_text);

        // === LIST ===
        ImVec2 list_pos = ImVec2(pos.x, pos.y + header_height + body_pad);
        ImRect list_rect(list_pos, ImVec2(list_pos.x + width, list_pos.y + list_height));

        if (list_rect.Contains(ImGui::GetMousePos()))
        {
            float scroll_delta = io.MouseWheel * row_pitch * 2.0f;
            target_scroll -= scroll_delta;

            float max_scroll = ImMax(0.0f, (float)players.size() * row_pitch - row_gap - list_height);
            target_scroll = ImClamp(target_scroll, 0.0f, max_scroll);
        }

        gui->easing(scroll_offset, target_scroll, 12.0f, dynamic_easing);

        draw_list->PushClipRect(list_pos, ImVec2(list_pos.x + width, list_pos.y + list_height), true);

        int start_index = (int)(scroll_offset / row_pitch);
        int end_index = ImMin((int)players.size(), start_index + (int)max_visible + 2);

        for (int i = start_index; i < end_index; i++)
        {
            player_info& player = players[i];
            float y_offset = list_pos.y + (i * row_pitch) - scroll_offset;

            ImVec2 item_pos = ImVec2(list_pos.x + padding, y_offset);
            float  item_w   = width - padding * 2;
            ImVec2 item_max = ImVec2(item_pos.x + item_w, item_pos.y + row_height);
            ImRect item_rect(item_pos, item_max);

            bool item_hovered = item_rect.Contains(ImGui::GetMousePos()) && list_rect.Contains(ImGui::GetMousePos());
            gui->easing(player_hover_alpha[i], item_hovered ? 1.0f : 0.0f, 15.0f, dynamic_easing);

            bool is_selected = (selected_player_id == player.id);

            if (item_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                selected_player_id = player.id;

            if (is_selected)
            {
                draw_list->AddRectFilledMultiColor(
                    item_pos, item_max,
                    IM_COL32(143, 200, 255, (int)(0.16f * a)),
                    IM_COL32(143, 200, 255, (int)(0.05f * a)),
                    IM_COL32(143, 200, 255, (int)(0.04f * a)),
                    IM_COL32(143, 200, 255, (int)(0.09f * a)));
                draw_list->AddRect(item_pos, item_max,
                    IM_COL32(143, 200, 255, (int)(0.38f * a)),
                    SCALE(6.0f), 0, SCALE(1.0f));
                draw_list->AddRectFilled(
                    ImVec2(item_pos.x, item_pos.y + SCALE(5.0f)),
                    ImVec2(item_pos.x + SCALE(2.5f), item_max.y - SCALE(5.0f)),
                    IM_COL32(143, 200, 255, a), SCALE(1.5f));
            }
            else if (player_hover_alpha[i] > 0.01f)
            {
                draw_list->AddRectFilled(item_pos, item_max,
                    IM_COL32(255, 255, 255, (int)(player_hover_alpha[i] * 0.05f * a)),
                    SCALE(6.0f));
                draw_list->AddRect(item_pos, item_max,
                    IM_COL32(255, 255, 255, (int)(player_hover_alpha[i] * 0.08f * a)),
                    SCALE(6.0f), 0, SCALE(1.0f));
            }

            // Avatar
            float avatar_size = SCALE(20.0f);
            ImVec2 avatar_center = ImVec2(item_pos.x + avatar_size * 0.5f + SCALE(3.0f),
                                          item_pos.y + row_height * 0.5f);

            ImU32 av_fill = player.is_alive
                ? IM_COL32(143, 200, 255, (int)(0.16f * a))
                : IM_COL32(255, 107, 107, (int)(0.14f * a));
            ImU32 av_border = player.is_alive
                ? IM_COL32(143, 200, 255, (int)(0.45f * a))
                : IM_COL32(255, 107, 107, (int)(0.40f * a));

            draw_list->AddCircleFilled(avatar_center, avatar_size * 0.5f, av_fill, 24);
            draw_list->AddCircle(avatar_center, avatar_size * 0.5f, av_border, 24, SCALE(1.0f));

            if (!player.name.empty())
            {
                char initial[2] = { player.name[0], '\0' };
                ImVec2 initial_size = init_font->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, initial);
                draw_list->AddText(init_font, 11.0f,
                    ImVec2(avatar_center.x - initial_size.x * 0.5f,
                           avatar_center.y - initial_size.y * 0.5f),
                    player.is_alive ? IM_COL32(207, 231, 255, a)
                                    : IM_COL32(255, 150, 150, (int)(0.80f * a)),
                    initial);
            }

            // Name
            float text_x = item_pos.x + SCALE(36.0f);
            ImVec2 name_size = text_font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, player.name.c_str());
            ImVec2 name_pos = ImVec2(text_x, item_pos.y + (row_height - name_size.y) * 0.5f);

            if (player.is_alive)
            {
                draw_list->AddText(text_font, 13.0f, name_pos,
                    IM_COL32(255, 255, 255, (int)(0.90f * a)), player.name.c_str());
            }
            else
            {
                draw_list->AddText(text_font, 13.0f, name_pos,
                    IM_COL32(255, 255, 255, (int)(0.40f * a)), player.name.c_str());
                draw_list->AddLine(
                    ImVec2(name_pos.x, name_pos.y + name_size.y * 0.5f),
                    ImVec2(name_pos.x + name_size.x, name_pos.y + name_size.y * 0.5f),
                    IM_COL32(255, 255, 255, (int)(0.25f * a)), 1.0f);
            }

            // Action buttons (selected only)
            if (is_selected)
            {
                float act_size = SCALE(21.0f);
                float act_gap  = SCALE(5.0f);
                float act2_x   = item_pos.x + item_w - SCALE(8.0f) - act_size;
                float act1_x   = act2_x - act_gap - act_size;
                float act_y    = item_pos.y + (row_height - act_size) * 0.5f;

                static const char* ico_bolt = "\xEF\x83\xA7"; // U+F0E7
                static const char* ico_eye  = "\xEF\x81\xAE"; // U+F06E

                ImRect tp_rect(ImVec2(act1_x, act_y), ImVec2(act1_x + act_size, act_y + act_size));
                ImRect sp_rect(ImVec2(act2_x, act_y), ImVec2(act2_x + act_size, act_y + act_size));

                bool tp_hover = tp_rect.Contains(ImGui::GetMousePos());
                bool sp_hover = sp_rect.Contains(ImGui::GetMousePos());

                auto draw_act = [&](const ImRect& r, bool hov, const char* icon)
                {
                    draw_list->AddRectFilled(r.Min, r.Max,
                        hov ? IM_COL32(143, 200, 255, (int)(0.13f * a))
                            : IM_COL32(0, 0, 0, (int)(0.35f * a)),
                        SCALE(5.0f));
                    draw_list->AddRect(r.Min, r.Max,
                        hov ? IM_COL32(143, 200, 255, (int)(0.55f * a))
                            : IM_COL32(255, 255, 255, (int)(0.14f * a)),
                        SCALE(5.0f), 0, SCALE(1.0f));

                    ImVec2 icon_sz = small_font->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, icon);
                    draw_list->AddText(small_font, 12.0f,
                        ImVec2(r.Min.x + (act_size - icon_sz.x) * 0.5f,
                               r.Min.y + (act_size - icon_sz.y) * 0.5f),
                        hov ? IM_COL32(143, 200, 255, a)
                            : IM_COL32(255, 255, 255, (int)(0.60f * a)),
                        icon);
                };

                draw_act(tp_rect, tp_hover, ico_bolt);
                draw_act(sp_rect, sp_hover, ico_eye);

                if (tp_hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    notify->add_notify("Teleported to player", success);
                if (sp_hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    notify->add_notify("Spectating player", info);
            }
        }

        draw_list->PopClipRect();
        draw_list->PopClipRect();

        // Border
        draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            IM_COL32(255, 255, 255, (int)(0.12f * a)),
            SCALE(9.0f), 0, SCALE(1.0f));
    }
    catch (...)
    {
    }
}