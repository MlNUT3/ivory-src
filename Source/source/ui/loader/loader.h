#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <thread>
#include <fstream>
#include <filesystem>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cstring>
#include <ui/menu/settings/functions.h>
#include <ui/menu/settings/variables.h>
#include <ui/menu/settings/colors.h>
#include <ui/menu/settings/elements.h>
#include <features/system/notifications/notifications.h>
#include <features/system/settings/settings.h>
#include <core/diagnostics/bootlog.h>
#include <core/system/eject.h>
#include <ui/menu/style.h>

namespace loader
{
    namespace config_ui
    {
        inline float screen1_width  = 300.0f; 
        inline float screen1_height = 120.0f; 

        inline float screen2_login_width  = 300.0f;
        inline float screen2_login_height = 145.0f; 

        inline float screen2_register_width  = 300.0f; 
        inline float screen2_register_height = 175.0f;

        inline float screen3_width  = 390.0f;
        inline float screen3_height = 275.0f;

        inline float item_spacing_y = 3.0f;
        inline float item_padding_y = 3.0f;
    }

    enum class screen_state
    {
        auth_choice,
        login,       
        register_acc,
        products,
        progress
    };

    // loader progress stages: 0 idle, 1 waiting for roblox, 2 attached,
    // 3 starting features, 4 ready
    inline int stage = 0;
    inline float progress = 0.0f;
    inline std::string status_text = "";

    inline bool open = false;
    inline screen_state state = screen_state::auth_choice;
    inline char username[64] = "";
    inline char password[64] = "";
    inline char license_key[64] = "";
    inline int selected_product = 0;
    inline bool is_loaded = false;
    inline bool is_loading = false;
    inline void (*on_load_callback)() = nullptr;
    inline bool clean_mode = false;
    inline bool auto_login = false;
    inline std::string logged_in_user = "";
    inline ImVec2 pos = ImVec2(-1.f, -1.f);
    inline bool is_dragging = false;
    inline std::string login_error = "";

    // Access accounts: username / password pairs
    struct auth_account_t { const char* username; const char* password; };
    inline const std::vector<auth_account_t> accounts = {
        { "temp1",  "868670114072535717945907" },
        { "temp2",  "552749600791770552027632" },
        { "temp3",  "181174953272791122484573" },
        { "temp4",  "181456006204209823353533" },
        { "temp5",  "375225643973793144448541" },
        { "temp6",  "356891353639128527344456" },
        { "temp7",  "537408661945283026868078" },
        { "temp8",  "633686399840183434119344" },
        { "temp9",  "625437247912451799518945" },
        { "temp10", "902048976153923512144653" },
    };

    inline bool validate_login(const char* user, const char* pass)
    {
        if (!user || !pass || !user[0] || !pass[0])
            return false;
        for (const auto& acc : accounts)
        {
            if (strcmp(acc.username, user) == 0 && strcmp(acc.password, pass) == 0)
                return true;
        }
        return false;
    }

    inline void ensure_auth_directories()
    {
        try {
            std::filesystem::create_directories("C:\\Ivory\\Auth");
            std::filesystem::create_directories("C:\\Ivory\\Auth\\Profiles");
            std::filesystem::create_directories("C:\\Ivory\\Auth\\Config");
            std::filesystem::create_directories("C:\\Ivory\\Auth\\Logs");
        } catch (...) {}
    }

    inline void save_loader_data()
    {
        try {
            ensure_auth_directories();
            std::ofstream f("C:\\Ivory\\Auth\\Config\\profile.bin", std::ios::binary);
            if (f.is_open())
            {
                f.write(reinterpret_cast<const char*>(&auto_login), sizeof(auto_login));
                f.write(username, sizeof(username));
                f.write(password, sizeof(password));
                f.write(license_key, sizeof(license_key));
                f.close();
            }
        } catch (...) {}
    }

    inline void load_loader_data()
    {
        static bool loaded_once = false;
        if (loaded_once) return;
        loaded_once = true;

        try {
            ensure_auth_directories();
            std::ifstream f("C:\\Ivory\\Auth\\Config\\profile.bin", std::ios::binary);
            if (f.is_open())
            {
                f.read(reinterpret_cast<char*>(&auto_login), sizeof(auto_login));
                f.read(username, sizeof(username));
                f.read(password, sizeof(password));
                f.read(license_key, sizeof(license_key));
                f.close();

                if (auto_login && validate_login(username, password))
                {
                    logged_in_user = username;
                    state = screen_state::products;
                }
                else if (auto_login)
                {
                    // saved credentials are no longer valid - fall back to login
                    auto_login = false;
                    login_error = "Saved credentials are no longer valid";
                }
            }
        } catch (...) {}
    }

    inline ImVec2 get_current_window_size()
    {
        if (state == screen_state::products || state == screen_state::progress)
            return ImVec2(380.0f, 250.0f);
        else if (state == screen_state::login)
            return ImVec2(config_ui::screen2_login_width,
                config_ui::screen2_login_height + (login_error.empty() ? 0.0f : 16.0f));
        else if (state == screen_state::register_acc)
            return ImVec2(config_ui::screen2_register_width, config_ui::screen2_register_height);
        else
            return ImVec2(config_ui::screen1_width, config_ui::screen1_height);
    }

    static inline void draw_roblox_icon(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos)
    {
        float w = max_pos.x - min_pos.x;
        float h = max_pos.y - min_pos.y;
        float sz = ImMin(w, h);
        ImVec2 center = min_pos + ImVec2(w * 0.5f, h * 0.5f);

        float outer_half = sz * 0.42f;
        float inner_half = sz * 0.14f;
        float angle = -0.22f;

        auto rotate_pt = [&](float x, float y) -> ImVec2 {
            float cos_a = std::cos(angle);
            float sin_a = std::sin(angle);
            return center + ImVec2(x * cos_a - y * sin_a, x * sin_a + y * cos_a);
        };

        ImVec2 p1 = rotate_pt(-outer_half, -outer_half);
        ImVec2 p2 = rotate_pt( outer_half, -outer_half);
        ImVec2 p3 = rotate_pt( outer_half,  outer_half);
        ImVec2 p4 = rotate_pt(-outer_half,  outer_half);

        ImU32 icon_col = IM_COL32(200, 205, 215, 255);
        ImU32 hole_col = ImGui::ColorConvertFloat4ToU32(ImVec4(clr->window.background_two));

        draw_list->AddQuadFilled(p1, p2, p3, p4, icon_col);

        ImVec2 h1 = rotate_pt(-inner_half, -inner_half);
        ImVec2 h2 = rotate_pt( inner_half, -inner_half);
        ImVec2 h3 = rotate_pt( inner_half,  inner_half);
        ImVec2 h4 = rotate_pt(-inner_half,  inner_half);

        draw_list->AddQuadFilled(h1, h2, h3, h4, hole_col);
    }

    // ------------------------------------------------------------------
    // 2000s emo / MySpace widget helpers - matches the menu chrome
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // macOS-style loader widgets (themed from the shared style)
    // ------------------------------------------------------------------
    inline style::Palette& P() { return style::P; }

    // animated value store (buttons, bars)
    namespace anim_store
    {
        inline std::unordered_map<ImGuiID, float> vals;
    }
    inline float Anim(const char* id, bool active, float speed = 12.0f)
    {
        const ImGuiID h = ImGui::GetID(id);
        float& v = anim_store::vals[h];
        const float target = active ? 1.0f : 0.0f;
        v += (target - v) * std::clamp(ImGui::GetIO().DeltaTime * speed, 0.0f, 1.0f);
        if (fabsf(target - v) < 0.005f) v = target;
        return v;
    }

    inline void LFill(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float r = 0.0f)
    {
        dl->AddRectFilled(a, b, col, r);
    }
    inline void LRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float r = 0.0f)
    {
        dl->AddRect(a, b, col, r);
    }

    // drifting glow orb - soft radial falloff faked with concentric alpha rings
    inline void GlowOrb(ImDrawList* dl, const ImVec2& c, float radius, ImU32 col)
    {
        for (int i = 8; i >= 1; i--)
        {
            const float r = radius * (0.35f + 0.65f * (float)i / 8.0f);
            const int a = (int)(3.0f + 9.0f * (1.0f - (float)i / 9.0f));
            dl->AddCircleFilled(c, r, (col & 0x00FFFFFFu) | ((ImU32)a << 24), 48);
        }
    }
    inline void LText(ImDrawList* dl, const ImVec2& p, ImU32 col, const char* t)
    {
        dl->AddText(p, col, t);
    }
    inline void DCircle(ImDrawList* dl, const ImVec2& c, float rad, ImU32 col)
    {
        dl->AddCircleFilled(c, rad, col);
    }

    inline bool LButton(const char* label, float w, float h = 30.0f, bool filled = false, bool danger = false)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(label);
        ImGui::InvisibleButton("##btn", ImVec2(w, h));
        const bool pressed = ImGui::IsItemClicked(0);
        const float hov = Anim(label, ImGui::IsItemHovered(), 10.0f);
        ImGui::PopID();

        LFill(dl, pos, pos + ImVec2(w, h),
            filled ? style::Lerp(P().accent, P().accent_hi, hov * 0.6f) : style::Lerp(P().field, P().card_brd, hov * 0.6f), 8.0f);
        LRect(dl, pos, pos + ImVec2(w, h), danger ? P().error : (filled ? P().accent : P().card_brd), 8.0f);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        LText(dl, ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f),
            filled ? P().knob : (danger ? P().error : P().text), label);
        return pressed;
    }

    inline bool LInput(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, const char* hint = nullptr)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 32.0f;

        LFill(dl, pos, pos + ImVec2(w, h), P().field, 8.0f);
        LRect(dl, pos, pos + ImVec2(w, h), P().card_brd, 8.0f);

        ImGui::PushID(label);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(((P().text >> 0) & 0xFF) / 255.0f, ((P().text >> 8) & 0xFF) / 255.0f, ((P().text >> 16) & 0xFF) / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(((P().dim >> 0) & 0xFF) / 255.0f, ((P().dim >> 8) & 0xFF) / 255.0f, ((P().dim >> 16) & 0xFF) / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, (h - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushItemWidth(w);
        bool changed = false;
        if (hint)
            changed = ImGui::InputTextWithHint(label, hint, buf, buf_size, flags);
        else
            changed = ImGui::InputText(label, buf, buf_size, flags);
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::PopID();
        return changed;
    }

    // rotating spinner
    inline void Spinner(ImDrawList* dl, const ImVec2& center, float radius, float thickness)
    {
        const float t = (float)ImGui::GetTime();
        const int segments = 30;
        const float start = t * 4.2f;
        const float arc = IM_PI * 1.35f;
        ImU32 col = P().accent;
        for (int i = 0; i < 10; i++)
        {
            const float a1 = start + (arc * i / 10.0f);
            const float a2 = start + (arc * (i + 1) / 10.0f);
            const float fade = 1.0f - (i / 10.0f) * 0.75f;
            dl->PathArcTo(center, radius, a1, a2, 6);
            dl->PathStroke(ImColor(((col >> 0) & 0xFF), ((col >> 8) & 0xFF), ((col >> 16) & 0xFF), (int)(255 * fade * 0.85f)), 0, thickness);
        }
    }

    inline void render_loader()
    {
        if (!open)
            return;

        load_loader_data();

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 window_size = get_current_window_size();

        if (pos.x < 0.f || pos.y < 0.f)
        {
            pos = ImVec2((io.DisplaySize.x - window_size.x) * 0.5f, (io.DisplaySize.y - window_size.y) * 0.5f);
        }

        static ImVec2 drag_offset = ImVec2(0.f, 0.f);
        ImVec2 mouse_pos = io.MousePos;

        const float tb_h = 26.0f;
        ImRect title_bar_rect(pos, pos + ImVec2(window_size.x, tb_h));
        if (ImGui::IsMouseClicked(0) && title_bar_rect.Contains(mouse_pos))
        {
            is_dragging = true;
            drag_offset = ImVec2(mouse_pos.x - pos.x, mouse_pos.y - pos.y);
        }

        if (is_dragging)
        {
            if (ImGui::IsMouseDown(0))
            {
                pos.x = mouse_pos.x - drag_offset.x;
                pos.y = mouse_pos.y - drag_offset.y;
            }
            else
            {
                is_dragging = false;
            }
        }

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Loader##LoaderMainWindow", &open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar(2);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();

        // window chrome (macOS: rounded + subtle tint)
        dl->AddRectFilled(wp, wp + ws, P().win_bg, 14.0f);
        dl->AddRectFilledMultiColor(wp, wp + ws, P().win_bg, P().win_bg, P().win_tint, P().win_tint);
        LRect(dl, wp, wp + ws, P().card_brd, 14.0f);

        // background shader: drifting glow orbs
        {
            const float t = (float)ImGui::GetTime();
            GlowOrb(dl, wp + ImVec2(ws.x * (0.25f + 0.10f * sinf(t * 0.33f)), ws.y * (0.28f + 0.12f * cosf(t * 0.26f))),
                ws.x * 0.5f, P().accent);
            GlowOrb(dl, wp + ImVec2(ws.x * (0.75f + 0.08f * cosf(t * 0.24f + 2.0f)), ws.y * (0.70f + 0.10f * sinf(t * 0.21f + 1.1f))),
                ws.x * 0.45f, P().win_tint);
        }

        // traffic lights (top right): yellow = hide, red = close
        {
            const ImVec2 tp(wp.x + ws.x - 52.0f, wp.y + 12.0f);
            ImGui::SetCursorScreenPos(tp);
            ImGui::PushID("##tl_y");
            ImGui::InvisibleButton("##y", ImVec2(15.0f, 15.0f));
            if (ImGui::IsItemClicked(0))
                open = false;
            ImGui::PopID();
            DCircle(dl, ImVec2(tp.x + 7.5f, tp.y + 7.5f), 7.0f, IM_COL32(255, 189, 46, 255));

            const ImVec2 rp = tp + ImVec2(22.0f, 0.0f);
            ImGui::SetCursorScreenPos(rp);
            ImGui::PushID("##tl_r");
            ImGui::InvisibleButton("##r", ImVec2(15.0f, 15.0f));
            if (ImGui::IsItemClicked(0))
                open = false;
            ImGui::PopID();
            DCircle(dl, ImVec2(rp.x + 7.5f, rp.y + 7.5f), 7.0f, IM_COL32(255, 95, 86, 255));
        }

        const float start_x = 20.0f;
        const float body_w = ws.x - 40.0f;

        if (state == screen_state::auth_choice)
        {
            LText(dl, wp + ImVec2(start_x, 52.0f), P().text, "Welcome to Ivory");
            LText(dl, wp + ImVec2(start_x, 74.0f), P().dim, "Sign in with your account to continue");

            ImGui::SetCursorScreenPos(wp + ImVec2(start_x, 104.0f));
            if (LButton("Login", body_w, 34.0f, true))
            {
                state = screen_state::login;
            }
        }
        else if (state == screen_state::login)
        {
            LText(dl, wp + ImVec2(start_x, 48.0f), P().text, "Sign in");
            LText(dl, wp + ImVec2(start_x, 70.0f), P().dim, "Enter your credentials");

            ImGui::SetCursorScreenPos(wp + ImVec2(start_x, 96.0f));
            LInput("##Username", username, sizeof(username), 0, "Username");
            ImGui::SetCursorScreenPos(wp + ImVec2(start_x, 134.0f));
            LInput("##Password", password, sizeof(password), ImGuiInputTextFlags_Password, "Password");

            const float btn_w = (body_w - 8.0f) * 0.5f;
            const float btn_y = wp.y + ws.y - (login_error.empty() ? 50.0f : 66.0f);
            ImGui::SetCursorScreenPos(ImVec2(wp.x + start_x, btn_y));
            if (LButton("Continue", btn_w, 32.0f, true))
            {
                if (validate_login(username, password))
                {
                    logged_in_user = username;
                    save_loader_data();
                    login_error.clear();
                    bootlog("auth: login ok (user=%s)", username);
                    notifications::add("Logged in successfully!", 3.0f);
                    state = screen_state::products;
                }
                else
                {
                    login_error = "Invalid username or password";
                    bootlog("auth: login failed (user=%s)", username);
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(wp.x + start_x + btn_w + 8.0f, btn_y));
            if (LButton("Back", btn_w, 32.0f))
            {
                login_error.clear();
                state = screen_state::auth_choice;
            }

            if (!login_error.empty())
            {
                LText(dl, wp + ImVec2(start_x, wp.y + ws.y - 22.0f), P().error, login_error.c_str());
            }
        }
        else if (state == screen_state::products)
        {
            LText(dl, wp + ImVec2(start_x, 46.0f), P().text, "Roadblocks");
            LText(dl, wp + ImVec2(start_x, 68.0f), P().dim,
                is_loaded ? "Cheat is injected and ready" : "Load the cheat to continue");

            ImGui::SetCursorScreenPos(wp + ImVec2(start_x, 96.0f));
            if (LButton(is_loaded ? "Injected - Open Menu" : (is_loading ? "Loading..." : "Load"), body_w, 34.0f, !is_loading))
            {
                if (!is_loaded && !is_loading)
                {
                    is_loading = true;
                    state = screen_state::progress;
                    if (on_load_callback)
                    {
                        std::thread([]() {
                            sys_eject::register_current_thread();
                            if (on_load_callback)
                                on_load_callback();
                            is_loaded = true;
                            is_loading = false;
                            open = false;
                            var->gui.menu_opened = true;
                        }).detach();
                    }
                    else
                    {
                        is_loaded = true;
                        is_loading = false;
                        open = false;
                        var->gui.menu_opened = true;
                    }
                }
                else if (is_loaded)
                {
                    open = false;
                    var->gui.menu_opened = true;
                }
            }

            // auto login / clean / logout / exit row
            ImGui::SetCursorScreenPos(wp + ImVec2(start_x, 140.0f));
            const float half = (body_w - 8.0f) * 0.5f;
            const ImVec2 bpos = ImGui::GetCursorScreenPos();
            if (LButton(auto_login ? "Auto-Login: ON" : "Auto-Login: OFF", half, 26.0f))
            {
                auto_login = !auto_login;
                save_loader_data();
                notifications::add(auto_login ? "Auto-Login: Enabled" : "Auto-Login: Disabled", 3.0f);
            }
            ImGui::SetCursorScreenPos(bpos + ImVec2(half + 8.0f, 0.0f));
            if (LButton(clean_mode ? "Cleaned" : "Clean", half, 26.0f))
            {
                clean_mode = !clean_mode;
                settings::streamproof = clean_mode;
                notifications::add(clean_mode ? "Clean" : "Clean mode deactivated", 3.0f);
            }

            ImGui::SetCursorScreenPos(ImVec2(wp.x + start_x, wp.y + ws.y - 36.0f));
            const float half2 = (body_w - 8.0f) * 0.5f;
            const ImVec2 epos = ImGui::GetCursorScreenPos();
            if (LButton("Logout", half2, 26.0f))
            {
                auto_login = false;
                save_loader_data();
                username[0] = '\0';
                password[0] = '\0';
                logged_in_user.clear();
                state = screen_state::auth_choice;
                notifications::add("Logged out.", 3.0f);
            }
            ImGui::SetCursorScreenPos(epos + ImVec2(half2 + 8.0f, 0.0f));
            if (LButton("Exit", half2, 26.0f, false, true))
            {
                // std::exit here used to close Discord - eject the DLL from
                // the host instead
                bootlog("loader: exit clicked - ejecting");
                open = false;
                var->gui.menu_opened = false;
                sys_eject::request();
            }
        }
        else if (state == screen_state::progress)
        {
            // actual loader view: spinner + status + progress bar
            const ImVec2 c(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f - 26.0f);
            if (stage == 1)
                Spinner(dl, c, 22.0f, 3.0f);
            else
            {
                // attach pulse ring
                const float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 3.0f);
                dl->AddCircle(c, 20.0f + pulse * 4.0f, P().accent_a, 40, 2.5f);
                DCircle(dl, c, 16.0f, P().accent);
            }

            char pct[32];
            snprintf(pct, sizeof(pct), "%d%%", (int)progress);

            // status
            LText(dl, ImVec2(c.x - ImGui::CalcTextSize(status_text.c_str()).x * 0.5f, c.y + 36.0f), P().text,
                status_text.c_str());

            // progress bar (animated fill)
            const float bar_w = body_w * 0.72f;
            const ImVec2 bmin(c.x - bar_w * 0.5f, c.y + 62.0f);
            static float s_bar = 0.0f;
            s_bar += (progress - s_bar) * std::clamp(ImGui::GetIO().DeltaTime * 6.0f, 0.0f, 1.0f);
            LFill(dl, bmin, bmin + ImVec2(bar_w, 6.0f), P().field, 3.0f);
            if (s_bar > 0.01f)
                LFill(dl, bmin, bmin + ImVec2(bar_w * std::clamp(s_bar / 100.0f, 0.0f, 1.0f), 6.0f), P().accent, 3.0f);

            LText(dl, ImVec2(c.x - ImGui::CalcTextSize(pct).x * 0.5f, bmin.y + 14.0f), P().dim, pct);
        }

        ImGui::End();
    }
}
