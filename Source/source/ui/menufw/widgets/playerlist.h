#pragma once
#include "../headers/includes.h"

struct player_info
{
    int id;
    std::string name;
    float health;  // 0.0 to 100.0
    bool is_alive;
    ImTextureID avatar;  // Placeholder for now
};

class c_playerlist
{
public:
    void render();

    // Dummy player data for testing
    std::vector<player_info> players;

    int selected_player_id = -1;

private:
    ImVec2 window_pos = ImVec2(500.0f, 15.0f);
    bool is_dragging = false;
    ImVec2 drag_offset = ImVec2(0.0f, 0.0f);
    bool placed = false;

    float scroll_offset = 0.0f;
    float target_scroll = 0.0f;

    // Animation states
    float hover_alpha = 0.0f;
    std::vector<float> player_hover_alpha;
    float teleport_hover = 0.0f;
    float teleport_active = 0.0f;
    float spectate_hover = 0.0f;
    float spectate_active = 0.0f;

    void init_dummy_data();
};

inline c_playerlist* playerlist = new c_playerlist();
