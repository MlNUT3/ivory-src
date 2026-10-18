#pragma once
#include "../headers/includes.h"

class c_playerbar
{
public:
    void render();

private:
    ImVec2 playerbar_pos = ImVec2(15.0f, 100.0f); // Start position
    bool is_dragging = false;
    ImVec2 drag_offset = ImVec2(0.0f, 0.0f);
    bool placed = false;

    // Identity card
    std::string username = "User";

    // License state — hidden until a key is set up
    bool key_set = true;
    std::string key_time_left = "23d left";
};

inline std::unique_ptr<c_playerbar> playerbar = std::make_unique<c_playerbar>();
