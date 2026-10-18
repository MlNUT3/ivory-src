#pragma once
#include "../headers/includes.h"

enum class keybind_mode
{
    toggle = 0,
    hold   = 1,
    always = 2,
};

struct keybind_item
{
    std::string   name;
    int           key      = 0;      // VK code, 0 = unbound
    bool          active   = false;
    keybind_mode  mode     = keybind_mode::toggle;
    bool          state    = false;  // aktueller Zustand (toggle/hold)
    bool          listening = false;
};

class c_keybinds
{
public:
    void render();
    void update();
    void add_keybind(const std::string& name, int key = 0,
                     keybind_mode mode = keybind_mode::toggle, bool active = true);

    bool is_active(const std::string& name);

    void seed_defaults();

    const std::vector<keybind_item>& items() const { return keybinds; }
    std::vector<keybind_item>& items() { return keybinds; }

private:
    ImVec2 keybind_pos  = ImVec2(15.0f, 200.0f);
    bool   is_dragging  = false;
    ImVec2 drag_offset  = ImVec2(0.0f, 0.0f);
    bool   placed       = false;

    int    context_item = -1;
    float  context_alpha = 0.0f;

    std::vector<keybind_item> keybinds;

    std::string vk_to_string(int vk);
};

inline std::unique_ptr<c_keybinds> keybinds = std::make_unique<c_keybinds>();
