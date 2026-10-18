#include "deezer.h"
#include "../headers/includes.h"
#include "../headers/widgets.h"
#include "../headers/functions.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
    ImVec4 accent(float a)   { return ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, a); }
    ImVec4 muted()           { return ImVec4(0.60f, 0.63f, 0.68f, 1.0f); }
    ImVec4 body_text()       { return ImVec4(0.88f, 0.90f, 0.94f, 1.0f); }

    std::string clock(int ms)
    {
        if (ms < 0) ms = 0;
        const int total = ms / 1000;
        char out[16];
        sprintf_s(out, "%d:%02d", total / 60, total % 60);
        return out;
    }

    bool list_row(const char* label, bool selected, float size_x = 0.0f)
    {
        ImGui::PushStyleColor(ImGuiCol_Header,
            selected ? ImVec4(201.0f / 255.0f, 168.0f / 255.0f, 255.0f / 255.0f, 0.26f)
                     : ImVec4(1.0f, 1.0f, 1.0f, 0.045f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
            ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 0.34f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,
            ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 0.46f));

        const bool pressed = ImGui::Selectable(label, selected, 0, ImVec2(size_x, SCALE(24.0f)));

        ImGui::PopStyleColor(3);
        return pressed;
    }

    void clip(std::string& s, size_t max)
    {
        if (s.size() <= max)
            return;
        if (max < 3)
            return;
        s = s.substr(0, max - 3) + "...";
    }
}

void render_deezer_tab(float width, float height)
{
    static char query_buf[160] = {};
    static bool bootstrapped = false;

    if (!bootstrapped)
    {
        bootstrapped = true;
        deezer->ensure_user_playlists();
        deezer->load_charts();
    }

    const float pad = SCALE(12.0f);
    const float inner_w = width - pad * 2.0f;
    if (inner_w <= 0.0f || height <= 0.0f)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(SCALE(6.0f), SCALE(6.0f)));
    ImGui::SetCursorPosX(pad);

    // ---- search row ----
    const float btn_w = SCALE(74.0f);
    const float src_w = SCALE(96.0f);
    const float search_w = inner_w - (btn_w * 3.0f) - src_w - SCALE(24.0f);

    static const char* kSource[] = { "30s", "audius", "soundcloud" };
    static const char* kHint[] = { "search deezer...", "search audius...", "search soundcloud..." };
    static const char* kList[] = { "search results", "audius - full tracks", "soundcloud - full tracks" };
    static const char* kResult[] = { "search results", "audius results", "soundcloud results" };

    const int prov = (deezer->provider >= 0 && deezer->provider <= 2) ? deezer->provider : 0;

    ImGui::SetCursorPosY(SCALE(6.0f));
    ImGui::SetNextItemWidth(search_w);
    const char* hint = kHint[prov];
    const bool enter_submit = ImGui::InputTextWithHint("##dz_query", hint,
                                                       query_buf, sizeof(query_buf),
                                                       ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    const bool do_search = ImGui::Button("search", ImVec2(btn_w, SCALE(24.0f)));

    ImGui::SameLine();
    const bool do_source = ImGui::Button(kSource[prov], ImVec2(src_w, SCALE(24.0f)));

    ImGui::SameLine();
    const bool do_charts = ImGui::Button("charts", ImVec2(btn_w, SCALE(24.0f)));

    ImGui::SameLine();
    const bool do_playlists = ImGui::Button("playlists", ImVec2(btn_w, SCALE(24.0f)));

    if (do_source)
    {
        deezer->provider = (prov + 1) % 3;
        deezer->results.clear();
        deezer->has_results = false;
        deezer->loading = false;
        deezer->selected_id = -1;
        deezer->list_title = kList[deezer->provider];
    }

    if ((do_search || enter_submit) && query_buf[0] != '\0')
    {
        deezer->search(query_buf);
        deezer->selected_id = -1;
        deezer->list_title = kResult[prov];
    }
    if (do_charts)
    {
        deezer->load_charts();
        deezer->selected_id = -1;
        deezer->list_title = "charts";
    }
    if (do_playlists)
        deezer->load_playlists();

    if (deezer->loading)
        ImGui::TextColored(accent(0.9f), "%s", deezer->downloading ? "downloading..." : "loading...");
    else if (!deezer->status.empty())
        ImGui::TextColored(muted(), "%s", deezer->status.c_str());

    // ---- body ----
    const float header_h = SCALE(54.0f);
    const float footer_h = SCALE(66.0f);
    const float body_top = header_h;
    const float body_h = height - body_top - footer_h - SCALE(8.0f);

    if (body_h < SCALE(60.0f))
    {
        ImGui::PopStyleVar();
        return;
    }

    const float left_w = inner_w * 0.26f;
    const float queue_w = inner_w * 0.24f;
    const float right_w = inner_w - left_w - queue_w - SCALE(16.0f);

    // ---- playlists column ----
    ImGui::SetCursorPos(ImVec2(pad, body_top));
    ImGui::BeginChild("##dz_playlists", ImVec2(left_w, body_h), true);

    static bool show_create = false;
    static char new_name[64] = {};

    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float bw = SCALE(22.0f);

        ImGui::TextColored(accent(1.0f), "playlists");
        ImGui::SameLine(ImGui::GetStyle().WindowPadding.x + avail - bw);
        ImGui::PushID("dz_add_pl");
        if (ImGui::Button("+", ImVec2(bw, SCALE(17.0f))))
            show_create = !show_create;
        ImGui::PopID();
    }
    ImGui::Separator();

    if (show_create)
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(avail - SCALE(50.0f));
        const bool submit = ImGui::InputTextWithHint("##dz_newpl", "playlist name...", new_name,
                                                     sizeof(new_name),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("add", ImVec2(SCALE(46.0f), SCALE(22.0f))) || submit) &&
            new_name[0] != '\0')
        {
            deezer->create_user_playlist(new_name);
            new_name[0] = '\0';
            show_create = false;
        }
    }

    deezer->ensure_user_playlists();

    ImGui::TextColored(muted(), "mine");
    if (deezer->user_playlists.empty())
        ImGui::TextColored(muted(), "none yet - press +");
    else
    {
        for (size_t i = 0; i < deezer->user_playlists.size(); ++i)
        {
            dz_user_playlist& pl = deezer->user_playlists[i];

            std::string label = pl.name.empty() ? "untitled" : pl.name;
            clip(label, 24);
            char id[64];
            sprintf_s(id, "##dzup%zu", i);
            label += id;

            const float avail = ImGui::GetContentRegionAvail().x;
            const float bw = SCALE(20.0f);
            const ImVec2 row_sz(avail - bw - SCALE(4.0f), SCALE(24.0f));
            const ImVec2 start = ImGui::GetCursorPos();

            const bool sel = (deezer->selected_user_playlist == (int)i);
            if (list_row(label.c_str(), sel, row_sz.x))
                deezer->open_user_playlist((int)i);

            const ImVec2 after = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(start.x + row_sz.x + SCALE(4.0f), start.y));
            ImGui::PushID((int)i);
            if (ImGui::Button("x", ImVec2(bw, SCALE(24.0f))))
            {
                deezer->delete_user_playlist((int)i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
            ImGui::SetCursorPos(after);
        }
    }

    ImGui::TextColored(muted(), "discover");

    if (deezer->playlists.empty())
    {
        ImGui::TextWrapped("%s", deezer->loading ? "loading..." : "no playlists yet");
    }
    else
    {
        for (size_t i = 0; i < deezer->playlists.size(); ++i)
        {
            const dz_playlist& p = deezer->playlists[i];

            std::string label = p.title.empty() ? "untitled" : p.title;
            clip(label, 34);
            char id[64];
            sprintf_s(id, "##dzpl%zu", i);
            label += id;

            if (list_row(label.c_str(), deezer->selected_id == p.id))
            {
                deezer->selected_user_playlist = -1;
                deezer->selected_id = p.id;
                deezer->load_playlist(p);
                deezer->list_title = p.title;
            }
        }
    }
    ImGui::EndChild();

    // ---- tracks column ----
    ImGui::SetCursorPos(ImVec2(pad + left_w + SCALE(8.0f), body_top));
    ImGui::BeginChild("##dz_tracks", ImVec2(right_w, body_h), true);

    const char* heading = deezer->list_title.empty() ? "tracks" : deezer->list_title.c_str();
    std::string heading_s = heading;
    clip(heading_s, 44);
    ImGui::TextColored(accent(1.0f), "%s", heading_s.c_str());
    ImGui::Separator();

    if (deezer->results.empty())
    {
        ImGui::TextWrapped("%s", deezer->loading ? "loading..." : "nothing here - search or pick a playlist");
    }
    else
    {
        const bool same_source = (deezer->queue.size() == deezer->results.size());
        const bool viewing_mine = deezer->selected_user_playlist >= 0;

        for (size_t i = 0; i < deezer->results.size(); ++i)
        {
            const dz_track t = deezer->results[i];

            char row[512];
            std::string title = t.title.empty() ? "untitled" : t.title;
            std::string artist = t.artist.empty() ? "unknown" : t.artist;
            clip(title, 44);
            clip(artist, 30);
            sprintf_s(row, "%zu.  %s  -  %s##dztr%zu", i + 1, title.c_str(), artist.c_str(), i);

            const bool selected = same_source && deezer->active && ((int)i == deezer->queue_index);

            const float avail = ImGui::GetContentRegionAvail().x;
            const float bw = SCALE(22.0f);
            const ImVec2 row_sz(avail - bw - SCALE(4.0f), SCALE(24.0f));
            const ImVec2 start = ImGui::GetCursorPos();

            const bool pressed = list_row(row, selected, row_sz.x);
            const ImVec2 after = ImGui::GetCursorPos();

            ImGui::SetCursorPos(ImVec2(start.x + row_sz.x + SCALE(4.0f), start.y));
            ImGui::PushID((int)i);

            if (ImGui::Button(viewing_mine ? "x" : "+", ImVec2(bw, SCALE(24.0f))))
            {
                if (viewing_mine)
                {
                    deezer->remove_from_user_playlist(deezer->selected_user_playlist, (int)i);
                    ImGui::PopID();
                    ImGui::SetCursorPos(after);
                    continue;
                }
                ImGui::OpenPopup("##dz_addto");
            }

            if (ImGui::BeginPopup("##dz_addto"))
            {
                if (deezer->user_playlists.empty())
                {
                    ImGui::TextColored(muted(), "no playlists yet");
                    ImGui::TextColored(muted(), "make one on the left");
                }
                else
                {
                    ImGui::TextColored(muted(), "add to");
                    ImGui::Separator();
                    for (size_t k = 0; k < deezer->user_playlists.size(); ++k)
                    {
                        const bool already =
                            std::any_of(deezer->user_playlists[k].tracks.begin(),
                                        deezer->user_playlists[k].tracks.end(),
                                        [&](const dz_track& e) { return e.id == t.id && e.src_id == t.src_id; });
                        if (already)
                            continue;
                        if (ImGui::MenuItem(deezer->user_playlists[k].name.c_str()))
                            deezer->add_to_user_playlist((int)k, t);
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
            ImGui::SetCursorPos(after);

            if (pressed)
            {
                deezer->play((int)i);
                var->gui.show_spotify = true;
                notify->add_notify(t.provider == prov_deezer ? "playing deezer preview"
                                                              : "playing full track",
                                   info);
            }
        }
    }
    ImGui::EndChild();

    // ---- queue column ----
    ImGui::SetCursorPos(ImVec2(pad + left_w + right_w + SCALE(16.0f), body_top));
    ImGui::BeginChild("##dz_queue", ImVec2(queue_w, body_h), true);

    {
        const bool has_q = !deezer->queue.empty();
        char hdr[96];
        if (has_q)
            sprintf_s(hdr, "queue   %d / %d",
                      deezer->queue_index >= 0 ? deezer->queue_index + 1 : 0,
                      (int)deezer->queue.size());
        else
            strcpy_s(hdr, "queue");

        ImGui::TextColored(accent(1.0f), "%s", hdr);
        ImGui::Separator();

        if (!has_q)
        {
            ImGui::TextWrapped("%s", deezer->loading ? "loading..."
                                                     : "nothing queued - search or pick a playlist");
        }
        else
        {
            if (deezer->active && ImGui::Button("stop", ImVec2(ImGui::GetContentRegionAvail().x, SCALE(20.0f))))
                deezer->stop();

            for (size_t i = 0; i < deezer->queue.size(); ++i)
            {
                const dz_track& t = deezer->queue[i];
                std::string title = t.title.empty() ? "untitled" : t.title;
                std::string artist = t.artist.empty() ? "unknown" : t.artist;
                clip(title, 24);
                clip(artist, 18);

                const bool cur = deezer->active && (int)i == deezer->queue_index;

                char lbl[320];
                if (cur)
                    sprintf_s(lbl, ">> %s  -  %s##dzq%zu", title.c_str(), artist.c_str(), i);
                else
                    sprintf_s(lbl, "%zu.  %s  -  %s##dzq%zu", i + 1, title.c_str(), artist.c_str(), i);

                if (list_row(lbl, cur))
                    deezer->play_queue_at((int)i);
            }
        }
    }

    ImGui::EndChild();

    // ---- now playing ----
    const float np_top = body_top + body_h + SCALE(8.0f);

    ImGui::SetCursorPos(ImVec2(pad, np_top));
    ImGui::BeginChild("##dz_now", ImVec2(inner_w, footer_h - SCALE(8.0f)), true);

    if (!deezer->active)
    {
        ImGui::SetCursorPos(ImVec2(SCALE(12.0f), SCALE(12.0f)));
        ImGui::TextColored(muted(), "no track - pick something from the list");
    }
    else
    {
        std::string title = deezer->now_title.empty() ? "nothing" : deezer->now_title;
        std::string artist = deezer->now_artist.empty() ? "-" : deezer->now_artist;
        clip(title, 40);
        clip(artist, 34);

        ImGui::SetCursorPos(ImVec2(SCALE(12.0f), SCALE(9.0f)));
        ImGui::TextColored(body_text(), "%s", title.c_str());

        ImGui::SetCursorPos(ImVec2(SCALE(12.0f), SCALE(30.0f)));
        ImGui::TextColored(muted(), "%s", artist.c_str());

        // transport
        const float cx = inner_w * 0.40f;
        const ImVec2 sz(SCALE(38.0f), SCALE(24.0f));

        ImGui::SetCursorPos(ImVec2(cx, SCALE(14.0f)));
        ImGui::PushID("dz_prev");
        if (ImGui::Button("<", sz))
            deezer->previous();
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID("dz_play");
        if (ImGui::Button(deezer->is_playing() ? "||" : ">", sz))
            deezer->toggle_play_pause();
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID("dz_next");
        if (ImGui::Button(">", sz))
            deezer->next();
        ImGui::PopID();

        // progress + time
        const float seek_x = inner_w * 0.58f;
        const float time_w = SCALE(96.0f);
        const float seek_w = inner_w - seek_x - time_w - SCALE(20.0f);

        float p = deezer->progress();

        ImGui::SetCursorPos(ImVec2(seek_x, SCALE(12.0f)));
        ImGui::SetNextItemWidth(seek_w);
        if (ImGui::SliderFloat("##dz_seek", &p, 0.0f, 1.0f, ""))
        {
            if (ImGui::IsItemActive())
                deezer->seek(p);
        }

        const std::string time_s = clock(deezer->position_ms()) + " / " + clock(deezer->length_ms());
        ImGui::SetCursorPos(ImVec2(seek_x + seek_w + SCALE(12.0f), SCALE(16.0f)));
        ImGui::TextColored(muted(), "%s", time_s.c_str());

        // queue position
        if (!deezer->queue.empty() && deezer->queue_index >= 0)
        {
            char pos_txt[32];
            sprintf_s(pos_txt, "%d / %zu", deezer->queue_index + 1, deezer->queue.size());
            ImGui::SetCursorPos(ImVec2(cx, SCALE(40.0f)));
            ImGui::TextColored(muted(), "%s", pos_txt);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
